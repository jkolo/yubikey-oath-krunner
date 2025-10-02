/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "wayland_text_input.h"
#include "../logging_categories.h"

#include <QGuiApplication>
#include <QProcess>
#include <QDebug>

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/fakeinput.h>

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

namespace KRunner {
namespace YubiKey {

WaylandTextInput::WaylandTextInput(QObject *parent)
    : TextInputProvider(parent)
{
    initializeWayland();
}

WaylandTextInput::~WaylandTextInput()
{
    if (m_fakeInput) {
        m_fakeInput->release();
        m_fakeInput->deleteLater();
    }
    if (m_registry) {
        m_registry->release();
        m_registry->deleteLater();
    }
    if (m_connection) {
        m_connection->deleteLater();
    }
}

void WaylandTextInput::initializeWayland()
{
    if (m_waylandInitialized) {
        return;
    }

    qCDebug(TextInputLog) << "WaylandTextInput: Initializing KWayland connection";

    m_connection = KWayland::Client::ConnectionThread::fromApplication(this);
    if (!m_connection) {
        qCWarning(TextInputLog) << "WaylandTextInput: Failed to get Wayland connection from application";
        return;
    }

    m_registry = new KWayland::Client::Registry(this);
    m_registry->create(m_connection);

    connect(m_registry, &KWayland::Client::Registry::interfacesAnnounced, this, &WaylandTextInput::setupRegistry);

    m_registry->setup();
    m_connection->roundtrip();

    m_waylandInitialized = true;
}

void WaylandTextInput::setupRegistry()
{
    const auto fakeInterface = m_registry->interface(KWayland::Client::Registry::Interface::FakeInput);
    if (fakeInterface.name == 0) {
        qCWarning(TextInputLog) << "WaylandTextInput: FakeInput interface not available";
        return;
    }

    m_fakeInput = m_registry->createFakeInput(fakeInterface.name, fakeInterface.version, this);
    if (!m_fakeInput) {
        qCWarning(TextInputLog) << "WaylandTextInput: Failed to create FakeInput";
        return;
    }

    // Authenticate with compositor
    m_fakeInput->authenticate(QStringLiteral("KRunner YubiKey Plugin"),
                               QStringLiteral("Type OATH codes from YubiKey"));

    qCDebug(TextInputLog) << "WaylandTextInput: FakeInput initialized and authenticated";
}

bool WaylandTextInput::typeText(const QString &text)
{
    qCDebug(TextInputLog) << "WaylandTextInput: Typing text, length:" << text.length();

    // Try native KWayland FakeInput first
    if (m_fakeInput && m_fakeInput->isValid()) {
        if (typeTextWithFakeInput(text)) {
            qCDebug(TextInputLog) << "WaylandTextInput: Text typed successfully using KWayland FakeInput";
            return true;
        }
        qCWarning(TextInputLog) << "WaylandTextInput: FakeInput typing failed, falling back to external tools";
    } else {
        qCDebug(TextInputLog) << "WaylandTextInput: FakeInput not available, using external tools";
    }

    // Fallback to external tools
    return typeTextWithExternalTools(text);
}

bool WaylandTextInput::typeTextWithFakeInput(const QString &text)
{
    if (!m_fakeInput || !m_fakeInput->isValid()) {
        return false;
    }

    // Use xkbcommon for proper character to keycode conversion
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) {
        qCWarning(TextInputLog) << "WaylandTextInput: Failed to create XKB context";
        return false;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        qCWarning(TextInputLog) << "WaylandTextInput: Failed to create XKB keymap";
        xkb_context_unref(ctx);
        return false;
    }

    struct xkb_state *state = xkb_state_new(keymap);
    if (!state) {
        qCWarning(TextInputLog) << "WaylandTextInput: Failed to create XKB state";
        xkb_keymap_unref(keymap);
        xkb_context_unref(ctx);
        return false;
    }

    bool success = true;
    for (const QChar &ch : text) {
        xkb_keysym_t keysym = xkb_utf32_to_keysym(ch.unicode());

        if (keysym == XKB_KEY_NoSymbol) {
            qCWarning(TextInputLog) << "WaylandTextInput: Cannot convert character to keysym:" << ch;
            success = false;
            continue;
        }

        // Find the keycode for this keysym
        xkb_keycode_t keycode = XKB_KEYCODE_INVALID;
        xkb_keycode_t min_keycode = xkb_keymap_min_keycode(keymap);
        xkb_keycode_t max_keycode = xkb_keymap_max_keycode(keymap);

        for (xkb_keycode_t kc = min_keycode; kc <= max_keycode; kc++) {
            xkb_keysym_t ks = xkb_state_key_get_one_sym(state, kc);
            if (ks == keysym) {
                keycode = kc;
                break;
            }
        }

        if (keycode == XKB_KEYCODE_INVALID) {
            qCWarning(TextInputLog) << "WaylandTextInput: Cannot find keycode for character:" << ch;
            success = false;
            continue;
        }

        // Convert XKB keycode to Linux keycode (subtract 8)
        quint32 linux_keycode = keycode - 8;

        // Press and release the key
        m_fakeInput->requestKeyboardKeyPress(linux_keycode);
        m_fakeInput->requestKeyboardKeyRelease(linux_keycode);
    }

    xkb_state_unref(state);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    return success;
}

bool WaylandTextInput::typeTextWithExternalTools(const QString &text)
{
    // Try wtype first (more common)
    QProcess process;
    process.start(QStringLiteral("wtype"), {text});
    if (process.waitForFinished(5000) && process.exitCode() == 0) {
        qCDebug(TextInputLog) << "WaylandTextInput: Text typed successfully using wtype";
        return true;
    }

    qCDebug(TextInputLog) << "WaylandTextInput: wtype failed, trying ydotool";

    // Fallback to ydotool
    process.start(QStringLiteral("ydotool"), {QStringLiteral("type"), text});
    if (!process.waitForFinished(5000) || process.exitCode() != 0) {
        qCWarning(TextInputLog) << "WaylandTextInput: Both wtype and ydotool failed";
        return false;
    }

    qCDebug(TextInputLog) << "WaylandTextInput: Text typed successfully using ydotool";
    return true;
}

bool WaylandTextInput::isCompatible() const
{
    return QGuiApplication::platformName() == QStringLiteral("wayland");
}

QString WaylandTextInput::providerName() const
{
    return QStringLiteral("Wayland");
}

} // namespace YubiKey
} // namespace KRunner
