/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_protocol.h"
#include "../logging_categories.h"

#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

// OATH Application Identifier
const QByteArray OathProtocol::OATH_AID = QByteArray::fromHex("a0000005272101");

// =============================================================================
// Command Creation
// =============================================================================

QByteArray OathProtocol::createSelectCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));           // CLA
    command.append(static_cast<char>(INS_SELECT));    // INS
    command.append((char)0x04);                       // P1 = Select by name
    command.append((char)0x00);                       // P2
    command.append(static_cast<char>(OATH_AID.length())); // Lc
    command.append(OATH_AID);                         // Data = AID
    command.append((char)0x00);                       // Le = expect response (Nitrokey 3 compatibility)

    return command;
}

QByteArray OathProtocol::createListCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));        // CLA
    command.append(static_cast<char>(INS_LIST));   // INS
    command.append((char)0x00);                    // P1
    command.append((char)0x00);                    // P2
    // No Lc or Le per YubiKey OATH spec

    return command;
}

QByteArray OathProtocol::createCalculateCommand(const QString &name, const QByteArray &challenge)
{
    QByteArray command;
    command.append(static_cast<char>(CLA));           // CLA
    command.append(static_cast<char>(INS_CALCULATE)); // INS
    command.append((char)0x00);                       // P1
    command.append((char)0x01);                       // P2 = Request response

    // Data length: tag + length_byte + data for both NAME and CHALLENGE
    QByteArray const nameBytes = name.toUtf8();
    int const dataLen = static_cast<int>(1 + 1 + nameBytes.length() + 1 + 1 + challenge.length());
    command.append(static_cast<char>(dataLen)); // Lc

    // NAME tag + length + data
    command.append(static_cast<char>(TAG_NAME));
    command.append(static_cast<char>(nameBytes.length()));
    command.append(nameBytes);

    // CHALLENGE tag + length + data
    command.append(static_cast<char>(TAG_CHALLENGE));
    command.append(static_cast<char>(challenge.length()));
    command.append(challenge);

    // No Le per YubiKey OATH spec

    return command;
}

QByteArray OathProtocol::createCalculateAllCommand(const QByteArray &challenge)
{
    QByteArray command;
    command.append(static_cast<char>(CLA));               // CLA
    command.append(static_cast<char>(INS_CALCULATE_ALL)); // INS
    command.append((char)0x00);                           // P1
    command.append((char)0x01);                           // P2 = Truncate response

    // Data = CHALLENGE tag + length + challenge
    command.append(static_cast<char>(1 + 1 + challenge.length())); // Lc
    command.append(static_cast<char>(TAG_CHALLENGE));
    command.append(static_cast<char>(challenge.length()));
    command.append(challenge);

    // No Le per YubiKey OATH spec
    // NOTE: Nitrokey 3C requires Le=0x00 for SELECT but NOT for CALCULATE_ALL
    // Adding Le here causes 0x6d00 (INS not supported) error on Nitrokey

    return command;
}

QByteArray OathProtocol::createValidateCommand(const QByteArray &response, const QByteArray &challenge)
{
    QByteArray command;
    command.append(static_cast<char>(CLA));           // CLA
    command.append(static_cast<char>(INS_VALIDATE));  // INS
    command.append((char)0x00);                       // P1
    command.append((char)0x00);                       // P2

    // Data = RESPONSE tag + length + response + CHALLENGE tag + length + challenge
    int const dataLen = static_cast<int>(1 + 1 + response.length() + 1 + 1 + challenge.length());
    command.append(static_cast<char>(dataLen)); // Lc

    // RESPONSE tag
    command.append(static_cast<char>(TAG_RESPONSE));
    command.append(static_cast<char>(response.length()));
    command.append(response);

    // CHALLENGE tag (for mutual authentication)
    command.append(static_cast<char>(TAG_CHALLENGE));
    command.append(static_cast<char>(challenge.length()));
    command.append(challenge);

    // No Le per YubiKey OATH spec

    return command;
}

