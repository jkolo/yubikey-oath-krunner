/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QDBusArgument>
#include <QMetaType>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Device lifecycle states
 *
 * Represents the current state of an OATH device throughout its lifecycle.
 * Enables async initialization and provides UI feedback during long operations.
 *
 * State transitions:
 * - Disconnected → Connecting → Authenticating → FetchingCredentials → Ready
 * - Any state → Error (on failure)
 * - Ready → Disconnected (on removal)
 */
enum class DeviceState : uint8_t {
    Disconnected = 0x00,        ///< Device physically disconnected or not initialized
    Connecting = 0x01,          ///< Establishing PC/SC connection (SCardConnect)
    Authenticating = 0x02,      ///< Loading password from KWallet or authenticating
    FetchingCredentials = 0x03, ///< Fetching credentials via CALCULATE_ALL
    Ready = 0x04,               ///< Fully initialized and ready for operations
    Error = 0xFF                ///< Initialization or operation failed
};

/**
 * @brief Converts device state to string representation
 *
 * Returns programmatic string for D-Bus serialization and logging.
 *
 * @param state Device state
 * @return String representation ("disconnected", "connecting", "authenticating",
 *         "fetching_credentials", "ready", "error")
 */
QString deviceStateToString(DeviceState state);

/**
 * @brief Parses device state from string
 *
 * Case-insensitive parsing for D-Bus deserialization.
 *
 * @param stateStr String representation
 * @return Parsed device state, DeviceState::Disconnected if invalid
 */
DeviceState deviceStateFromString(const QString& stateStr);

/**
 * @brief Gets localized human-readable state name
 *
 * Used for UI display (KRunner, KCM, notifications).
 *
 * @param state Device state
 * @return Localized state name (e.g., "Connecting...", "Ready", "Error")
 */
QString deviceStateName(DeviceState state);

/**
 * @brief Checks if device is in transitional state
 *
 * Transitional states indicate ongoing async operations.
 *
 * @param state Device state
 * @return true if Connecting, Authenticating, or FetchingCredentials
 */
[[nodiscard]] bool isDeviceStateTransitional(DeviceState state);

/**
 * @brief Checks if device is usable for operations
 *
 * Only Ready state allows generating codes, adding credentials, etc.
 *
 * @param state Device state
 * @return true if state == Ready
 */
[[nodiscard]] bool isDeviceStateReady(DeviceState state);

/**
 * @brief Checks if device should be visible in UI
 *
 * Disconnected devices are typically filtered out from lists.
 *
 * @param state Device state
 * @return true if state != Disconnected
 */
[[nodiscard]] bool isDeviceStateVisible(DeviceState state);

} // namespace Shared
} // namespace YubiKeyOath

// D-Bus marshaling operators (must be outside namespace)
QDBusArgument& operator<<(QDBusArgument& argument, YubiKeyOath::Shared::DeviceState state);
const QDBusArgument& operator>>(const QDBusArgument& argument, YubiKeyOath::Shared::DeviceState& state);

Q_DECLARE_METATYPE(YubiKeyOath::Shared::DeviceState)
