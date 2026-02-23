/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef YUBIKEY_OATH_SECURE_LOGGING_H
#define YUBIKEY_OATH_SECURE_LOGGING_H

#include <QString>
#include <QByteArray>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Utility functions for secure logging without exposing sensitive data.
 *
 * SECURITY POLICY:
 * - NEVER log cryptographic keys, HMAC responses, or derived secrets
 * - NEVER log TOTP/HOTP codes in plaintext
 * - NEVER log complete APDU command/response bytes
 * - Mask serial numbers (show only last 4 digits)
 * - Use high-level descriptions instead of raw data
 */
namespace SecureLogging {

/**
 * @brief Returns a safe representation of byte array for logging.
 * Only shows length, never content.
 */
inline QString safeByteInfo(const QByteArray &data)
{
    return QStringLiteral("[%1 bytes]").arg(data.length());
}

/**
 * @brief Returns masked serial number (shows only last 4 digits).
 */
inline QString maskSerial(quint32 serial)
{
    if (serial == 0) {
        return QStringLiteral("(none)");
    }
    const QString serialStr = QString::number(serial);
    if (serialStr.length() <= 4) {
        return serialStr;
    }
    return QStringLiteral("****%1").arg(serialStr.right(4));
}

/**
 * @brief Returns masked serial number from string.
 */
inline QString maskSerial(const QString &serial)
{
    if (serial.isEmpty()) {
        return QStringLiteral("(none)");
    }
    if (serial.length() <= 4) {
        return serial;
    }
    return QStringLiteral("****%1").arg(serial.right(4));
}

/**
 * @brief Returns masked credential name (shows only issuer).
 */
inline QString maskCredentialName(const QString &name)
{
    if (name.isEmpty()) {
        return QStringLiteral("(empty)");
    }

    // Format is typically "issuer:account" or just "account"
    const int colonPos = name.indexOf(QLatin1Char(':'));
    if (colonPos > 0) {
        return name.left(colonPos) + QStringLiteral(":****");
    }

    // No issuer, mask the whole name
    if (name.length() <= 4) {
        return name;
    }
    return name.left(2) + QStringLiteral("****");
}

/**
 * @brief Returns APDU command description without raw bytes.
 * @param ins APDU instruction byte
 */
inline QString apduDescription(quint8 ins)
{
    switch (ins) {
    case 0xA4: return QStringLiteral("SELECT");
    case 0xA1: return QStringLiteral("LIST");
    case 0xA2: return QStringLiteral("CALCULATE");
    case 0xA4 + 0x10: return QStringLiteral("CALCULATE_ALL");  // 0xA4 with P1=0x00 is SELECT, with other is CALCULATE_ALL
    case 0xA5: return QStringLiteral("SEND_REMAINING");
    case 0x01: return QStringLiteral("PUT");
    case 0x02: return QStringLiteral("DELETE");
    case 0x03: return QStringLiteral("SET_CODE");
    case 0x04: return QStringLiteral("RESET");
    case 0xA3: return QStringLiteral("VALIDATE");
    default: return QStringLiteral("CMD_0x%1").arg(ins, 2, 16, QLatin1Char('0'));
    }
}

/**
 * @brief Returns safe APDU command info for logging.
 * Shows instruction type and length, never raw bytes.
 */
inline QString safeApduInfo(const QByteArray &command)
{
    if (command.length() < 4) {
        return QStringLiteral("[invalid APDU, %1 bytes]").arg(command.length());
    }

    const quint8 cla = static_cast<quint8>(command[0]);
    const quint8 ins = static_cast<quint8>(command[1]);
    Q_UNUSED(cla)

    return QStringLiteral("%1 [%2 bytes]")
        .arg(apduDescription(ins))
        .arg(command.length());
}

/**
 * @brief Returns safe status word description.
 */
inline QString swDescription(quint16 sw)
{
    switch (sw) {
    case 0x9000: return QStringLiteral("SUCCESS");
    case 0x6985: return QStringLiteral("TOUCH_REQUIRED");
    case 0x6982: return QStringLiteral("AUTH_REQUIRED");
    case 0x6984: return QStringLiteral("WRONG_PASSWORD");
    case 0x6A80: return QStringLiteral("INVALID_DATA");
    case 0x6A82: return QStringLiteral("NOT_FOUND");
    case 0x6A84: return QStringLiteral("NO_SPACE");
    default: return QStringLiteral("SW_0x%1").arg(sw, 4, 16, QLatin1Char('0'));
    }
}

} // namespace SecureLogging
} // namespace Daemon
} // namespace YubiKeyOath

#endif // YUBIKEY_OATH_SECURE_LOGGING_H
