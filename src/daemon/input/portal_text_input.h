/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "text_input_provider.h"
#include <QObject>
#include <QString>
#include <QVariant>
#include <memory>

// Forward declarations for libportal types
typedef struct _XdpPortal XdpPortal;
typedef struct _XdpSession XdpSession;

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class SecretStorage;

/**
 * @brief Modern Wayland text input using xdg-desktop-portal
 *
 * This implementation uses the RemoteDesktop portal via libportal for both session
 * management and keyboard emulation.
 *
 * Works across all Wayland compositors (KDE Plasma, GNOME, Sway, Hyprland, etc.)
 * that implement org.freedesktop.portal.RemoteDesktop interface.
 *
 * Architecture:
 * 1. libportal handles portal session lifecycle and permission dialogs
 * 2. libportal xdp_session_keyboard_key() API for keyboard events
 * 3. No external dependencies beyond Qt and libportal
 *
 * Replaces previous libei + liboeffis implementation with cleaner, simpler API.
 */
class PortalTextInput : public QObject, public TextInputProvider
{
    Q_OBJECT

public:
    explicit PortalTextInput(SecretStorage *secretStorage, QObject *parent = nullptr);
    ~PortalTextInput() override;

    bool typeText(const QString &text) override;
    bool isCompatible() const override;
    QString providerName() const override;

    /**
     * @brief Check if last typeText() failure was due to waiting for permission
     * @return true if permission dialog timeout occurred
     */
    bool isWaitingForPermission() const override { return m_waitingForPermission; }

    /**
     * @brief Check if user explicitly rejected permission
     * @return true if permission was rejected (not just timeout)
     */
    bool wasPermissionRejected() const override { return m_permissionRejected; }

private:
    bool initializePortal();
    bool createSession();
    void cleanup();

    bool sendKeyEvents(const QString &text);
    bool sendKeycode(uint32_t keycode, bool pressed);
    bool convertCharToKeycode(QChar ch, uint32_t &keycode, bool &needShift);

    // libportal session management
    XdpPortal *m_portal = nullptr;
    XdpSession *m_session = nullptr;
    bool m_sessionReady = false;
    SecretStorage *m_secretStorage = nullptr;  // For loading/saving restore token

    // Permission state tracking
    bool m_waitingForPermission = false;
    bool m_permissionRejected = false;

    // Keystroke timing (ms delay between key press/release)
    static constexpr int KEY_DELAY_MS = 5;
};

} // namespace Daemon
} // namespace YubiKeyOath
