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
#include "shared/utils/version.h"

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

/**
 * @brief Base class for brand-specific OATH protocol implementations
 *
 * This base class provides universal OATH specification methods (85% of protocol logic):
 * - Protocol constants (instruction codes, status words, TLV tags)
 * - APDU command creation (SELECT, LIST, CALCULATE, VALIDATE, PUT, DELETE, etc.)
 * - Response parsing (common TLV parsing, SELECT response, etc.)
 * - Helper functions (TLV parsing, TOTP counter calculation, Base32 decoding)
 *
 * Brand-specific classes (YKOathProtocol, NitrokeySecretsOathProtocol) inherit
 * and override virtual methods for parsing differences (touch detection, response formats).
 *
 * Design rationale:
 * - YubiKey: LIST command has spurious 0x6985 errors, uses CALCULATE_ALL workaround
 * - Nitrokey: LIST works correctly, supports LIST v1 with properties byte
 *
 * Used by brand-specific OathSession implementations (YkOathSession, NitrokeyOathSession).
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
    static constexpr quint16 SW_CONDITIONS_NOT_SATISFIED = 0x6985;
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
    static constexpr quint8 TAG_VERSION = 0x79;
    static constexpr quint8 TAG_IMF = 0x7a;
    static constexpr quint8 TAG_ALGORITHM = 0x7B;      // YubiKey algorithm tag
    static constexpr quint8 TAG_TOUCH = 0x7c;
    static constexpr quint8 TAG_SERIAL_NUMBER = 0x8F;  // Nitrokey serial number (4 bytes)

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
     * @brief Parses SELECT response to extract device ID, challenge, firmware version, password requirement, and serial
     * @param response Response data from SELECT command
     * @param outDeviceId Output parameter for device ID (hex string, from TAG_SERIAL_NUMBER or TAG_NAME)
     * @param outChallenge Output parameter for challenge bytes (from TAG_CHALLENGE)
     * @param outFirmwareVersion Output parameter for firmware version (from TAG_VERSION)
     * @param outRequiresPassword Output parameter for password requirement (true if TAG_CHALLENGE present)
     * @param outSerialNumber Output parameter for serial number (from TAG_SERIAL_NUMBER 0x8F, 0 if not present)
     * @return true on success, false on parse error
     *
     * Brand-specific: Serial number extraction differs
     * - YubiKey: No TAG_SERIAL_NUMBER in SELECT, uses Management API or OTP/PIV
     * - Nitrokey: Includes TAG_SERIAL_NUMBER (0x8F) in SELECT response
     *
     * NOTE: Base class provides default implementation, brands can override if needed
     */
    virtual bool parseSelectResponse(const QByteArray &response,
                                    QString &outDeviceId,
                                    QByteArray &outChallenge,
                                    Version &outFirmwareVersion,
                                    bool &outRequiresPassword,
                                    quint32 &outSerialNumber) const;

    /**
     * @brief Parses LIST response to extract credential names
     * @param response Response data from LIST command
     * @return List of credential names
     *
     * NOTE: This is the base version (LIST version 0). Brand-specific classes may provide
     * additional methods like parseCredentialListV1() for extended formats.
     */
    static QList<OathCredential> parseCredentialList(const QByteArray &response);

    /**
     * @brief Parses CALCULATE response to extract TOTP/HOTP code
     * @param response Response data from CALCULATE command
     * @return Code string (6-8 digits) or empty on error
     *
     * Brand-specific (MUST override): Touch detection status word differs
     * - YubiKey: 0x6985 = touch required
     * - Nitrokey: 0x6982 = touch required
     */
    virtual QString parseCode(const QByteArray &response) const = 0;

    /**
     * @brief Parses CALCULATE ALL response to extract all codes
     * @param response Response data from CALCULATE ALL command
     * @return List of credentials with codes
     *
     * Brand-specific (MUST override): Response format differs between brands
     * - YubiKey: NAME (0x71) + RESPONSE (0x76) or TOUCH (0x7c) or HOTP (0x77)
     * - Nitrokey: May use LIST v1 format with properties byte (or empty if CALCULATE_ALL unsupported)
     */
    virtual QList<OathCredential> parseCalculateAllResponse(const QByteArray &response) const = 0;

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

    /**
     * @brief Parses credential ID to extract period, issuer, and account
     * @param credentialId Full credential ID from YubiKey (format: [period/][issuer:]account)
     * @param isTotp Whether this is a TOTP credential (period only applies to TOTP)
     * @param outPeriod Output: TOTP period in seconds (default 30 if not specified)
     * @param outIssuer Output: Service issuer (empty if not present)
     * @param outAccount Output: Account name
     *
     * Parses YubiKey credential ID format used by ykman:
     * - TOTP: [period/][issuer:]account
     *   Examples: "Google:user@example.com" (period=30, default)
     *             "60/GitHub:mytoken" (period=60)
     *             "15/Steam:login" (period=15)
     * - HOTP: [issuer:]account (no period)
     *
     * Regex: ^((\d+)/)?(([^:]+):)?(.+)$
     * Groups: (1: period with slash, 2: period number, 3: issuer with colon, 4: issuer, 5: account)
     */
    static void parseCredentialId(const QString &credentialId,
                                   bool isTotp,
                                   int &outPeriod,
                                   QString &outIssuer,
                                   QString &outAccount);

    // OTP Application support (for serial number retrieval on YubiKey NEO)
    /**
     * @brief OTP Application Identifier
     *
     * Used for serial number retrieval on YubiKey NEO firmware 3.x.x.
     * OTP application provides CMD_DEVICE_SERIAL which works via CCID/NFC.
     * This is the primary method used by Yubico Authenticator for NEO devices.
     */
    static const QByteArray OTP_AID;

    /**
     * @brief OTP INS_CONFIG instruction code
     *
     * Used for OTP configuration commands including GET_SERIAL.
     */
    static constexpr quint8 INS_OTP_CONFIG = 0x01;

    /**
     * @brief OTP CMD_DEVICE_SERIAL slot code
     *
     * Used as P1 parameter for INS_CONFIG to retrieve device serial number.
     * Available on YubiKey firmware 1.2+ (includes NEO 3.4.0).
     */
    static constexpr quint8 CMD_DEVICE_SERIAL = 0x10;

    /**
     * @brief Creates SELECT OTP application command
     * @return APDU command bytes
     */
    static QByteArray createSelectOtpCommand();

    /**
     * @brief Creates OTP GET_SERIAL command
     * @return APDU command bytes
     *
     * APDU format: 00 01 10 00 00
     * - CLA: 0x00
     * - INS: 0x01 (INS_OTP_CONFIG)
     * - P1:  0x10 (CMD_DEVICE_SERIAL)
     * - P2:  0x00
     * - Lc:  0x00 (no data)
     *
     * Response: 4 bytes (big-endian serial number) + 90 00
     */
    static QByteArray createOtpGetSerialCommand();

    /**
     * @brief Parses OTP GET_SERIAL response
     * @param response Response data from GET_SERIAL command
     * @param outSerial Output parameter for serial number
     * @return true on success, false on parse error
     *
     * Response format: 4 bytes serial number (big-endian) + status word (90 00)
     * Example: 00 35 7A 5C 90 00 → serial = 0x00357A5C = 3504732
     *
     * Status words:
     *   0x9000: Success
     *   0x6D00: INS not supported (OTP not available)
     *   0x6984: Security status not satisfied (serial-api-visible disabled)
     */
    static bool parseOtpSerialResponse(const QByteArray &response,
                                       quint32 &outSerial);

    // PIV Application support (for serial number retrieval on YubiKey NEO)
    /**
     * @brief PIV Application Identifier
     *
     * Used for fallback serial number retrieval on YubiKey NEO and YubiKey 4.
     * YubiKey 5 series should use Management interface instead (faster).
     */
    static const QByteArray PIV_AID;

    /**
     * @brief PIV GET SERIAL instruction code
     *
     * Available on all YubiKey models with PIV support (NEO, 4, 5).
     * Requires serial-api-visible flag (enabled by default).
     */
    static constexpr quint8 INS_GET_SERIAL = 0xF8;

    /**
     * @brief Creates SELECT PIV application command
     * @return APDU command bytes
     */
    static QByteArray createSelectPivCommand();

    /**
     * @brief Creates PIV GET SERIAL command
     * @return APDU command bytes
     *
     * APDU format: 00 F8 00 00
     * - CLA: 0x00
     * - INS: 0xF8 (GET SERIAL)
     * - P1:  0x00
     * - P2:  0x00
     * - No data, no Le
     *
     * Response: 4 bytes (big-endian serial number) + 90 00
     */
    static QByteArray createGetSerialCommand();

    /**
     * @brief Parses PIV GET SERIAL response
     * @param response Response data from GET SERIAL command
     * @param outSerial Output parameter for serial number
     * @return true on success, false on parse error
     *
     * Response format: 4 bytes serial number (big-endian) + status word (90 00)
     * Example: 00 AE 17 CB 90 00 → serial = 0x00AE17CB = 11409355
     *
     * Status words:
     *   0x9000: Success
     *   0x6D00: INS not supported (PIV not available)
     *   0x6982: Security status not satisfied (serial-api-visible disabled)
     */
    static bool parseSerialResponse(const QByteArray &response,
                                   quint32 &outSerial);

    // PC/SC Reader Name Parsing (for legacy device detection)
    /**
     * @brief Information parsed from PC/SC reader name
     *
     * Used as fallback detection method for YubiKey NEO devices that don't support
     * Management Application interface. Yubico Authenticator uses this method
     * to detect device model via NFC/CCID.
     */
    struct ReaderNameInfo {
        bool isNEO = false;          ///< True if "NEO" detected in reader name
        quint32 serialNumber = 0;    ///< Serial extracted from reader name (e.g., "(0003507404)")
        quint8 formFactor = 0;       ///< USB_A_KEYCHAIN (0x01) for NEO, or 0 if unknown
        bool valid = false;          ///< True if parsing succeeded and useful info extracted
    };

    /**
     * @brief Parses PC/SC reader name for device information
     * @param readerName Reader name from SCardListReaders (e.g., "Yubico YubiKey NEO OTP+CCID (0003507404) 00 00")
     * @return Parsed information structure
     *
     * Parsing strategy (Yubico method):
     * 1. Detect "NEO" substring (case-insensitive) → sets isNEO = true
     * 2. Extract serial from format "(XXXXXXXXXX)" or "(00XXXXXXXX)" → 10-digit number
     * 3. Set formFactor = USB_A_KEYCHAIN (0x01) if NEO detected (all NEO are USB-A keychain)
     * 4. Mark valid = true if any useful information was extracted
     *
     * Examples:
     * - "Yubico YubiKey NEO OTP+U2F+CCID (0003507404) 00 00" → isNEO=true, serial=3507404, formFactor=0x01
     * - "Yubico YubiKey OTP+FIDO+CCID 01 00" → isNEO=false, serial=0, formFactor=0, valid=false
     *
     * Use case: YubiKey NEO 3.4.0 doesn't support Management Application,
     * so we use reader name parsing as fallback detection method.
     */
    static ReaderNameInfo parseReaderNameInfo(const QString &readerName);

    // Virtual destructor for polymorphic deletion
    virtual ~OathProtocol() = default;

protected:
    // Protected constructor - allow inheritance but prevent direct instantiation
    OathProtocol() = default;
};

} // namespace Daemon
} // namespace YubiKeyOath