QByteArray OathProtocol::createSendRemainingCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));                  // CLA
    command.append(static_cast<char>(INS_SEND_REMAINING));   // INS = SEND REMAINING (OATH-specific)
    command.append((char)0x00);                              // P1
    command.append((char)0x00);                              // P2
    command.append((char)0x00);                              // Le = 0 (get up to 256 bytes)

    return command;
}

QByteArray OathProtocol::createPutCommand(const OathCredentialData &data)
{
    QByteArray command;
    command.append(static_cast<char>(CLA));      // CLA
    command.append(static_cast<char>(INS_PUT));  // INS = PUT
    command.append((char)0x00);                  // P1
    command.append((char)0x00);                  // P2

    // Build TLV data
    QByteArray tlvData;

    // TAG_NAME (0x71): credential name in UTF-8
    QByteArray nameBytes = data.name.toUtf8();
    if (nameBytes.length() > 64) {
        nameBytes = nameBytes.left(64); // Max 64 bytes per spec
    }
    tlvData.append(static_cast<char>(TAG_NAME));
    tlvData.append(static_cast<char>(nameBytes.length()));
    tlvData.append(nameBytes);

    // TAG_KEY (0x73): [algo_byte][digits][key_bytes]
    // Decode Base32 secret
    QByteArray keyBytes = decodeBase32(data.secret);
    if (keyBytes.isEmpty()) {
        qWarning() << "Failed to decode Base32 secret";
        return {}; // Return empty on error
    }

    // Pad to minimum 14 bytes
    while (keyBytes.length() < 14) {
        keyBytes.append((char)0x00);
    }

    // Build KEY tag data
    QByteArray keyTagData;

    // algo_byte = (type << 4) | algorithm
    quint8 const typeBits = static_cast<quint8>(data.type) & 0x0F;
    quint8 const algoBits = static_cast<quint8>(data.algorithm) & 0x0F;
    quint8 const algoByte = (typeBits << 4) | algoBits;
    keyTagData.append(static_cast<char>(algoByte));

    // digits
    keyTagData.append(static_cast<char>(data.digits));

    // key bytes
    keyTagData.append(keyBytes);

    // Append KEY tag
    tlvData.append(static_cast<char>(TAG_KEY));
    tlvData.append(static_cast<char>(keyTagData.length()));
    tlvData.append(keyTagData);

    // TAG_PROPERTY (0x78): 0x02 if requireTouch
    // Note: TAG_PROPERTY uses Tag-Value format (not Tag-Length-Value)
    if (data.requireTouch) {
        tlvData.append(static_cast<char>(TAG_PROPERTY));
        tlvData.append((char)0x02);  // Value = 0x02 (require touch) - NO length byte!
    }

    // TAG_IMF (0x7a): 4-byte counter (HOTP only)
    if (data.type == OathType::HOTP) {
        tlvData.append(static_cast<char>(TAG_IMF));
        tlvData.append((char)0x04);  // Length = 4
        // Big-endian counter
        tlvData.append(static_cast<char>((data.counter >> 24) & 0xFF));
        tlvData.append(static_cast<char>((data.counter >> 16) & 0xFF));
        tlvData.append(static_cast<char>((data.counter >> 8) & 0xFF));
        tlvData.append(static_cast<char>(data.counter & 0xFF));
    }

    // Append Lc (data length)
    command.append(static_cast<char>(tlvData.length()));

    // Append data
    command.append(tlvData);

    // No Le per YubiKey OATH spec

    return command;
}

QByteArray OathProtocol::createDeleteCommand(const QString &name)
{
    // Format: CLA INS P1 P2 Lc Data
    // CLA=0x00, INS=0x02 (DELETE), P1=0x00, P2=0x00
    // Data: TAG_NAME (0x71) + length + name (UTF-8)

    QByteArray command;
    command.reserve(256);

    // APDU header
    command.append(static_cast<char>(CLA));         // CLA = 0x00
    command.append(static_cast<char>(INS_DELETE));  // INS = 0x02
    command.append((char)0x00);                     // P1 = 0x00
    command.append((char)0x00);                     // P2 = 0x00

    // Build TLV data
    QByteArray tlvData;
    QByteArray const nameBytes = name.toUtf8();

    // TAG_NAME (0x71) + length + name
    tlvData.append(static_cast<char>(TAG_NAME));
    tlvData.append(static_cast<char>(nameBytes.length()));
    tlvData.append(nameBytes);

    // Append Lc (data length)
    command.append(static_cast<char>(tlvData.length()));

    // Append data
    command.append(tlvData);

    // No Le per YubiKey OATH spec

    return command;
}

