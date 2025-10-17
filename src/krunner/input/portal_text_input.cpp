/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "portal_text_input.h"
#include "../logging_categories.h"

#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTimer>
#include <linux/input-event-codes.h>

namespace KRunner {
namespace YubiKey {

PortalTextInput::PortalTextInput(QObject *parent)
    : TextInputProvider(parent)
{
    qCDebug(TextInputLog) << "PortalTextInput: Initializing xdg-desktop-portal + libei";
    initializePortal();
}

PortalTextInput::~PortalTextInput()
{
    cleanup();
}

void PortalTextInput::cleanup()
{
    qCDebug(TextInputLog) << "PortalTextInput: Cleaning up";

    if (m_eiNotifier) {
        m_eiNotifier->setEnabled(false);
        delete m_eiNotifier;
        m_eiNotifier = nullptr;
    }

    if (m_device) {
        ei_device_unref(m_device);
        m_device = nullptr;
    }

    if (m_seat) {
        ei_seat_unref(m_seat);
        m_seat = nullptr;
    }

    if (m_ei) {
        ei_unref(m_ei);
        m_ei = nullptr;
    }

    if (m_oeffisNotifier) {
        m_oeffisNotifier->setEnabled(false);
        delete m_oeffisNotifier;
        m_oeffisNotifier = nullptr;
    }

    if (m_oeffis) {
        oeffis_unref(m_oeffis);
        m_oeffis = nullptr;
    }

    m_portalConnected = false;
    m_deviceReady = false;
}

bool PortalTextInput::initializePortal()
{
    m_oeffis = oeffis_new(nullptr);
    if (!m_oeffis) {
        qCWarning(TextInputLog) << "PortalTextInput: Failed to create oeffis context";
        return false;
    }

    int fd = oeffis_get_fd(m_oeffis);
    if (fd < 0) {
        qCWarning(TextInputLog) << "PortalTextInput: Failed to get oeffis fd";
        oeffis_unref(m_oeffis);
        m_oeffis = nullptr;
        return false;
    }

    m_oeffisNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_oeffisNotifier, &QSocketNotifier::activated, this, &PortalTextInput::handleOeffisEvents);

