/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_protocol.h"
#include "../logging_categories.h"

#include <QDateTime>
#include <QDebug>

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
    int const dataLen = 1 + 1 + nameBytes.length() + 1 + 1 + challenge.length();
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
    int const dataLen = 1 + 1 + response.length() + 1 + 1 + challenge.length();
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
    if (data.requireTouch) {
        tlvData.append(static_cast<char>(TAG_PROPERTY));
        tlvData.append((char)0x01);  // Length = 1
        tlvData.append((char)0x02);  // Value = 0x02 (require touch)
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
    // Format: CLA INS P1 P2 Lc
    // CLA=0x00, INS=0x03 (SET_CODE), P1=0x00, P2=0x00, Lc=0x00
    // Sending length 0 removes authentication requirement

    QByteArray command;
    command.reserve(5);

    // APDU header
    command.append(static_cast<char>(CLA));          // CLA = 0x00
    command.append(static_cast<char>(INS_SET_CODE)); // INS = 0x03
    command.append((char)0x00);                      // P1 = 0x00
    command.append((char)0x00);                      // P2 = 0x00
    command.append((char)0x00);                      // Lc = 0x00 (no data)

    // No data, no Le

    return command;
}

// =============================================================================
// Response Parsing
// =============================================================================

bool OathProtocol::parseSelectResponse(const QByteArray &response,
                                       QString &outDeviceId,
                                       QByteArray &outChallenge)
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

    // Look for TAG_NAME_SALT (0x71) and TAG_CHALLENGE (0x74)
    int pos = 0;
    while (pos < data.length() - 1) {
        if (pos + 2 > data.length()) { break;
}

        quint8 const tag = data[pos];
        quint8 const len = data[pos + 1];

        if (pos + 2 + len > data.length()) { break;
}

        if (tag == TAG_NAME_SALT) {
            // Extract device ID (hex string)
            QByteArray const deviceIdBytes = data.mid(pos + 2, len);
            outDeviceId = QString::fromLatin1(deviceIdBytes.toHex());
        } else if (tag == TAG_CHALLENGE) {
            // Extract challenge
            outChallenge = data.mid(pos + 2, len);
        }

        pos += 2 + len;
    }

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
                cred.name = name;
                cred.isTotp = (nameAlgo & 0x0F) == 0x02; // TOTP if lower nibble is 0x02

                // Parse issuer and username from name (format: "issuer:username")
                if (name.contains(QStringLiteral(":"))) {
                    QStringList parts = name.split(QStringLiteral(":"));
                    if (parts.size() >= 2) {
                        cred.issuer = parts[0];
                        cred.username = parts.mid(1).join(QStringLiteral(":"));
                    }
                } else {
                    cred.issuer = name;
                }

                credentials.append(cred);
            }
        }

        i += length;
    }

    return credentials;
}

QString OathProtocol::parseCode(const QByteArray &response)
{
    if (response.length() < 2) {
        return {};
    }

    // Check status word
    quint16 const sw = getStatusWord(response);

    // Special case: 0x6985 = touch required
    if (sw == 0x6985) {
        return {}; // Caller should detect this via status word
    }

    if (!isSuccess(sw)) {
        return {};
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

        if (tag == TAG_TOTP_RESPONSE && length >= 5) { // TAG_TOTP_RESPONSE = 0x76
            // First byte is number of digits
            quint8 const digits = data[i];

            // Next 4 bytes are the code value (big-endian)
            quint32 codeValue = 0;
            for (int j = 0; j < 4; ++j) {
                codeValue = (codeValue << 8) | static_cast<quint8>(data[i + 1 + j]);
            }

            // Format code with proper digit count
            return formatCode(data.mid(i, length), digits);
        }

        i += length;
    }

    return {};
}