QByteArray OathProtocol::createSetCodeCommand(const QByteArray &key,
                                               const QByteArray &challenge,
                                               const QByteArray &response)
{
    // Format: CLA INS P1 P2 Lc Data
    // CLA=0x00, INS=0x03 (SET_CODE), P1=0x00, P2=0x00
    // Data: TAG_KEY (0x73) + TAG_CHALLENGE (0x74) + TAG_RESPONSE (0x75)

    QByteArray command;
    command.reserve(256);

    // APDU header
    command.append(static_cast<char>(CLA));          // CLA = 0x00
    command.append(static_cast<char>(INS_SET_CODE)); // INS = 0x03
    command.append((char)0x00);                      // P1 = 0x00
    command.append((char)0x00);                      // P2 = 0x00

    // Build TLV data
    QByteArray tlvData;

    // TAG_KEY (0x73): algorithm (0x01=HMAC-SHA1) + key (16 bytes)
    tlvData.append(static_cast<char>(TAG_KEY));
    tlvData.append(static_cast<char>(1 + key.length())); // Length: 1 (algo) + 16 (key)
    tlvData.append((char)0x01); // Algorithm: HMAC-SHA1
    tlvData.append(key);

    // TAG_CHALLENGE (0x74): 8-byte challenge for mutual authentication
    tlvData.append(static_cast<char>(TAG_CHALLENGE));
    tlvData.append(static_cast<char>(challenge.length()));
    tlvData.append(challenge);

    // TAG_RESPONSE (0x75): HMAC response to device's challenge
    tlvData.append(static_cast<char>(TAG_RESPONSE));
    tlvData.append(static_cast<char>(response.length()));
    tlvData.append(response);

    // Append Lc (data length)
    command.append(static_cast<char>(tlvData.length()));

    // Append data
    command.append(tlvData);

    // No Le per YubiKey OATH spec

    return command;
}

QByteArray OathProtocol::createRemoveCodeCommand()
{
    // Format: CLA INS P1 P2 Lc Data
    // CLA=0x00, INS=0x03 (SET_CODE), P1=0x00, P2=0x00, Lc=0x02
    // Data: TAG_KEY (0x73) + Length (0x00)
    // Sending TAG_KEY with length 0 removes authentication requirement
    // Based on official Yubico implementation: yubikey-manager/yubikit/oath.py

    QByteArray command;
    command.reserve(7);

    // APDU header
    command.append(static_cast<char>(CLA));          // CLA = 0x00
    command.append(static_cast<char>(INS_SET_CODE)); // INS = 0x03
    command.append((char)0x00);                      // P1 = 0x00
    command.append((char)0x00);                      // P2 = 0x00
    command.append((char)0x02);                      // Lc = 0x02 (tag + length)

    // Data: TLV with TAG_KEY and length 0
    command.append(static_cast<char>(TAG_KEY));      // TAG = 0x73
    command.append((char)0x00);                      // Length = 0x00 (remove password)

    // No Le per YubiKey OATH spec

    return command;
}

// =============================================================================
// Response Parsing
// =============================================================================

