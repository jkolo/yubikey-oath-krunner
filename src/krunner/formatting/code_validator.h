/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Validates and calculates TOTP code timing
 *
 * Single Responsibility: TOTP code validity calculations
 */
class CodeValidator
{
public:
    /**
     * @brief Calculates remaining TOTP code validity time
     * @return Remaining seconds until code expires
     */
    static int calculateCodeValidity();

    /**
     * @brief Calculates when code will expire
     * @param currentTime Current time
     * @return Expiration timestamp
     */
    static QDateTime calculateExpirationTime(const QDateTime &currentTime = QDateTime::currentDateTime());

private:
    static constexpr int TOTP_PERIOD = 30; ///< TOTP period in seconds
};

} // namespace YubiKey
} // namespace KRunner