    qCDebug(TextInputLog) << "PortalTextInput: oeffis context created, fd:" << fd;
    return true;
}

void PortalTextInput::handleOeffisEvents()
{
    if (!m_oeffis) {
        return;
    }

    oeffis_dispatch(m_oeffis);

    enum oeffis_event_type event;
    while ((event = oeffis_get_event(m_oeffis)) != OEFFIS_EVENT_NONE) {
        switch (event) {
        case OEFFIS_EVENT_CONNECTED_TO_EIS:
            qCDebug(TextInputLog) << "PortalTextInput: Connected to EIS, attempting to connect libei";
            m_portalConnected = true;
            connectToEis();
            break;

        case OEFFIS_EVENT_CLOSED:
            qCDebug(TextInputLog) << "PortalTextInput: Portal session closed";
            m_portalConnected = false;
            // Defer cleanup to avoid deleting notifier while in its callback
            QTimer::singleShot(0, this, &PortalTextInput::cleanup);
            break;

        case OEFFIS_EVENT_DISCONNECTED:
            qCWarning(TextInputLog) << "PortalTextInput: Disconnected from portal:"
                       << oeffis_get_error_message(m_oeffis);
            m_portalConnected = false;
            m_permissionRejected = true;  // User explicitly rejected permission
            // Defer cleanup to avoid deleting notifier while in its callback
            QTimer::singleShot(0, this, &PortalTextInput::cleanup);
            break;

        default:
            break;
        }
    }
}

bool PortalTextInput::connectToEis()
{
    if (!m_oeffis || !m_portalConnected) {
        qCWarning(TextInputLog) << "PortalTextInput: Cannot connect to EIS - portal not ready";
        return false;
    }

    int eis_fd = oeffis_get_eis_fd(m_oeffis);
    if (eis_fd < 0) {
        qCWarning(TextInputLog) << "PortalTextInput: Failed to get EIS fd";
        return false;
    }

    m_ei = ei_new_sender(nullptr);
    if (!m_ei) {
        qCWarning(TextInputLog) << "PortalTextInput: Failed to create ei sender context";
        return false;
    }

    if (ei_setup_backend_fd(m_ei, eis_fd) != 0) {
        qCWarning(TextInputLog) << "PortalTextInput: Failed to setup ei backend";
        ei_unref(m_ei);
        m_ei = nullptr;
        return false;
    }

    int ei_fd = ei_get_fd(m_ei);
    m_eiNotifier = new QSocketNotifier(ei_fd, QSocketNotifier::Read, this);
    connect(m_eiNotifier, &QSocketNotifier::activated, this, &PortalTextInput::handleEiEvents);

    qCDebug(TextInputLog) << "PortalTextInput: libei connected, fd:" << ei_fd;
    return true;
}

void PortalTextInput::handleEiEvents()
{
    if (!m_ei) {
        return;
    }

    ei_dispatch(m_ei);

    struct ei_event *event;
    while ((event = ei_get_event(m_ei))) {
        enum ei_event_type type = ei_event_get_type(event);

        switch (type) {
        case EI_EVENT_CONNECT:
            qCDebug(TextInputLog) << "PortalTextInput: EI connected";
            break;

        case EI_EVENT_DISCONNECT:
            qCWarning(TextInputLog) << "PortalTextInput: EI disconnected";
            m_deviceReady = false;
            break;

        case EI_EVENT_SEAT_ADDED: {
            m_seat = ei_event_get_seat(event);
            ei_seat_ref(m_seat);
            qCDebug(TextInputLog) << "PortalTextInput: Seat added, binding keyboard capability";
            ei_seat_bind_capabilities(m_seat, EI_DEVICE_CAP_KEYBOARD, nullptr);
            break;
        }

        case EI_EVENT_DEVICE_ADDED: {
            struct ei_device *device = ei_event_get_device(event);
            if (ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD)) {
                m_device = device;
                ei_device_ref(m_device);
                qCDebug(TextInputLog) << "PortalTextInput: Keyboard device added";
            }
            break;
        }

        case EI_EVENT_DEVICE_RESUMED:
            if (ei_event_get_device(event) == m_device) {
                qCDebug(TextInputLog) << "PortalTextInput: Device resumed - ready to send events";
                m_deviceReady = true;
            }
            break;

        case EI_EVENT_DEVICE_PAUSED:
            if (ei_event_get_device(event) == m_device) {
                qCDebug(TextInputLog) << "PortalTextInput: Device paused";
                m_deviceReady = false;
            }
            break;

        case EI_EVENT_DEVICE_REMOVED:
            if (ei_event_get_device(event) == m_device) {
                qCDebug(TextInputLog) << "PortalTextInput: Device removed";
                ei_device_unref(m_device);
                m_device = nullptr;
                m_deviceReady = false;
            }
            break;

        default:
            break;
        }

        ei_event_unref(event);
    }
}

