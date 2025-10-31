/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "portal_text_input.h"
#include "../storage/secret_storage.h"
#include "../logging_categories.h"

#include <QDebug>
#include <QDateTime>
#include <QGuiApplication>
#include <QThread>
#include <QEventLoop>
#include <QTimer>

#include <libportal/portal.h>
#include <linux/input-event-codes.h>

namespace YubiKeyOath {
namespace Daemon {

// =============================================================================
// Constructor / Destructor
// =============================================================================

PortalTextInput::PortalTextInput(SecretStorage *secretStorage, QObject *parent)
    : TextInputProvider(parent)
    , m_secretStorage(secretStorage)
{
    qCDebug(TextInputLog) << "PortalTextInput: Constructor";
}

PortalTextInput::~PortalTextInput()
{
    qCDebug(TextInputLog) << "PortalTextInput: Destructor";
    cleanup();
}

// =============================================================================
// TextInputProvider Interface
// =============================================================================

bool PortalTextInput::typeText(const QString &text)
{
    qCDebug(TextInputLog) << "PortalTextInput: typeText() called with" << text.length() << "characters";

    if (text.isEmpty()) {
        qCWarning(TextInputLog) << "PortalTextInput: Empty text provided";
        return false;
    }

    // Reset permission state
    m_waitingForPermission = false;
    m_permissionRejected = false;

    // Initialize portal if needed
    if (!m_portal) {
        if (!initializePortal()) {
            qCWarning(TextInputLog) << "PortalTextInput: Failed to initialize portal";
            return false;
        }
    }

    // Create session if needed
    if (!m_sessionReady) {
        if (!createSession()) {
            qCWarning(TextInputLog) << "PortalTextInput: Failed to create portal session";
            return false;
        }
    }

    // Send key events
    return sendKeyEvents(text);
}

bool PortalTextInput::isCompatible() const
{
    // Only works on Wayland
    const QString platformName = QGuiApplication::platformName();
    const bool isWayland = platformName == QStringLiteral("wayland");

    qCDebug(TextInputLog) << "PortalTextInput: isCompatible() - platform:" << platformName
                           << "compatible:" << isWayland;

    return isWayland;
}

QString PortalTextInput::providerName() const
{
    return QStringLiteral("Portal (libportal RemoteDesktop)");
}

// =============================================================================
// Portal Initialization
// =============================================================================

bool PortalTextInput::initializePortal()
{
    qCDebug(TextInputLog) << "PortalTextInput: Initializing xdg-desktop-portal connection";

    // Create portal instance (GLib-based, but works with Qt)
    m_portal = xdp_portal_new();
    if (!m_portal) {
        qCWarning(TextInputLog) << "PortalTextInput: Failed to create XdpPortal";
        return false;
    }

    qCDebug(TextInputLog) << "PortalTextInput: Portal initialized successfully";
    return true;
}

// =============================================================================
// Session Management
// =============================================================================

bool PortalTextInput::createSession()
{
    qCDebug(TextInputLog) << "PortalTextInput: Creating RemoteDesktop portal session";

    if (!m_portal) {
        qCWarning(TextInputLog) << "PortalTextInput: Portal not initialized";
        return false;
    }

    // Prepare flags for RemoteDesktop session (keyboard only)
    XdpDeviceType devices = XDP_DEVICE_KEYBOARD;
    XdpOutputType outputs = XDP_OUTPUT_NONE;  // No screen sharing needed
    XdpRemoteDesktopFlags flags = XDP_REMOTE_DESKTOP_FLAG_NONE;
    XdpCursorMode cursor_mode = XDP_CURSOR_MODE_HIDDEN;  // Hide cursor (not needed for keyboard)
    XdpPersistMode persist_mode = XDP_PERSIST_MODE_PERSISTENT;  // Save permission permanently

    // Set up async callback variables
    bool sessionCreated = false;
    QString errorMessage;
    QEventLoop loop;

    // Lambda callback for session creation (C callback wrapper)
    auto callback = [](GObject *source_object, GAsyncResult *result, gpointer user_data) {
        auto *data = static_cast<std::tuple<bool*, QString*, QEventLoop*>*>(user_data);
        auto &[success, error, eventLoop] = *data;

        GError *g_error = nullptr;
        XdpSession *session = xdp_portal_create_remote_desktop_session_finish(
            XDP_PORTAL(source_object), result, &g_error);

        if (session) {
            qCDebug(TextInputLog) << "PortalTextInput: Session created successfully";
            *success = true;

            // Store session globally (will be retrieved later)
            // Note: This is a simplified approach - in production code you'd want
            // to pass the PortalTextInput instance through user_data
            g_object_set_data(G_OBJECT(source_object), "yubikey_session", session);
        } else {
            qCWarning(TextInputLog) << "PortalTextInput: Session creation failed:"
                                     << (g_error ? g_error->message : "Unknown error");
            *error = g_error ? QString::fromUtf8(g_error->message) : QStringLiteral("Unknown error");
            *success = false;

            if (g_error) {
                g_error_free(g_error);
            }
        }

        eventLoop->quit();
        delete data;
    };

    // Call async session creation
    auto *callbackData = new std::tuple<bool*, QString*, QEventLoop*>(&sessionCreated, &errorMessage, &loop);

    // Load restore token from KWallet (via SecretStorage)
    QString restoreToken;
    if (m_secretStorage) {
        restoreToken = m_secretStorage->loadRestoreToken();
        if (!restoreToken.isEmpty()) {
            qCDebug(TextInputLog) << "PortalTextInput: Loaded restore token from KWallet";
        }
    }

    // Prepare restore token (convert QString to const char*)
    QByteArray tokenBytes = restoreToken.toUtf8();
    const char* restore_token = restoreToken.isEmpty() ? nullptr : tokenBytes.constData();

    if (restore_token) {
        qCDebug(TextInputLog) << "PortalTextInput: Using restore token to skip permission dialog";
    } else {
        qCDebug(TextInputLog) << "PortalTextInput: No restore token - first time setup, permission dialog will appear";
    }

    xdp_portal_create_remote_desktop_session_full(
        m_portal,
        devices,
        outputs,
        flags,
        cursor_mode,
        persist_mode,
        restore_token,
        nullptr,  // cancellable
        callback,
        callbackData
    );

    // Wait for callback with timeout
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);  // 30s timeout
    loop.exec();