bool OathProtocol::parseSelectResponse(const QByteArray &response,
                                       QString &outDeviceId,
                                       QByteArray &outChallenge,
                                       Version &outFirmwareVersion,
                                       bool &outRequiresPassword,
                                       quint32 &outSerialNumber) const
{
    if (response.length() < 2) {
        return false;
    }

    // Check status word
    quint16 const sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        return false;
    }

    // Parse TLV data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    // Look for TAG_NAME_SALT (0x71), TAG_CHALLENGE (0x74), TAG_VERSION (0x79), and TAG_SERIAL_NUMBER (0x8F)
    QByteArray nameSalt;
    QByteArray serialNumber;
    bool hasChallengeTag = false;

    // Initialize output parameters
    outRequiresPassword = false;
    outSerialNumber = 0;

    int pos = 0;
    while (pos < data.length() - 1) {
        if (pos + 2 > data.length()) { break;
}

        quint8 const tag = data[pos];
        quint8 const len = data[pos + 1];

        if (pos + 2 + len > data.length()) { break;
}

        if (tag == TAG_NAME_SALT) {
            // Extract name/salt for device ID (fallback if no serial)
            nameSalt = data.mid(pos + 2, len);
        } else if (tag == TAG_CHALLENGE) {
            // Extract challenge - presence indicates password is required
            outChallenge = data.mid(pos + 2, len);
            hasChallengeTag = true;
        } else if (tag == TAG_VERSION) {
            // Extract firmware version (3 bytes: major, minor, patch)
            if (len == 3) {
                int const major = static_cast<quint8>(data[pos + 2]);
                int const minor = static_cast<quint8>(data[pos + 3]);
                int const patch = static_cast<quint8>(data[pos + 4]);
                outFirmwareVersion = Version(major, minor, patch);
            }
        } else if (tag == TAG_SERIAL_NUMBER) {
            // Extract serial number (4 bytes, big-endian) - Nitrokey 3
            if (len == 4) {
                serialNumber = data.mid(pos + 2, len);
                outSerialNumber = (static_cast<quint8>(serialNumber[0]) << 24) |
                                 (static_cast<quint8>(serialNumber[1]) << 16) |
                                 (static_cast<quint8>(serialNumber[2]) << 8) |
                                 (static_cast<quint8>(serialNumber[3]));
            }
        }
        // Ignore TAG_ALGORITHM (0x7B) - YubiKey-specific, not needed

        pos += 2 + len;
    }

    // Device ID priority: serial number > name/salt
    if (!serialNumber.isEmpty()) {
        // Nitrokey: 4 bytes = 8 hex chars, pad to 16 for database compatibility
        // YubiKey: doesn't send TAG_SERIAL_NUMBER, uses TAG_NAME instead
        const QString serialHex = QString::fromLatin1(serialNumber.toHex());
        outDeviceId = serialHex.rightJustified(16, QLatin1Char('0'));  // "218a715f" → "00000000218a715f"
    } else if (!nameSalt.isEmpty()) {
        outDeviceId = QString::fromLatin1(nameSalt.toHex());
    }

    // Password requirement detection: TAG_CHALLENGE presence
    outRequiresPassword = hasChallengeTag;

    return !outDeviceId.isEmpty();
}

QList<OathCredential> OathProtocol::parseCredentialList(const QByteArray &response)
{
    QList<OathCredential> credentials;

    if (response.length() < 2) {
        return credentials;
    }

    // Check status word
    quint16 const sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        return credentials;
    }

    // Parse TLV data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    int i = 0;
    while (i < data.length()) {
        if (i + 2 > data.length()) { break;
}

        quint8 const tag = data[i++];
        quint8 const length = data[i++];

        if (i + length > data.length()) { break;
}

        if (tag == TAG_NAME_LIST) { // TAG_NAME_LIST = 0x72
            QByteArray nameData = data.mid(i, length);

            // Parse name data: first byte is algorithm + type
            if (nameData.length() >= 2) {
                quint8 const nameAlgo = nameData[0];
                QByteArray const nameBytes = nameData.mid(1);
                QString const name = QString::fromUtf8(nameBytes);

                OathCredential cred;
                cred.originalName = name;

                // Extract type from lower 4 bits of nameAlgo
                quint8 const oathType = nameAlgo & 0x0F;
                cred.isTotp = (oathType == 0x02); // TOTP if lower nibble is 0x02
                cred.type = static_cast<int>(oathType);

                // Extract algorithm from upper 4 bits of nameAlgo
                quint8 const algorithmBits = (nameAlgo >> 4) & 0x0F;
                cred.algorithm = static_cast<int>(algorithmBits);

                // Parse credential ID to extract period, issuer, and account
                int period = 30;
                QString issuer;
                QString account;
                parseCredentialId(name, cred.isTotp, period, issuer, account);

                cred.period = period;
                cred.issuer = issuer;
                cred.account = account;

                credentials.append(cred);
            }
        }

        i += length;
    }

    return credentials;
}

