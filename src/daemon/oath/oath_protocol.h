/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <QList>
#include "types/oath_credential.h"
#include "types/oath_credential_data.h"

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

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
    static constexpr quint8 INS_PUT = 0x01;
    static constexpr quint8 INS_DELETE = 0x02;
    static constexpr quint8 INS_SET_CODE = 0x03;
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
    static constexpr quint16 SW_NO_SUCH_OBJECT = 0x6984;
    static constexpr quint16 SW_WRONG_DATA = 0x6A80;
    static constexpr quint16 SW_INSUFFICIENT_SPACE = 0x6A84;
    static constexpr quint16 SW_INS_NOT_SUPPORTED = 0x6D00;
    static constexpr quint16 SW_CLA_NOT_SUPPORTED = 0x6E00;

    // TLV tags
    static constexpr quint8 TAG_NAME = 0x71;
    static constexpr quint8 TAG_NAME_SALT = 0x71;  // Same as TAG_NAME, context-dependent
    static constexpr quint8 TAG_NAME_LIST = 0x72;
    static constexpr quint8 TAG_KEY = 0x73;
    static constexpr quint8 TAG_CHALLENGE = 0x74;
    static constexpr quint8 TAG_RESPONSE = 0x75;
    static constexpr quint8 TAG_TOTP_RESPONSE = 0x76;
    static constexpr quint8 TAG_HOTP = 0x77;
    static constexpr quint8 TAG_PROPERTY = 0x78;
    static constexpr quint8 TAG_IMF = 0x7a;
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

    /**
     * @brief Creates PUT command for adding/updating credential
     * @param data Credential data (name, secret, algorithm, etc.)
     * @return APDU command bytes
     *
     * Format (TLV):
     *   TAG_NAME (0x71): credential name (UTF-8)
     *   TAG_KEY (0x73): [algo_byte][digits][key_bytes]
     *     algo_byte = (type << 4) | algorithm
     *       type: 0x1=HOTP, 0x2=TOTP
     *       algorithm: 0x1=SHA1, 0x2=SHA256, 0x3=SHA512
     *     digits: 0x06, 0x07, or 0x08
     *     key_bytes: Base32 decoded secret (min 14 bytes, padded)
     *   TAG_PROPERTY (0x78): 0x02 if requireTouch
     *   TAG_IMF (0x7a): 4-byte counter (HOTP only)
     */
    static QByteArray createPutCommand(const OathCredentialData &data);

    /**
     * @brief Creates DELETE command for removing credential
     * @param name Full credential name (issuer:username)
     * @return APDU command bytes
     *
     * Format (TLV):
     *   TAG_NAME (0x71): credential name (UTF-8)
     */
    static QByteArray createDeleteCommand(const QString &name);

    /**
     * @brief Creates SET_CODE command to set/change device password
     * @param key Derived key (PBKDF2 of password, 16 bytes)
     * @param challenge Challenge for mutual authentication (8 bytes)
     * @param response HMAC response to device's challenge (variable length)
     * @return APDU command bytes
     *
     * Format (TLV):
     *   TAG_KEY (0x73): [algorithm (0x01=HMAC-SHA1)][key_bytes (16 bytes)]
     *   TAG_CHALLENGE (0x74): 8-byte challenge
     *   TAG_RESPONSE (0x75): HMAC response to device challenge
     *
     * Note: Algorithm 0x01 (HMAC-SHA1) is standard for OATH password auth
     */
    static QByteArray createSetCodeCommand(const QByteArray &key,
                                           const QByteArray &challenge,
                                           const QByteArray &response);

    /**
     * @brief Creates SET_CODE command to remove device password
     * @return APDU command bytes (length 0 data)
     *
     * Sending SET_CODE with length 0 removes authentication requirement.
     */
    static QByteArray createRemoveCodeCommand();

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

    /**
     * @brief Parses SET_CODE response and verifies success
     * @param response Response data from SET_CODE command
     * @param outVerificationResponse Output parameter for verification response (TAG_RESPONSE)
     * @return true on success (SW=0x9000), false on failure
     *
     * Status words:
     *   0x9000: Success
     *   0x6984: Response verification failed (wrong old password)
     *   0x6982: Authentication required
     *   0x6a80: Incorrect syntax
     */
    static bool parseSetCodeResponse(const QByteArray &response,
                                    QByteArray &outVerificationResponse);

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

    /**
     * @brief Decodes Base32 string to binary data
     * @param base32 Base32 encoded string (A-Z, 2-7, optional padding =)
     * @return Decoded bytes, or empty if invalid
     *
     * RFC 3548 Base32 decoding without padding requirement.
     * Used for decoding OATH secrets from otpauth:// URIs.
     */
    static QByteArray decodeBase32(const QString &base32);

private:
    // Private constructor - utility class only
    OathProtocol() = delete;
};

} // namespace Daemon
} // namespace YubiKeyOath