    if (!sessionCreated) {
        qCWarning(TextInputLog) << "PortalTextInput: Session creation failed or timed out:" << errorMessage;

        if (errorMessage.contains(QStringLiteral("denied")) || errorMessage.contains(QStringLiteral("cancelled"))) {
            m_permissionRejected = true;
        } else {
            m_waitingForPermission = true;
        }

        return false;
    }

    // Retrieve session from GObject data (temporary storage)
    m_session = static_cast<XdpSession*>(g_object_get_data(G_OBJECT(m_portal), "yubikey_session"));
    if (!m_session) {
        qCWarning(TextInputLog) << "PortalTextInput: Failed to retrieve session";
        return false;
    }

    qCDebug(TextInputLog) << "PortalTextInput: Session retrieved successfully";

    // Start the session to activate it for keyboard emulation
    bool sessionStarted = false;
    QString startErrorMessage;
    QEventLoop startLoop;

    // Lambda callback for session start (C callback wrapper)
    auto startCallback = [](GObject *source_object, GAsyncResult *result, gpointer user_data) {
        auto *data = static_cast<std::tuple<bool*, QString*, QEventLoop*>*>(user_data);
        auto &[success, error, eventLoop] = *data;

        GError *g_error = nullptr;
        gboolean started = xdp_session_start_finish(XDP_SESSION(source_object), result, &g_error);

        if (started) {
            qCDebug(TextInputLog) << "PortalTextInput: Session started successfully";
            *success = true;
        } else {
            qCWarning(TextInputLog) << "PortalTextInput: Session start failed:"
                                     << (g_error ? g_error->message : "Unknown error");
            *error = g_error ? QString::fromUtf8(g_error->message) : QStringLiteral("Unknown error");
            *success = false;

            if (g_error) {
                g_error_free(g_error);
            }
        }

        eventLoop->quit();
        delete data;
    };

    // Call async session start
    auto *startCallbackData = new std::tuple<bool*, QString*, QEventLoop*>(&sessionStarted, &startErrorMessage, &startLoop);

    xdp_session_start(
        m_session,
        nullptr,  // parent window
        nullptr,  // cancellable
        startCallback,
        startCallbackData
    );

    // Wait for callback with timeout
    QTimer::singleShot(30000, &startLoop, &QEventLoop::quit);  // 30s timeout
    startLoop.exec();

    if (!sessionStarted) {
        qCWarning(TextInputLog) << "PortalTextInput: Session start failed or timed out:" << startErrorMessage;

        if (startErrorMessage.contains(QStringLiteral("denied")) || startErrorMessage.contains(QStringLiteral("cancelled"))) {
            m_permissionRejected = true;
        } else {
            m_waitingForPermission = true;
        }

        return false;
    }

