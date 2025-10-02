/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_protocol.h"
#include "../logging_categories.h"

#include <QDateTime>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

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
    QByteArray nameBytes = name.toUtf8();
    int dataLen = 1 + 1 + nameBytes.length() + 1 + 1 + challenge.length();
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
    int dataLen = 1 + 1 + response.length() + 1 + 1 + challenge.length();
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
    quint16 sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        return false;
    }

    // Parse TLV data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    // Look for TAG_NAME_SALT (0x71) and TAG_CHALLENGE (0x74)
    int pos = 0;
    while (pos < data.length() - 1) {
        if (pos + 2 > data.length()) break;

        quint8 tag = data[pos];
        quint8 len = data[pos + 1];

        if (pos + 2 + len > data.length()) break;

        if (tag == TAG_NAME_SALT) {
            // Extract device ID (hex string)
            QByteArray deviceIdBytes = data.mid(pos + 2, len);
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
    quint16 sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        return credentials;
    }

    // Parse TLV data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    int i = 0;
    while (i < data.length()) {
        if (i + 2 > data.length()) break;

        quint8 tag = data[i++];
        quint8 length = data[i++];

        if (i + length > data.length()) break;

        if (tag == TAG_NAME_LIST) { // TAG_NAME_LIST = 0x72
            QByteArray nameData = data.mid(i, length);

            // Parse name data: first byte is algorithm + type
            if (nameData.length() >= 2) {
                quint8 nameAlgo = nameData[0];
                QByteArray nameBytes = nameData.mid(1);
                QString name = QString::fromUtf8(nameBytes);

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
        return QString();
    }

    // Check status word
    quint16 sw = getStatusWord(response);

    // Special case: 0x6985 = touch required
    if (sw == 0x6985) {
        return QString(); // Caller should detect this via status word
    }

    if (!isSuccess(sw)) {
        return QString();
    }

    // Parse TLV data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    int i = 0;
    while (i < data.length()) {
        if (i + 2 > data.length()) break;

        quint8 tag = data[i++];
        quint8 length = data[i++];

        if (i + length > data.length()) break;

        if (tag == TAG_TOTP_RESPONSE && length >= 5) { // TAG_TOTP_RESPONSE = 0x76
            // First byte is number of digits
            quint8 digits = data[i];

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

    return QString();
}

QList<OathCredential> OathProtocol::parseCalculateAllResponse(const QByteArray &response)
{
    QList<OathCredential> credentials;

    if (response.length() < 2) {
        return credentials;
    }

    // Check status word
    quint16 sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        return credentials;
    }

    // Parse TLV data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    // CALCULATE ALL response format: NAME (0x71) followed by RESPONSE (0x76) or HOTP (0x77) or TOUCH (0x7c)
    int i = 0;
    while (i < data.length()) {
        if (i + 2 > data.length()) break;

        quint8 tag = data[i++];
        quint8 length = data[i++];

        if (i + length > data.length()) break;

        if (tag == TAG_NAME) { // TAG_NAME = 0x71
            // Parse credential name (no algorithm byte in CALCULATE ALL response)
            QByteArray nameBytes = data.mid(i, length);
            QString name = QString::fromUtf8(nameBytes);

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
                quint8 respTag = data[i++];
                quint8 respLength = data[i++];

                if (i + respLength > data.length()) break;

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
                    quint8 digits = data[i];
                    quint32 codeValue = 0;
                    for (int j = 0; j < 4; ++j) {
                        codeValue = (codeValue << 8) | static_cast<quint8>(data[i + 1 + j]);
                    }

                    QString code = formatCode(data.mid(i, respLength), digits);

                    if (!credentials.isEmpty()) {
                        credentials.last().code = code;

                        // Calculate validity (30-second TOTP period)
                        qint64 currentTime = QDateTime::currentSecsSinceEpoch();
                        qint64 timeInPeriod = currentTime % 30;
                        qint64 validityRemaining = 30 - timeInPeriod;
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

// =============================================================================
// Helper Functions
// =============================================================================

QByteArray OathProtocol::findTlvTag(const QByteArray &data, quint8 tag)
{
    int pos = 0;
    while (pos < data.length() - 1) {
        if (pos + 2 > data.length()) break;

        quint8 currentTag = data[pos];
        quint8 len = data[pos + 1];

        if (pos + 2 + len > data.length()) break;

        if (currentTag == tag) {
            return data.mid(pos + 2, len);
        }

        pos += 2 + len;
    }

    return QByteArray();
}

QByteArray OathProtocol::calculateTotpCounter(int period)
{
    qint64 currentTime = QDateTime::currentSecsSinceEpoch();
    qint64 counter = currentTime / period;

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

    quint8 sw1 = static_cast<quint8>(response[response.length() - 2]);
    quint8 sw2 = static_cast<quint8>(response[response.length() - 1]);

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
        return QString();
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

} // namespace YubiKey
} // namespace KRunner
