/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "credential_finder.h"

namespace YubiKeyOath {
namespace Utils {
using namespace YubiKeyOath::Shared;

std::optional<OathCredential> findCredential(
    const QList<OathCredential> &credentials,
    const QString &credentialName,
    const QString &deviceId)
{
    // Use rich domain model method instead of accessing data directly (Tell, Don't Ask)
    for (const auto &cred : credentials) {
        if (cred.matches(credentialName, deviceId)) {
            return cred;
        }
    }
    return std::nullopt;
}

} // namespace Utils
} // namespace YubiKeyOath