// parseCode() moved to brand-specific classes (YKOathProtocol, NitrokeySecretsOathProtocol)
// - YubiKey: Uses 0x6985 for touch required
// - Nitrokey: Uses 0x6982 for touch required

// parseCalculateAllResponse() moved to brand-specific classes (YKOathProtocol, NitrokeySecretsOathProtocol)
// - YubiKey: NAME (0x71) + RESPONSE (0x76) or TOUCH (0x7c) or HOTP (0x77) format
// - Nitrokey: May use LIST v1 format, or return empty if CALCULATE_ALL unsupported (0x6D00)

bool OathProtocol::parseSetCodeResponse(const QByteArray &response,
                                        QByteArray &outVerificationResponse)
{
    if (response.length() < 2) {
        return false;
    }

    // Check status word
    quint16 const sw = getStatusWord(response);

    // Extract response data (excluding status word)
    QByteArray const data = response.left(response.length() - 2);

    // Try to extract verification response (TAG_RESPONSE 0x75)
    outVerificationResponse = findTlvTag(data, TAG_RESPONSE);

    // Check for success
    // All other status codes indicate failure:
    // - 0x6984: Response verification failed (wrong old password)
    // - 0x6982 (SW_SECURITY_STATUS_NOT_SATISFIED): Authentication required
    // - 0x6a80 (SW_WRONG_DATA): Incorrect syntax
    // - Other: Unspecified error
    return sw == SW_SUCCESS;
}

// =============================================================================
// Helper Functions
// =============================================================================

QByteArray OathProtocol::findTlvTag(const QByteArray &data, quint8 tag)
{
    int pos = 0;
    while (pos < data.length() - 1) {
        if (pos + 2 > data.length()) { break;
}

        quint8 const currentTag = data[pos];
        quint8 const len = data[pos + 1];

        if (pos + 2 + len > data.length()) { break;
}

        if (currentTag == tag) {
            return data.mid(pos + 2, len);
        }

        pos += 2 + len;
    }

    return {};
}

QByteArray OathProtocol::calculateTotpCounter(int period)
{
    qint64 const currentTime = QDateTime::currentSecsSinceEpoch();
    qint64 const counter = currentTime / period;

    // Build 8-byte big-endian counter
    QByteArray result;
    for (int i = 7; i >= 0; --i) {
        result.append(static_cast<char>((counter >> (i * 8)) & 0xFF));
    }

    return result;
}

QByteArray OathProtocol::createTotpChallenge(int period)
{
    return calculateTotpCounter(period);
}

quint16 OathProtocol::getStatusWord(const QByteArray &response)
{
    if (response.length() < 2) {
        return 0;
    }

    auto const sw1 = static_cast<quint8>(response[response.length() - 2]);
    auto const sw2 = static_cast<quint8>(response[response.length() - 1]);

    return (sw1 << 8) | sw2;
}

bool OathProtocol::hasMoreData(quint16 sw)
{
    return (sw & 0xFF00) == 0x6100;
}

bool OathProtocol::isSuccess(quint16 sw)
{
    return sw == SW_SUCCESS;
}

QString OathProtocol::formatCode(const QByteArray &rawCode, int digits)
{
    if (rawCode.length() < 5) {
        return {};
    }

    // First byte is number of digits (should match parameter)
    // Next 4 bytes are the code value (big-endian)
    quint32 codeValue = 0;
    for (int j = 0; j < 4; ++j) {
        codeValue = (codeValue << 8) | static_cast<quint8>(rawCode[1 + j]);
    }

    // Apply modulo to get the code
    quint32 modulo = 1;
    for (int j = 0; j < digits; ++j) {
        modulo *= 10;
    }
    codeValue = codeValue % modulo;

    // Format code with leading zeros
    QString code = QString::number(codeValue);
    while (code.length() < digits) {
        code.prepend(QLatin1Char('0'));
    }

    return code;
}

