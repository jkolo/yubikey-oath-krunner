/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "virtual_nitrokey.h"
#include <QDateTime>
#include <QRandomGenerator>

QByteArray VirtualNitrokey::handleSelect(const QByteArray& apdu)
{
    // Verify SELECT OATH applet command
    if (apdu.size() < 12) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Check AID: A0 00 00 05 27 21 01 (7 bytes)
    QByteArray expectedAid = QByteArray::fromHex("A0000005272101");
    QByteArray receivedAid = apdu.mid(5, 7);

    if (receivedAid != expectedAid) {
        return createErrorResponse(OathProtocol::SW_NO_SUCH_OBJECT);
    }

    // Build SELECT response
    QByteArray response;

    // TAG_VERSION (0x79) - firmware version
    response.append(static_cast<char>(OathProtocol::TAG_VERSION));
    response.append(static_cast<char>(0x03)); // Length
    response.append(static_cast<char>(m_firmwareVersion.major()));
    response.append(static_cast<char>(m_firmwareVersion.minor()));
    response.append(static_cast<char>(m_firmwareVersion.patch()));

    // TAG_NAME (0x71) - device ID
    QByteArray deviceIdBytes = QByteArray::fromHex(m_deviceId.toLatin1());
    response.append(static_cast<char>(OathProtocol::TAG_NAME));
    response.append(static_cast<char>(deviceIdBytes.size()));
    response.append(deviceIdBytes);

    // TAG_SERIAL_NUMBER (0x8F) - Nitrokey includes serial in SELECT (unlike YubiKey)
    response.append(static_cast<char>(OathProtocol::TAG_SERIAL_NUMBER));
    response.append(static_cast<char>(0x04)); // 4 bytes
    response.append(static_cast<char>((m_serialNumber >> 24) & 0xFF));
    response.append(static_cast<char>((m_serialNumber >> 16) & 0xFF));
    response.append(static_cast<char>((m_serialNumber >> 8) & 0xFF));
    response.append(static_cast<char>(m_serialNumber & 0xFF));

    // TAG_CHALLENGE (0x74) - if password protected
    if (!m_passwordKey.isEmpty()) {
        m_lastChallenge = QByteArray::number(QRandomGenerator::global()->generate64(), 16).left(8);
        response.append(static_cast<char>(OathProtocol::TAG_CHALLENGE));
        response.append(static_cast<char>(m_lastChallenge.size()));
        response.append(m_lastChallenge);
    }

    m_sessionActive = true;
    m_authenticated = m_passwordKey.isEmpty();

    return createSuccessResponse(response);
}

QByteArray VirtualNitrokey::handleList(const QByteArray& apdu)
{
    // Check session
    if (!m_sessionActive) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Check authentication
    if (!m_passwordKey.isEmpty() && !m_authenticated) {
        return createErrorResponse(OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED);
    }

    // Build LIST v1 response (includes properties byte)
    QByteArray response;

    for (const auto& cred : m_credentials) {
        // TAG_NAME_LIST (0x72)
        QByteArray nameBytes = cred.originalName.toUtf8();
        response.append(static_cast<char>(OathProtocol::TAG_NAME_LIST));
        response.append(static_cast<char>(nameBytes.size() + 1)); // +1 for type byte

        // Type byte
        quint8 typeByte = (cred.isTotp ? 0x20 : 0x10) | static_cast<quint8>(cred.algorithm);
        response.append(static_cast<char>(typeByte));
        response.append(nameBytes);

        // TAG_PROPERTY (0x78) - CRITICAL: Tag-Value format, NOT TLV!
        // Correct: 78 02 (tag, value)
        // Wrong:   78 01 02 (tag, length, value) - this causes 0x6a80 error
        if (cred.requiresTouch) {
            response.append(static_cast<char>(OathProtocol::TAG_PROPERTY));
            response.append(static_cast<char>(0x02)); // Value directly (touch required bit)
        }
    }

    return createSuccessResponse(response);
}

QByteArray VirtualNitrokey::handleCalculate(const QByteArray& apdu)
{
    // Nitrokey uses individual CALCULATE (0xA2) for each credential
    if (apdu.size() < 6) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Check authentication
    if (!m_passwordKey.isEmpty() && !m_authenticated) {
        return createErrorResponse(OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED);
    }

    QByteArray data = apdu.mid(5);

    // Parse TAG_NAME
    QByteArray nameBytes = OathProtocol::findTlvTag(data, OathProtocol::TAG_NAME);
    if (nameBytes.isEmpty()) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    QString name = QString::fromUtf8(nameBytes);

    // Find credential
    if (!hasCredential(name)) {
        return createErrorResponse(OathProtocol::SW_NO_SUCH_OBJECT);
    }

    const auto& cred = m_credentials[name];

    // Check touch requirement - Nitrokey uses 0x6982 (not 0x6985 like YubiKey)
    if (cred.requiresTouch && m_touchPending) {
        return createErrorResponse(OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED); // 0x6982
    }

    // Parse TAG_CHALLENGE
    QByteArray challenge = OathProtocol::findTlvTag(data, OathProtocol::TAG_CHALLENGE);
    if (challenge.isEmpty()) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Calculate timestamp
    quint64 timestamp = qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(challenge.constData()));

    // Calculate code
    QString code = cred.isTotp
                      ? calculateTotpCode(cred, timestamp)
                      : calculateHotpCode(cred, timestamp);

    // Convert to BCD
    QByteArray codeBcd;
    for (int i = 0; i < code.length(); i += 2) {
        quint8 highNibble = code.mid(i, 1).toUInt();
        quint8 lowNibble = (i + 1 < code.length()) ? code.mid(i + 1, 1).toUInt() : 0;
        codeBcd.append(static_cast<char>((highNibble << 4) | lowNibble));
    }

    // Build response
    QByteArray response;
    if (cred.isTotp) {
        response.append(static_cast<char>(OathProtocol::TAG_TOTP_RESPONSE));
        response.append(static_cast<char>(codeBcd.size() + 1));
        response.append(static_cast<char>(cred.digits));
        response.append(codeBcd);
    } else {
        response.append(static_cast<char>(OathProtocol::TAG_HOTP));
        response.append(static_cast<char>(codeBcd.size() + 1));
        response.append(static_cast<char>(cred.digits));
        response.append(codeBcd);
    }

    return createSuccessResponse(response);
}

QByteArray VirtualNitrokey::handleCalculateAll(const QByteArray& apdu)
{
    // Nitrokey 3 does NOT support CALCULATE_ALL (0xA4)
    // Returns 0x6D00 (INS not supported)
    Q_UNUSED(apdu);
    return createErrorResponse(OathProtocol::SW_INS_NOT_SUPPORTED);
}
