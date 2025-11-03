/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "code_validator.h"

namespace YubiKeyOath {
namespace Daemon {

int CodeValidator::calculateCodeValidity()
{
    // TOTP codes are valid for 30 seconds
    // Calculate remaining seconds in current 30-second window
    qint64 const currentTime = QDateTime::currentSecsSinceEpoch();
    return static_cast<int>(TOTP_PERIOD - (currentTime % TOTP_PERIOD));
}

QDateTime CodeValidator::calculateExpirationTime(const QDateTime &currentTime)
{
    // Calculate remaining seconds based on provided currentTime, not actual current time
    qint64 const timeInSeconds = currentTime.toSecsSinceEpoch();
    int const remainingSeconds = static_cast<int>(TOTP_PERIOD - (timeInSeconds % TOTP_PERIOD));
    return currentTime.addSecs(remainingSeconds);
}

} // namespace Daemon
} // namespace YubiKeyOath