QByteArray OathProtocol::decodeBase32(const QString &base32)
{
    // Remove padding and convert to uppercase
    QString const cleaned = base32.toUpper().remove(QLatin1Char('='));

    // Base32 alphabet: A-Z (0-25), 2-7 (26-31)
    static constexpr QLatin1String alphabet("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");

    QByteArray result;
    quint64 buffer = 0;
    int bitsInBuffer = 0;

    for (const QChar &ch : cleaned) {
        // Find character position in alphabet
        int const value = static_cast<int>(QString(alphabet).indexOf(ch));
        if (value < 0) {
            // Invalid character
            qWarning() << "Invalid Base32 character:" << ch;
            return {};
        }

        // Accumulate bits
        buffer = (buffer << 5) | value;
        bitsInBuffer += 5;

        // Extract bytes when we have at least 8 bits
        if (bitsInBuffer >= 8) {
            bitsInBuffer -= 8;
            result.append(static_cast<char>((buffer >> bitsInBuffer) & 0xFF));
        }
    }

    return result;
}

void OathProtocol::parseCredentialId(const QString &credentialId,
                                     bool isTotp,
                                     int &outPeriod,
                                     QString &outIssuer,
                                     QString &outAccount)
{
    // Default values
    constexpr int DEFAULT_PERIOD = 30;
    outPeriod = isTotp ? DEFAULT_PERIOD : 0;
    outIssuer.clear();
    outAccount = credentialId;

    if (credentialId.isEmpty()) {
        return;
    }

    // Regex pattern: ^((\d+)/)?(([^:]+):)?(.+)$
    // Groups: 1=period with slash, 2=period number, 3=issuer with colon, 4=issuer, 5=account
    static const QRegularExpression CREDENTIAL_ID_PATTERN(
        QStringLiteral(R"(^((\d+)/)?(([^:]+):)?(.+)$)")
    );

    QRegularExpressionMatch const match = CREDENTIAL_ID_PATTERN.match(credentialId);
    if (!match.hasMatch()) {
        // Pattern didn't match - use whole string as account
        return;
    }

    // Extract period (group 2) - only for TOTP
    if (isTotp) {
        QString const periodStr = match.captured(2);
        if (!periodStr.isEmpty()) {
            bool ok = false;
            int const period = periodStr.toInt(&ok);
            if (ok && period > 0) {
                outPeriod = period;
            }
        }
    }

    // Extract issuer (group 4)
    outIssuer = match.captured(4);

    // Extract account (group 5)
    outAccount = match.captured(5);
    if (outAccount.isEmpty()) {
        // Fallback: use original credential ID as account
        outAccount = credentialId;
    }
}

// =============================================================================
// OTP Application Support (for serial number retrieval on YubiKey NEO)
// =============================================================================

// OTP Application Identifier
// A0 00 00 05 27 20 01 01
const QByteArray OathProtocol::OTP_AID = QByteArray::fromHex("a000000527200101");

QByteArray OathProtocol::createSelectOtpCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));                // CLA = 0x00
    command.append((char)0xA4);                            // INS = SELECT
    command.append((char)0x04);                            // P1 = Select by name
    command.append((char)0x00);                            // P2 = 0x00
    command.append(static_cast<char>(OTP_AID.length()));   // Lc = 8
    command.append(OTP_AID);                               // Data = AID

    return command;
}

QByteArray OathProtocol::createOtpGetSerialCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));                // CLA = 0x00
    command.append(static_cast<char>(INS_OTP_CONFIG));     // INS = 0x01
    command.append(static_cast<char>(CMD_DEVICE_SERIAL));  // P1 = 0x10
    command.append((char)0x00);                            // P2 = 0x00
    command.append((char)0x00);                            // Lc = 0 (no data)

    return command;
}

