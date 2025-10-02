/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <QList>
#include "../../shared/types/oath_credential.h"

namespace KRunner {
namespace YubiKey {

/**
 * @brief Stateless utility class for OATH protocol operations
 *
 * This class provides pure functions for OATH protocol handling:
 * - Protocol constants (instruction codes, status words, TLV tags)
 * - APDU command creation
 * - Response parsing
 * - Helper functions (TLV parsing, TOTP counter calculation)
 *
 * No state, no I/O - all functions are static.
 * Used by OathSession for actual communication with YubiKey.
 */
class OathProtocol
{
public:
    // OATH Application constants
    static constexpr quint8 CLA = 0x00;

    // Instruction codes
    static constexpr quint8 INS_SELECT = 0xa4;
    static constexpr quint8 INS_LIST = 0xa1;
    static constexpr quint8 INS_CALCULATE = 0xa2;
    static constexpr quint8 INS_VALIDATE = 0xa3;
    static constexpr quint8 INS_CALCULATE_ALL = 0xa4;
    static constexpr quint8 INS_SEND_REMAINING = 0xa5;

    // Status words
    static constexpr quint16 SW_SUCCESS = 0x9000;
    static constexpr quint16 SW_OK = 0x9000;
    static constexpr quint16 SW_MORE_DATA = 0x6100;
    static constexpr quint16 SW_SECURITY_STATUS_NOT_SATISFIED = 0x6982;
    static constexpr quint16 SW_WRONG_DATA = 0x6A80;

    // TLV tags
    static constexpr quint8 TAG_NAME = 0x71;
    static constexpr quint8 TAG_NAME_SALT = 0x71;  // Same as TAG_NAME, context-dependent
    static constexpr quint8 TAG_NAME_LIST = 0x72;
    static constexpr quint8 TAG_CHALLENGE = 0x74;
    static constexpr quint8 TAG_RESPONSE = 0x75;
    static constexpr quint8 TAG_TOTP_RESPONSE = 0x76;
    static constexpr quint8 TAG_HOTP = 0x77;
    static constexpr quint8 TAG_TOUCH = 0x7c;

    // OATH Application Identifier
    static const QByteArray OATH_AID;

    // Command creation
    /**
     * @brief Creates SELECT OATH application command
     * @return APDU command bytes
     */
    static QByteArray createSelectCommand();

    /**
     * @brief Creates LIST credentials command
     * @return APDU command bytes
     */
    static QByteArray createListCommand();

    /**
     * @brief Creates CALCULATE command for single credential
     * @param name Full credential name (issuer:username)
     * @param challenge TOTP challenge (8 bytes)
     * @return APDU command bytes
     */
    static QByteArray createCalculateCommand(const QString &name, const QByteArray &challenge);

    /**
     * @brief Creates CALCULATE ALL command for all credentials
     * @param challenge TOTP challenge (8 bytes)
     * @return APDU command bytes
     */
    static QByteArray createCalculateAllCommand(const QByteArray &challenge);

    /**
     * @brief Creates VALIDATE command for password authentication
     * @param response HMAC response to challenge
     * @param challenge Challenge from YubiKey
     * @return APDU command bytes
     */
    static QByteArray createValidateCommand(const QByteArray &response, const QByteArray &challenge);

    /**
     * @brief Creates SEND REMAINING command for chained responses
     * @return APDU command bytes
     */
    static QByteArray createSendRemainingCommand();

    // Response parsing
    /**
     * @brief Parses SELECT response to extract device ID and challenge
     * @param response Response data from SELECT command
     * @param outDeviceId Output parameter for device ID (hex string)
     * @param outChallenge Output parameter for challenge bytes
     * @return true on success, false on parse error
     */
    static bool parseSelectResponse(const QByteArray &response,
                                   QString &outDeviceId,
                                   QByteArray &outChallenge);

    /**
     * @brief Parses LIST response to extract credential names
     * @param response Response data from LIST command
     * @return List of credential names
     */
    static QList<OathCredential> parseCredentialList(const QByteArray &response);

    /**
     * @brief Parses CALCULATE response to extract TOTP/HOTP code
     * @param response Response data from CALCULATE command
     * @return Code string (6-8 digits) or empty on error
     */
    static QString parseCode(const QByteArray &response);

    /**
     * @brief Parses CALCULATE ALL response to extract all codes
     * @param response Response data from CALCULATE ALL command
     * @return List of credentials with codes
     */
    static QList<OathCredential> parseCalculateAllResponse(const QByteArray &response);

    // Helper functions
    /**
     * @brief Finds TLV tag in data
     * @param data Data to search
     * @param tag Tag to find
     * @return Value bytes for tag, or empty if not found
     */
    static QByteArray findTlvTag(const QByteArray &data, quint8 tag);

    /**
     * @brief Calculates TOTP counter from current time
     * @param period TOTP period in seconds (typically 30)
     * @return 8-byte counter in big-endian format
     */
    static QByteArray calculateTotpCounter(int period = 30);

    /**
     * @brief Creates TOTP challenge from current time
     * @param period TOTP period in seconds (typically 30)
     * @return 8-byte challenge for CALCULATE/CALCULATE_ALL
     */
    static QByteArray createTotpChallenge(int period = 30);

    /**
     * @brief Extracts status word from response
     * @param response Response bytes
     * @return Status word (SW1 << 8 | SW2)
     */
    static quint16 getStatusWord(const QByteArray &response);

    /**
     * @brief Checks if status word indicates more data available
     * @param sw Status word
     * @return true if more data available (0x61XX)
     */
    static bool hasMoreData(quint16 sw);

    /**
     * @brief Checks if status word indicates success
     * @param sw Status word
     * @return true if success (0x9000)
     */
    static bool isSuccess(quint16 sw);

    /**
     * @brief Formats TOTP/HOTP code with proper digit count
     * @param rawCode Raw code bytes from response
     * @param digits Number of digits (6-8)
     * @return Formatted code string with leading zeros
     */
    static QString formatCode(const QByteArray &rawCode, int digits);

private:
    // Private constructor - utility class only
    OathProtocol() = delete;
};

} // namespace YubiKey
} // namespace KRunner
