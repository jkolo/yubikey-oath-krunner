/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "text_input_provider.h"
#include <QObject>
#include <QSocketNotifier>
#include <memory>

extern "C" {
#include <libei.h>
#include <liboeffis.h>
}

namespace KRunner {
namespace YubiKey {

/**
 * @brief Modern Wayland text input using xdg-desktop-portal + libei
 *
 * This implementation uses the RemoteDesktop portal with libei for keyboard
 * emulation. It's the recommended approach for Wayland and works across
 * all compositors (KDE Plasma, GNOME, Sway, Hyprland, etc.)
 *
 * Replaces the deprecated KWayland FakeInput protocol.
 */
class PortalTextInput : public TextInputProvider
{
    Q_OBJECT

public:
    explicit PortalTextInput(QObject *parent = nullptr);
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

private Q_SLOTS:
    void handleOeffisEvents();
    void handleEiEvents();

private:
    bool initializePortal();
    bool connectToEis();
    void cleanup();

    bool sendKeyEvents(const QString &text);
    bool convertCharToKeycode(QChar ch, uint32_t &keycode, bool &needShift);

    // oeffis (portal wrapper) state
    struct oeffis *m_oeffis = nullptr;
    QSocketNotifier *m_oeffisNotifier = nullptr;
    bool m_portalConnected = false;

    // libei (input emulation) state
    struct ei *m_ei = nullptr;
    struct ei_seat *m_seat = nullptr;
    struct ei_device *m_device = nullptr;
    QSocketNotifier *m_eiNotifier = nullptr;
    bool m_deviceReady = false;
    uint32_t m_sequence = 0;

    // Permission state tracking
    bool m_waitingForPermission = false;
    bool m_permissionRejected = false;
};

} // namespace YubiKey
} // namespace KRunner