    // Get restore token for future sessions (to skip permission dialog)
    char *token = xdp_session_get_restore_token(m_session);
    if (token) {
        QString newToken = QString::fromUtf8(token);
        g_free(token);  // Free GLib-allocated string

        // Save to KWallet for persistence across daemon restarts
        if (m_secretStorage) {
            if (m_secretStorage->saveRestoreToken(newToken)) {
                qCDebug(TextInputLog) << "PortalTextInput: Restore token saved to KWallet for future sessions";
            } else {
                qCWarning(TextInputLog) << "PortalTextInput: Failed to save restore token to KWallet";
            }
        } else {
            qCWarning(TextInputLog) << "PortalTextInput: No SecretStorage available, token won't persist across restarts";
        }
    } else {
        qCDebug(TextInputLog) << "PortalTextInput: No restore token available (may be using existing token)";
    }

    m_sessionReady = true;
    qCDebug(TextInputLog) << "PortalTextInput: Session ready for keyboard emulation";
    return true;
}

// =============================================================================
// Keyboard Emulation via libportal API
// =============================================================================

bool PortalTextInput::sendKeycode(uint32_t keycode, bool pressed)
{
    if (!m_session || !m_sessionReady) {
        qCWarning(TextInputLog) << "PortalTextInput: Session not ready for keycode" << keycode;
        return false;
    }

    // Use libportal's keyboard emulation API
    // Parameters:
    // - session: XdpSession pointer
    // - keysym: FALSE = evdev keycode (not X11 keysym)
    // - key: evdev keycode value
    // - state: XDP_KEY_PRESSED (1) or XDP_KEY_RELEASED (0)
    XdpKeyState state = pressed ? XDP_KEY_PRESSED : XDP_KEY_RELEASED;

    xdp_session_keyboard_key(m_session, FALSE, static_cast<int>(keycode), state);

    return true;
}

bool PortalTextInput::sendKeyEvents(const QString &text)
{
    qCDebug(TextInputLog) << "PortalTextInput: Sending" << text.length() << "characters via libportal";
    qCDebug(TextInputLog) << "PortalTextInput: [TIMING] Started at" << QDateTime::currentMSecsSinceEpoch();

    bool success = true;

    for (const QChar &ch : text) {
        uint32_t keycode = 0;
        bool needShift = false;

        if (!convertCharToKeycode(ch, keycode, needShift)) {
            qCWarning(TextInputLog) << "PortalTextInput: Failed to convert character:" << ch;
            success = false;
            continue;
        }

        // Press shift if needed
        if (needShift) {
            if (!sendKeycode(KEY_LEFTSHIFT, true)) {
                success = false;
                continue;
            }
            QThread::msleep(KEY_DELAY_MS);
        }

        // Press key
        if (!sendKeycode(keycode, true)) {
            success = false;
            if (needShift) {
                sendKeycode(KEY_LEFTSHIFT, false);  // Release shift even on error
            }
            continue;
        }
        QThread::msleep(KEY_DELAY_MS);

        // Release key
        if (!sendKeycode(keycode, false)) {
            success = false;
            if (needShift) {
                sendKeycode(KEY_LEFTSHIFT, false);
            }
            continue;
        }
        QThread::msleep(KEY_DELAY_MS);

        // Release shift if needed
        if (needShift) {
            if (!sendKeycode(KEY_LEFTSHIFT, false)) {
                success = false;
            }
            QThread::msleep(KEY_DELAY_MS);
        }
    }

    qCDebug(TextInputLog) << "PortalTextInput: [TIMING] Finished at" << QDateTime::currentMSecsSinceEpoch();
    qCDebug(TextInputLog) << "PortalTextInput: Sent" << text.length() << "characters, success:" << success;

    return success;
}

// =============================================================================
// Character to Keycode Conversion (US Keyboard Layout)
// =============================================================================

bool PortalTextInput::convertCharToKeycode(QChar ch, uint32_t &keycode, bool &needShift)
{
    needShift = false;
    char16_t const unicode = ch.unicode();

    // Numbers (0-9)
    if (unicode >= '0' && unicode <= '9') {
        keycode = KEY_1 + (unicode - '1');
        if (unicode == '0') {
            keycode = KEY_0;
        }
        return true;
    }

    // Lowercase letters (a-z)
    if (unicode >= 'a' && unicode <= 'z') {
        keycode = KEY_A + (unicode - 'a');
        return true;
    }

    // Uppercase letters (A-Z)
    if (unicode >= 'A' && unicode <= 'Z') {
        keycode = KEY_A + (unicode - 'A');
        needShift = true;
        return true;
    }

    // Special characters (unshifted)
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

// =============================================================================
// Cleanup
// =============================================================================

void PortalTextInput::cleanup()
{
    qCDebug(TextInputLog) << "PortalTextInput: Cleanup";

    m_sessionReady = false;

    if (m_session) {
        // Close session via portal
        xdp_session_close(m_session);
        m_session = nullptr;
    }

    if (m_portal) {
        g_object_unref(m_portal);
        m_portal = nullptr;
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
