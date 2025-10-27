/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "text_input_provider.h"
#include <QObject>

// Forward declarations for KDE Wayland classes (must be outside namespace)
namespace KWayland
{
namespace Client
{
class ConnectionThread;
class Registry;
class FakeInput;
}
}

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Wayland-specific text input implementation using KWayland FakeInput
 */
class WaylandTextInput : public TextInputProvider
{
    Q_OBJECT

public:
    explicit WaylandTextInput(QObject *parent = nullptr);
    ~WaylandTextInput() override;

    bool typeText(const QString &text) override;
    bool isCompatible() const override;
    QString providerName() const override;

private Q_SLOTS:
    void setupRegistry();

private:
    bool typeTextWithFakeInput(const QString &text);
    bool typeTextWithExternalTools(const QString &text);
    void initializeWayland();

    KWayland::Client::ConnectionThread *m_connection = nullptr;
    KWayland::Client::Registry *m_registry = nullptr;
    KWayland::Client::FakeInput *m_fakeInput = nullptr;
    bool m_waylandInitialized = false;
};

} // namespace Daemon
} // namespace YubiKeyOath
