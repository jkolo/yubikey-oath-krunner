/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QList>
#include <QString>
#include <optional>
#include "types/oath_credential.h"

namespace YubiKeyOath {
namespace Utils {
using namespace YubiKeyOath::Shared;

/**
 * @brief Finds a credential in a list by name and device ID
 *
 * Searches through a list of credentials to find one matching
 * both the credential name and device ID.
 *
 * @param credentials List of credentials to search
 * @param credentialName Name of the credential to find
 * @param deviceId Device ID to match
 * @return std::optional<OathCredential> Found credential or std::nullopt if not found
 */
std::optional<OathCredential> findCredential(
    const QList<OathCredential> &credentials,
    const QString &credentialName,
    const QString &deviceId);

} // namespace Utils
} // namespace YubiKeyOath
