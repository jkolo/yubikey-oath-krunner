/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "yubikey_dbus_types.h"
#include "../types/oath_credential.h"

namespace KRunner {
namespace YubiKey {

/**
 * @brief Utility class for converting between internal types and D-Bus types
 *
 * This class provides static methods to convert between:
 * - Internal types used by daemon (OathCredential, DeviceRecord)
 * - D-Bus types used for inter-process communication (CredentialInfo, DeviceInfo)
 *
 * Single Responsibility: Type conversion/marshaling between layers
 *
 * @par Usage Example
 * @code
 * // Convert credential for D-Bus transfer
 * OathCredential credential = device->getCredential("Google");
 * CredentialInfo dbusInfo = TypeConversions::toCredentialInfo(credential);
 *
 * // Send via D-Bus
 * m_dbusInterface->call("UpdateCredential", QVariant::fromValue(dbusInfo));
 * @endcode
 */
class TypeConversions
{
public:
    /**
     * @brief Converts OathCredential to CredentialInfo for D-Bus transfer
     *
     * Maps internal credential representation to D-Bus-safe struct:
     * - name, issuer, username → copied directly
     * - requiresTouch → copied
     * - validUntil → timestamp when code expires (0 if touch required)
     * - deviceId → device identifier
     *
     * @param credential Internal OathCredential from device
     * @return CredentialInfo suitable for D-Bus marshaling
     */
    static CredentialInfo toCredentialInfo(const OathCredential &credential);

private:
    // Static utility class - no instances allowed
    TypeConversions() = delete;
    ~TypeConversions() = delete;
    TypeConversions(const TypeConversions&) = delete;
    TypeConversions& operator=(const TypeConversions&) = delete;
};

} // namespace YubiKey
} // namespace KRunner
