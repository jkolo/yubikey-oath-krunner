/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_state.h"

#include <KLocalizedString>

namespace YubiKeyOath {
namespace Shared {

QString deviceStateToString(DeviceState state)
{
    switch (state) {
    case DeviceState::Disconnected:
        return QStringLiteral("disconnected");
    case DeviceState::Connecting:
        return QStringLiteral("connecting");
    case DeviceState::Authenticating:
        return QStringLiteral("authenticating");
    case DeviceState::FetchingCredentials:
        return QStringLiteral("fetching_credentials");
    case DeviceState::Ready:
        return QStringLiteral("ready");
    case DeviceState::Error:
        return QStringLiteral("error");
    default:
        return QStringLiteral("disconnected");
    }
}

DeviceState deviceStateFromString(const QString& stateStr)
{
    const QString lower = stateStr.toLower();

    if (lower == QStringLiteral("disconnected")) {
        return DeviceState::Disconnected;
    }
    if (lower == QStringLiteral("connecting")) {
        return DeviceState::Connecting;
    }
    if (lower == QStringLiteral("authenticating")) {
        return DeviceState::Authenticating;
    }
    if (lower == QStringLiteral("fetching_credentials") ||
        lower == QStringLiteral("fetching")) {
        return DeviceState::FetchingCredentials;
    }
    if (lower == QStringLiteral("ready")) {
        return DeviceState::Ready;
    }
    if (lower == QStringLiteral("error")) {
        return DeviceState::Error;
    }

    // Default to Disconnected for unknown strings
    return DeviceState::Disconnected;
}

QString deviceStateName(DeviceState state)
{
    switch (state) {
    case DeviceState::Disconnected:
        return i18nc("@label Device state", "Disconnected");
    case DeviceState::Connecting:
        return i18nc("@label Device state - operation in progress", "Connecting...");
    case DeviceState::Authenticating:
        return i18nc("@label Device state - operation in progress", "Authenticating...");
    case DeviceState::FetchingCredentials:
        return i18nc("@label Device state - operation in progress", "Loading credentials...");
    case DeviceState::Ready:
        return i18nc("@label Device state", "Ready");
    case DeviceState::Error:
        return i18nc("@label Device state - error occurred", "Error");
    default:
        return i18nc("@label Device state unknown", "Unknown");
    }
}

bool isDeviceStateTransitional(DeviceState state)
{
    return state == DeviceState::Connecting ||
           state == DeviceState::Authenticating ||
           state == DeviceState::FetchingCredentials;
}

bool isDeviceStateReady(DeviceState state)
{
    return state == DeviceState::Ready;
}

bool isDeviceStateVisible(DeviceState state)
{
    return state != DeviceState::Disconnected;
}

} // namespace Shared
} // namespace YubiKeyOath

// D-Bus marshaling operators implementation
// DeviceState is marshaled as a simple byte (quint8) - D-Bus type 'y'
QDBusArgument& operator<<(QDBusArgument& argument, YubiKeyOath::Shared::DeviceState state)
{
    argument << static_cast<quint8>(state);
    return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, YubiKeyOath::Shared::DeviceState& state)
{
    quint8 value{};
    argument >> value;
    state = static_cast<YubiKeyOath::Shared::DeviceState>(value);
    return argument;
}
