/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "virtual_yubikey.h"
#include <QDateTime>
#include <QRandomGenerator>

QByteArray VirtualYubiKey::handleSelect(const QByteArray& apdu)
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

    // TAG_NAME (0x71) - device ID (8 bytes from serial)
    QByteArray deviceIdBytes = QByteArray::fromHex(m_deviceId.toLatin1());
    response.append(static_cast<char>(OathProtocol::TAG_NAME));
    response.append(static_cast<char>(deviceIdBytes.size()));
    response.append(deviceIdBytes);

    // TAG_CHALLENGE (0x74) - if password protected
    if (!m_passwordKey.isEmpty()) {
        // Generate random challenge
        m_lastChallenge = QByteArray::number(QRandomGenerator::global()->generate64(), 16).left(8);
        response.append(static_cast<char>(OathProtocol::TAG_CHALLENGE));
        response.append(static_cast<char>(m_lastChallenge.size()));
        response.append(m_lastChallenge);
    }

    // NOTE: YubiKey does NOT include TAG_SERIAL_NUMBER (0x8F) in SELECT response
    // Serial is retrieved via Management API

    m_sessionActive = true;
    m_authenticated = m_passwordKey.isEmpty(); // Auto-auth if no password

    return createSuccessResponse(response);
}

QByteArray VirtualYubiKey::handleList(const QByteArray& apdu)
{
    // YubiKey LIST (0xA1) may spuriously return 0x6985 (touch required)
    // This emulates the known bug in real YubiKeys
    if (m_emulateListBug && QRandomGenerator::global()->bounded(10) == 0) {
        // 10% chance of spurious touch required error
        return createErrorResponse(0x6985);
    }

    // Check session
    if (!m_sessionActive) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Check authentication
    if (!m_passwordKey.isEmpty() && !m_authenticated) {
        return createErrorResponse(OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED);
    }

    // Build LIST response (LIST v0 - no properties byte)
    QByteArray response;

    for (const auto& cred : m_credentials) {
        // TAG_NAME_LIST (0x72)
        QByteArray nameBytes = cred.originalName.toUtf8();
        response.append(static_cast<char>(OathProtocol::TAG_NAME_LIST));
        response.append(static_cast<char>(nameBytes.size() + 1)); // +1 for type byte

        // Type byte: high nibble = type (0x10=HOTP, 0x20=TOTP), low nibble = algorithm
        quint8 typeByte = (cred.isTotp ? 0x20 : 0x10) | static_cast<quint8>(cred.algorithm);
        response.append(static_cast<char>(typeByte));
        response.append(nameBytes);
    }

    return createSuccessResponse(response);
}

QByteArray VirtualYubiKey::handleCalculate(const QByteArray& apdu)
{
    // YubiKey supports individual CALCULATE (0xA2)
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

    // Check touch requirement
    if (cred.requiresTouch && m_touchPending) {
        return createErrorResponse(0x6985); // Touch required
    }

    // Parse TAG_CHALLENGE
    QByteArray challenge = OathProtocol::findTlvTag(data, OathProtocol::TAG_CHALLENGE);
    if (challenge.isEmpty()) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Calculate timestamp from challenge (big-endian quint64)
    quint64 timestamp = qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(challenge.constData()));

    // Calculate code
    QString code = cred.isTotp
                      ? calculateTotpCode(cred, timestamp)
                      : calculateHotpCode(cred, timestamp);

    // Convert code to BCD format
    QByteArray codeBcd;
    for (int i = 0; i < code.length(); i += 2) {
        quint8 highNibble = code.mid(i, 1).toUInt();
        quint8 lowNibble = (i + 1 < code.length()) ? code.mid(i + 1, 1).toUInt() : 0;
        codeBcd.append(static_cast<char>((highNibble << 4) | lowNibble));
    }

    // Build response
    QByteArray response;
    if (cred.isTotp) {
        // TAG_TOTP_RESPONSE (0x76)
        response.append(static_cast<char>(OathProtocol::TAG_TOTP_RESPONSE));
        response.append(static_cast<char>(codeBcd.size() + 1)); // +1 for digits byte
        response.append(static_cast<char>(cred.digits));
        response.append(codeBcd);
    } else {
        // TAG_HOTP (0x77)
        response.append(static_cast<char>(OathProtocol::TAG_HOTP));
        response.append(static_cast<char>(codeBcd.size() + 1));
        response.append(static_cast<char>(cred.digits));
        response.append(codeBcd);
    }

    return createSuccessResponse(response);
}

QByteArray VirtualYubiKey::handleCalculateAll(const QByteArray& apdu)
{
    // YubiKey primary method: CALCULATE_ALL (0xA4)
    if (apdu.size() < 6) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Check authentication
    if (!m_passwordKey.isEmpty() && !m_authenticated) {
        return createErrorResponse(OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED);
    }

    // Check session
    if (!m_sessionActive) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    QByteArray data = apdu.mid(5);

    // Parse TAG_CHALLENGE
    QByteArray challenge = OathProtocol::findTlvTag(data, OathProtocol::TAG_CHALLENGE);
    if (challenge.isEmpty()) {
        return createErrorResponse(OathProtocol::SW_WRONG_DATA);
    }

    // Calculate timestamp from challenge
    quint64 timestamp = qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(challenge.constData()));

    // Build CALCULATE_ALL response
    QByteArray response;

    for (const auto& cred : m_credentials) {
        // TAG_NAME (0x71)
        QByteArray nameBytes = cred.originalName.toUtf8();
        response.append(static_cast<char>(OathProtocol::TAG_NAME));
        response.append(static_cast<char>(nameBytes.size()));
        response.append(nameBytes);

        // Check touch requirement
        if (cred.requiresTouch && m_touchPending) {
            // TAG_TOUCH (0x7c) - touch required, no code
            response.append(static_cast<char>(OathProtocol::TAG_TOUCH));
            response.append(static_cast<char>(0x00)); // No value
            continue;
        }

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

        // Add code to response
        if (cred.isTotp) {
            // TAG_TOTP_RESPONSE (0x76)
            response.append(static_cast<char>(OathProtocol::TAG_TOTP_RESPONSE));
            response.append(static_cast<char>(codeBcd.size() + 1));
            response.append(static_cast<char>(cred.digits));
            response.append(codeBcd);
        } else {
            // TAG_HOTP (0x77)
            response.append(static_cast<char>(OathProtocol::TAG_HOTP));
            response.append(static_cast<char>(codeBcd.size() + 1));
            response.append(static_cast<char>(cred.digits));
            response.append(codeBcd);
        }
    }

    return createSuccessResponse(response);
}