bool OathProtocol::parseOtpSerialResponse(const QByteArray &response,
                                          quint32 &outSerial)
{
    // Response format: 4 bytes serial (big-endian) + status word (90 00)
    // Example: 00 35 7A 5C 90 00 → serial = 3504732
    if (response.length() < 6) {
        qCWarning(YubiKeyOathDeviceLog) << "OTP serial response too short:" << response.length();
        return false;
    }

    // Check status word
    const quint16 sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        qCWarning(YubiKeyOathDeviceLog) << "OTP GET_SERIAL failed, status word:"
                                        << Qt::hex << Qt::showbase << sw;
        return false;
    }

    // Parse serial number (4 bytes, big-endian)
    const QByteArray serialBytes = response.left(4);
    outSerial = (static_cast<quint8>(serialBytes[0]) << 24) |
                (static_cast<quint8>(serialBytes[1]) << 16) |
                (static_cast<quint8>(serialBytes[2]) << 8) |
                (static_cast<quint8>(serialBytes[3]));

    qCDebug(YubiKeyOathDeviceLog) << "OTP serial parsed successfully:" << outSerial;
    return true;
}

// =============================================================================
// PIV Application Support (for serial number retrieval)
// =============================================================================

// PIV Application Identifier
// A0 00 00 03 08 00 00 10 00
const QByteArray OathProtocol::PIV_AID = QByteArray::fromHex("a00000030800001000");

QByteArray OathProtocol::createSelectPivCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));                // CLA
    command.append((char)0xA4);                            // INS = SELECT
    command.append((char)0x04);                            // P1 = Select by name
    command.append((char)0x00);                            // P2
    command.append(static_cast<char>(PIV_AID.length()));   // Lc
    command.append(PIV_AID);                               // Data = AID

    return command;
}

QByteArray OathProtocol::createGetSerialCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));               // CLA = 0x00
    command.append(static_cast<char>(INS_GET_SERIAL));    // INS = 0xF8
    command.append((char)0x00);                           // P1 = 0x00
    command.append((char)0x00);                           // P2 = 0x00
    // No Lc, no data, no Le

    return command;
}

bool OathProtocol::parseSerialResponse(const QByteArray &response,
                                       quint32 &outSerial)
{
    // Response format: 4 bytes serial (big-endian) + status word (90 00)
    // Example: 00 AE 17 CB 90 00
    if (response.length() < 6) {
        qCWarning(YubiKeyOathDeviceLog) << "Serial response too short:" << response.length();
        return false;
    }

    // Check status word
    const quint16 sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        qCWarning(YubiKeyOathDeviceLog) << "GET SERIAL failed, status word:"
                                        << Qt::hex << Qt::showbase << sw;
        return false;
    }

    // Parse 4-byte big-endian serial number
    outSerial = (static_cast<quint8>(response[0]) << 24) |
                (static_cast<quint8>(response[1]) << 16) |
                (static_cast<quint8>(response[2]) << 8) |
                (static_cast<quint8>(response[3]));

    qCInfo(YubiKeyOathDeviceLog) << "PIV serial number retrieved:" << outSerial;

    return true;
}

// ==================== PC/SC Reader Name Parsing ====================

OathProtocol::ReaderNameInfo OathProtocol::parseReaderNameInfo(const QString &readerName)
{
    ReaderNameInfo info;

    if (readerName.isEmpty()) {
        return info;
    }

    // Detect "NEO" substring (case-insensitive)
    if (readerName.contains(QStringLiteral("NEO"), Qt::CaseInsensitive)) {
        info.isNEO = true;
        info.formFactor = 0x01;  // USB_A_KEYCHAIN - all NEO devices are USB-A keychain
        info.valid = true;

        qCDebug(YubiKeyOathDeviceLog) << "YubiKey NEO detected from reader name:" << readerName;
    }

    // Extract serial number from format: "(XXXXXXXXXX)" or "(00XXXXXXXX)"
    // Example: "Yubico YubiKey NEO OTP+U2F+CCID (0003507404) 00 00"
    const QRegularExpression serialRegex(QStringLiteral(R"(\((\d{10})\))"));
    const QRegularExpressionMatch match = serialRegex.match(readerName);

    if (match.hasMatch()) {
        const QString serialStr = match.captured(1);
        bool ok = false;
        const quint32 serial = serialStr.toUInt(&ok);

        if (ok && serial > 0) {
            info.serialNumber = serial;
            info.valid = true;

            qCDebug(YubiKeyOathDeviceLog) << "Serial number extracted from reader name:" << serial;
        }
    }

    return info;
}

} // namespace Daemon
} // namespace YubiKeyOath
