/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Error code constants for OATH operations
 *
 * These constants provide stable error identifiers that are independent
 * of i18n translations. Use these for programmatic error comparison instead
 * of comparing user-facing translated strings.
 *
 * Usage:
 * @code
 * // Generating errors
 * return Result<QString>::error(OathErrorCodes::PASSWORD_REQUIRED);
 *
 * // Checking errors
 * if (result.isError() && result.error() == OathErrorCodes::PASSWORD_REQUIRED) {
 *     // Handle password required case
 * }
 * @endcode
 */
namespace OathErrorCodes {

/**
 * @brief Operation requires password/PIN authentication
 *
 * Returned when OATH operation fails with SW_SECURITY_STATUS_NOT_SATISFIED (0x6982/0x6985).
 * The device requires authentication before performing the requested operation.
 */
inline const QString PASSWORD_REQUIRED = QStringLiteral("OATH_ERROR_PASSWORD_REQUIRED");

/**
 * @brief Touch/user presence required
 *
 * Returned when credential requires physical touch but touch was not detected.
 * Status words: YubiKey=0x6985, Nitrokey=0x6982
 */
inline const QString TOUCH_REQUIRED = QStringLiteral("OATH_ERROR_TOUCH_REQUIRED");

/**
 * @brief Authentication failed (wrong password/PIN)
 *
 * Returned when password/PIN validation fails during VALIDATE command.
 */
inline const QString AUTHENTICATION_FAILED = QStringLiteral("OATH_ERROR_AUTHENTICATION_FAILED");

/**
 * @brief Device communication error
 *
 * Returned when PC/SC communication fails or device returns unexpected response.
 */
inline const QString COMMUNICATION_ERROR = QStringLiteral("OATH_ERROR_COMMUNICATION");

/**
 * @brief Credential not found
 *
 * Returned when requested credential does not exist on device.
 */
inline const QString CREDENTIAL_NOT_FOUND = QStringLiteral("OATH_ERROR_CREDENTIAL_NOT_FOUND");

/**
 * @brief Invalid OATH response format
 *
 * Returned when device response cannot be parsed (malformed TLV, unexpected data).
 */
inline const QString INVALID_RESPONSE = QStringLiteral("OATH_ERROR_INVALID_RESPONSE");

/**
 * @brief Operation timeout
 *
 * Returned when operation exceeds timeout (typically waiting for touch).
 */
inline const QString TIMEOUT = QStringLiteral("OATH_ERROR_TIMEOUT");

/**
 * @brief Device disconnected during operation
 *
 * Returned when card is removed or connection lost mid-operation.
 */
inline const QString DEVICE_DISCONNECTED = QStringLiteral("OATH_ERROR_DEVICE_DISCONNECTED");

} // namespace OathErrorCodes

} // namespace Daemon
} // namespace YubiKeyOath
