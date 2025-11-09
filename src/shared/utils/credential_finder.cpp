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
    for (const auto &cred : credentials) {
        if (cred.originalName == credentialName && cred.deviceId == deviceId) {
            return cred;
        }
    }
    return std::nullopt;
}

} // namespace Utils
} // namespace YubiKeyOath