bool PortalTextInput::typeText(const QString &text)
{
    qCDebug(TextInputLog) << "PortalTextInput: typeText() called with text length:" << text.length();
    qCDebug(TextInputLog) << "PortalTextInput: Current state - portalConnected:" << m_portalConnected
             << "deviceReady:" << m_deviceReady;

    // Reset permission state flags
    m_waitingForPermission = false;
    m_permissionRejected = false;

    // Lazy initialization - create session only when needed
    if (!m_portalConnected) {
        qCDebug(TextInputLog) << "PortalTextInput: Portal not connected, creating RemoteDesktop session...";
        qCDebug(TextInputLog) << "PortalTextInput: A system dialog should appear asking for permission!";

        if (!m_oeffis) {
            qCWarning(TextInputLog) << "PortalTextInput: oeffis context is NULL!";
            return false;
        }

        oeffis_create_session(m_oeffis, OEFFIS_DEVICE_KEYBOARD);
        qCDebug(TextInputLog) << "PortalTextInput: oeffis_create_session() called, waiting for connection...";

        // Use QEventLoop with QTimer for proper event processing
        // QSocketNotifier (used by oeffis) only works with Qt event loop
        QEventLoop loop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);

        // Connect success/failure signals to quit the event loop
        auto connectionSuccess = connect(this, &PortalTextInput::destroyed, &loop, &QEventLoop::quit);

        // Setup timeout
        int timeout = 60000; // 60 seconds
        connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeoutTimer.start(timeout);

        // Log progress every second
        QTimer progressTimer;
        int elapsed = 0;
        connect(&progressTimer, &QTimer::timeout, this, [this, &elapsed]() {
            elapsed += 1000;
            qCDebug(TextInputLog) << "PortalTextInput: Still waiting for portal connection... waited:" << elapsed << "ms";

            // Check if connected or rejected
            if (m_portalConnected || m_permissionRejected || !m_oeffis) {
                return; // Will exit loop on next iteration
            }
        });
        progressTimer.start(1000);

        // Wait for connection with proper event loop
        while (!m_portalConnected && !m_permissionRejected && m_oeffis && timeoutTimer.isActive()) {
            loop.processEvents(QEventLoop::WaitForMoreEvents, 100);
        }

        // Stop timers
        timeoutTimer.stop();
        progressTimer.stop();
        disconnect(connectionSuccess);

        // Use elapsed time from progress timer (more accurate than remainingTime())
        int totalWaited = elapsed;

        // Check if permission was rejected
        if (m_permissionRejected || !m_oeffis) {
            qCWarning(TextInputLog) << "PortalTextInput: Permission rejected by user";
            m_permissionRejected = true;
            return false;
        }

        if (!m_portalConnected) {
            qCWarning(TextInputLog) << "PortalTextInput: TIMEOUT waiting for portal connection after" << totalWaited << "ms";
            qCWarning(TextInputLog) << "PortalTextInput: Did you approve the RemoteDesktop permission dialog?";
            m_waitingForPermission = true;  // Mark that we're waiting for permission
            return false;
        }

        qCDebug(TextInputLog) << "PortalTextInput: Portal connected after" << totalWaited << "ms";
    }

    // Wait for device to be ready using event loop
    if (!m_deviceReady) {
        qCDebug(TextInputLog) << "PortalTextInput: Device not ready, waiting...";

        QEventLoop loop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);

        int timeout = 30000; // 30 seconds - EI protocol needs time for seat/device setup
        connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeoutTimer.start(timeout);

        // Log progress every 500ms
        QTimer progressTimer;
        int elapsed = 0;
        connect(&progressTimer, &QTimer::timeout, this, [this, &elapsed]() {
            elapsed += 500;
            if (elapsed % 500 == 0) {
                qCDebug(TextInputLog) << "PortalTextInput: Still waiting for device... waited:" << elapsed << "ms";
            }
            if (m_deviceReady) {
                return; // Will exit loop on next iteration
            }
        });
        progressTimer.start(500);

        // Wait with proper event processing
        while (!m_deviceReady && timeoutTimer.isActive()) {
            loop.processEvents(QEventLoop::WaitForMoreEvents, 100);
        }

        timeoutTimer.stop();
        progressTimer.stop();

        if (!m_deviceReady) {
            qCWarning(TextInputLog) << "PortalTextInput: TIMEOUT - Device not ready after" << timeout << "ms";
            return false;
        }

        qCDebug(TextInputLog) << "PortalTextInput: Device ready after" << elapsed << "ms";
    }

    qCDebug(TextInputLog) << "PortalTextInput: Sending key events...";
    return sendKeyEvents(text);
}

bool PortalTextInput::sendKeyEvents(const QString &text)
{
    if (!m_device || !m_deviceReady) {
        qCWarning(TextInputLog) << "PortalTextInput: Device not ready for sending events";
        return false;
    }

    // Start emulating sequence
    ei_device_start_emulating(m_device, ++m_sequence);

    bool success = true;
    for (const QChar &ch : text) {
        uint32_t keycode;
        bool needShift;

        if (!convertCharToKeycode(ch, keycode, needShift)) {
            qCWarning(TextInputLog) << "PortalTextInput: Cannot convert character:" << ch;
            success = false;
            continue;
        }

        uint64_t timestamp = ei_now(m_ei);

        // Press Shift if needed
        if (needShift) {
            ei_device_keyboard_key(m_device, KEY_LEFTSHIFT, true);
        }

        // Press key
        ei_device_keyboard_key(m_device, keycode, true);
        ei_device_frame(m_device, timestamp);

        // Release key
        ei_device_keyboard_key(m_device, keycode, false);
        ei_device_frame(m_device, timestamp);

        // Release Shift if needed
        if (needShift) {
            ei_device_keyboard_key(m_device, KEY_LEFTSHIFT, false);
            ei_device_frame(m_device, timestamp);
        }
    }

    // Stop emulating
    ei_device_stop_emulating(m_device);

    qCDebug(TextInputLog) << "PortalTextInput: Successfully sent" << text.length() << "characters";
    return success;
}