QList<OathCredential> OathProtocol::parseCalculateAllResponse(const QByteArray &response)
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

    // CALCULATE ALL response format: NAME (0x71) followed by RESPONSE (0x76) or HOTP (0x77) or TOUCH (0x7c)
    int i = 0;
    while (i < data.length()) {
        if (i + 2 > data.length()) { break;
}

        quint8 const tag = data[i++];
        quint8 const length = data[i++];

        if (i + length > data.length()) { break;
}

        if (tag == TAG_NAME) { // TAG_NAME = 0x71
            // Parse credential name (no algorithm byte in CALCULATE ALL response)
            QByteArray const nameBytes = data.mid(i, length);
            QString const name = QString::fromUtf8(nameBytes);

            OathCredential cred;
            cred.name = name;
            cred.isTotp = true; // Assume TOTP by default

            // Parse issuer and username from name
            if (name.contains(QStringLiteral(":"))) {
                QStringList parts = name.split(QStringLiteral(":"));
                if (parts.size() >= 2) {
                    cred.issuer = parts[0];
                    cred.username = parts.mid(1).join(QStringLiteral(":"));
                }
            } else {
                cred.issuer = name;
            }

            credentials.append(cred);
            i += length;

            // Next should be RESPONSE tag (0x76), HOTP (0x77), or TOUCH (0x7c)
            if (i + 2 <= data.length()) {
                quint8 const respTag = data[i++];
                quint8 const respLength = data[i++];

                if (i + respLength > data.length()) { break;
}

                if (respTag == TAG_TOUCH) {
                    // Touch required
                    if (!credentials.isEmpty()) {
                        credentials.last().requiresTouch = true;
                    }
                } else if (respTag == TAG_HOTP) {
                    // HOTP credential - no response to avoid incrementing counter
                    if (!credentials.isEmpty()) {
                        credentials.last().isTotp = false;
                    }
                } else if (respTag == TAG_TOTP_RESPONSE && respLength >= 5) {
                    // Parse code
                    quint8 const digits = data[i];
                    quint32 codeValue = 0;
                    for (int j = 0; j < 4; ++j) {
                        codeValue = (codeValue << 8) | static_cast<quint8>(data[i + 1 + j]);
                    }

                    QString const code = formatCode(data.mid(i, respLength), digits);

                    if (!credentials.isEmpty()) {
                        credentials.last().code = code;

                        // Calculate validity using default 30-second TOTP period
                        // NOTE: For credentials with non-standard period, validUntil will be incorrect.
                        // Caller should recalculate validUntil using the actual period from metadata.
                        // YubiKey doesn't return period info in CALCULATE_ALL response.
                        qint64 const currentTime = QDateTime::currentSecsSinceEpoch();
                        constexpr int defaultPeriod = 30;
                        qint64 const timeInPeriod = currentTime % defaultPeriod;
                        qint64 const validityRemaining = defaultPeriod - timeInPeriod;
                        credentials.last().validUntil = currentTime + validityRemaining;
                    }
                }

                i += respLength;
            }
        } else {
            // Skip unknown tags
            i += length;
        }
    }

    return credentials;
}

bool OathProtocol::parseSetCodeResponse(const QByteArray &response,
                                        QByteArray &outVerificationResponse)
{
    if (response.length() < 2) {
        return false;
    }

    // Check status word
    quint16 const sw = getStatusWord(response);

    // Extract response data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    // Try to extract verification response (TAG_RESPONSE 0x75)
    outVerificationResponse = findTlvTag(data, TAG_RESPONSE);

    // Check for success
    if (sw == SW_SUCCESS) {
        return true;
    }

    // Log specific errors
    switch (sw) {
    case 0x6984:
        // Response verification failed - wrong old password
        return false;
    case SW_SECURITY_STATUS_NOT_SATISFIED: // 0x6982
        // Authentication required
        return false;
    case SW_WRONG_DATA: // 0x6a80
        // Incorrect syntax
        return false;
    default:
        return false;
    }
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
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    QByteArray result;
    quint64 buffer = 0;
    int bitsInBuffer = 0;

    for (const QChar &ch : cleaned) {
        // Find character position in alphabet
        const char *pos = strchr(alphabet, ch.toLatin1());
        if (!pos) {
            // Invalid character
            qWarning() << "Invalid Base32 character:" << ch;
            return {};
        }

        int const value = pos - alphabet;

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

} // namespace Daemon
} // namespace YubiKeyOath
