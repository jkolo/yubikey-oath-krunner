/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "type_conversions.h"

namespace KRunner {
namespace YubiKey {

CredentialInfo TypeConversions::toCredentialInfo(const OathCredential &credential)
{
    CredentialInfo info;
    info.name = credential.name;
    info.issuer = credential.issuer;
    info.username = credential.username;
    info.requiresTouch = credential.requiresTouch;

    // Use validUntil timestamp from credential
    // If credential has been generated, it will have a validUntil timestamp
    // Otherwise it will be 0
    info.validUntil = credential.validUntil;

    // Device ID - required for multi-device support
    info.deviceId = credential.deviceId;

    return info;
}

} // namespace YubiKey
} // namespace KRunner