bool PortalTextInput::convertCharToKeycode(QChar ch, uint32_t &keycode, bool &needShift)
{
    needShift = false;
    char16_t unicode = ch.unicode();

    // Simple ASCII mapping to evdev keycodes
    // Based on US keyboard layout
    if (unicode >= '0' && unicode <= '9') {
        keycode = KEY_1 + (unicode - '1');
        if (unicode == '0') keycode = KEY_0;
        return true;
    }

    if (unicode >= 'a' && unicode <= 'z') {
        keycode = KEY_A + (unicode - 'a');
        return true;
    }

    if (unicode >= 'A' && unicode <= 'Z') {
        keycode = KEY_A + (unicode - 'A');
        needShift = true;
        return true;
    }

    // Special characters
    switch (ch.unicode()) {
    case ' ':  keycode = KEY_SPACE; break;
    case '-':  keycode = KEY_MINUS; break;
    case '=':  keycode = KEY_EQUAL; break;
    case '[':  keycode = KEY_LEFTBRACE; break;
    case ']':  keycode = KEY_RIGHTBRACE; break;
    case ';':  keycode = KEY_SEMICOLON; break;
    case '\'': keycode = KEY_APOSTROPHE; break;
    case '`':  keycode = KEY_GRAVE; break;
    case '\\': keycode = KEY_BACKSLASH; break;
    case ',':  keycode = KEY_COMMA; break;
    case '.':  keycode = KEY_DOT; break;
    case '/':  keycode = KEY_SLASH; break;
    case '\n': keycode = KEY_ENTER; break;
    case '\t': keycode = KEY_TAB; break;

    // Shifted special characters
    case '!':  keycode = KEY_1; needShift = true; break;
    case '@':  keycode = KEY_2; needShift = true; break;
    case '#':  keycode = KEY_3; needShift = true; break;
    case '$':  keycode = KEY_4; needShift = true; break;
    case '%':  keycode = KEY_5; needShift = true; break;
    case '^':  keycode = KEY_6; needShift = true; break;
    case '&':  keycode = KEY_7; needShift = true; break;
    case '*':  keycode = KEY_8; needShift = true; break;
    case '(':  keycode = KEY_9; needShift = true; break;
    case ')':  keycode = KEY_0; needShift = true; break;
    case '_':  keycode = KEY_MINUS; needShift = true; break;
    case '+':  keycode = KEY_EQUAL; needShift = true; break;
    case '{':  keycode = KEY_LEFTBRACE; needShift = true; break;
    case '}':  keycode = KEY_RIGHTBRACE; needShift = true; break;
    case ':':  keycode = KEY_SEMICOLON; needShift = true; break;
    case '"':  keycode = KEY_APOSTROPHE; needShift = true; break;
    case '~':  keycode = KEY_GRAVE; needShift = true; break;
    case '|':  keycode = KEY_BACKSLASH; needShift = true; break;
    case '<':  keycode = KEY_COMMA; needShift = true; break;
    case '>':  keycode = KEY_DOT; needShift = true; break;
    case '?':  keycode = KEY_SLASH; needShift = true; break;

    default:
        qCWarning(TextInputLog) << "PortalTextInput: Unsupported character:" << ch;
        return false;
    }

    return true;
}

bool PortalTextInput::isCompatible() const
{
    // Works on all Wayland compositors that support xdg-desktop-portal
    return QGuiApplication::platformName() == QStringLiteral("wayland");
}

QString PortalTextInput::providerName() const
{
    return QStringLiteral("Wayland (xdg-desktop-portal)");
}

} // namespace YubiKey
} // namespace KRunner
