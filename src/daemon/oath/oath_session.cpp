/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_session.h"
#include "../logging_categories.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

OathSession::OathSession(SCARDHANDLE cardHandle,
                         DWORD protocol,
                         const QString &deviceId,
                         QObject *parent)
    : QObject(parent)
    , m_cardHandle(cardHandle)
    , m_protocol(protocol)
    , m_deviceId(deviceId)
{
    qCDebug(YubiKeyOathDeviceLog) << "OathSession created for device" << m_deviceId;
}

OathSession::~OathSession()
{
    qCDebug(YubiKeyOathDeviceLog) << "OathSession destroyed for device" << m_deviceId;
    // Note: We do NOT disconnect card handle - caller owns it
}

// =============================================================================
// PC/SC Communication
// =============================================================================

QByteArray OathSession::sendApdu(const QByteArray &command)
{
    qCDebug(YubiKeyOathDeviceLog) << "sendApdu() for device:" << m_deviceId
             << "command length:" << command.length() << "command:" << command.toHex();

    if (m_cardHandle == 0) {
        qCDebug(YubiKeyOathDeviceLog) << "Device" << m_deviceId << "not connected (invalid handle)";
        return QByteArray();
    }

    SCARD_IO_REQUEST pioSendPci;
    pioSendPci.dwProtocol = m_protocol;
    pioSendPci.cbPciLength = sizeof(SCARD_IO_REQUEST);

    BYTE response[4096];
    DWORD responseLen = sizeof(response);

    qCDebug(YubiKeyOathDeviceLog) << "Transmitting APDU, protocol:" << m_protocol
             << "command length:" << command.length();

    LONG result = SCardTransmit(m_cardHandle, &pioSendPci,
                               (BYTE*)command.constData(), command.length(),
                               nullptr, response, &responseLen);

    qCDebug(YubiKeyOathDeviceLog) << "SCardTransmit result:" << result
             << "response length:" << responseLen;

    if (result != SCARD_S_SUCCESS) {
        qCDebug(YubiKeyOathDeviceLog) << "Failed to send APDU, error code:" << QString::number(result, 16);

        // Check if card was removed/disconnected
        if (result == SCARD_W_REMOVED_CARD ||
            result == SCARD_E_NO_SMARTCARD ||
            result == SCARD_W_RESET_CARD) {
            qCDebug(YubiKeyOathDeviceLog) << "Device" << m_deviceId << "was removed or disconnected";
            // Mark session as inactive when card is removed/reset
            m_sessionActive = false;
        }

        Q_EMIT errorOccurred(tr("Failed to send APDU: 0x%1").arg(result, 0, 16));
        return QByteArray();
    }

    QByteArray responseData((char*)response, responseLen);
    qCDebug(YubiKeyOathDeviceLog) << "APDU response:" << responseData.toHex();

    // Handle chained responses (0x61XX = more data available)
    // Accumulate all data parts into single response
    QByteArray fullData;

    while (responseLen >= 2) {
        quint8 sw1 = response[responseLen - 2];
        quint8 sw2 = response[responseLen - 1];

        // Accumulate data (without status word)
        fullData.append((char*)response, responseLen - 2);

        // Check if more data is available
        if (sw1 == 0x61) {
            qCDebug(YubiKeyOathDeviceLog) << "More data available (0x61" << QString::number(sw2, 16)
                     << "), sending SEND REMAINING";

            // Use OATH-specific SEND REMAINING (0xA5)
            QByteArray sendRemCmd = OathProtocol::createSendRemainingCommand();

            qCDebug(YubiKeyOathDeviceLog) << "Sending SEND REMAINING:" << sendRemCmd.toHex();

            responseLen = sizeof(response);
            result = SCardTransmit(m_cardHandle, &pioSendPci,
                                 (BYTE*)sendRemCmd.constData(), sendRemCmd.length(),
                                 nullptr, response, &responseLen);

            if (result != SCARD_S_SUCCESS) {
                qCDebug(YubiKeyOathDeviceLog) << "SEND REMAINING failed:" << QString::number(result, 16);
                break;
            }

            qCDebug(YubiKeyOathDeviceLog) << "SEND REMAINING received" << responseLen << "bytes";

            // Log the response hex for debugging
            if (responseLen > 0) {
                QByteArray sendRemData((char*)response, responseLen);
                qCDebug(YubiKeyOathDeviceLog) << "SEND REMAINING data:" << sendRemData.toHex();
            }
        } else {
            // No more data, append final status word and break
            fullData.append((char)sw1);
            fullData.append((char)sw2);
            break;
        }
    }

    qCDebug(YubiKeyOathDeviceLog) << "Final response length:" << fullData.length() << "bytes";
    return fullData;
}

// =============================================================================
// High-level OATH Operations
// =============================================================================

Result<void> OathSession::selectOathApplication(QByteArray &outChallenge)
{
    qCDebug(YubiKeyOathDeviceLog) << "selectOathApplication() for device" << m_deviceId;

    QByteArray command = OathProtocol::createSelectCommand();
    QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from SELECT";
        m_sessionActive = false;  // Mark session as inactive on error
        return Result<void>::error(tr("Failed to select OATH application"));
    }

    // Parse response to get device ID and challenge
    QString deviceId;
    if (!OathProtocol::parseSelectResponse(response, deviceId, outChallenge)) {
        qCDebug(YubiKeyOathDeviceLog) << "Failed to parse SELECT response";
        m_sessionActive = false;  // Mark session as inactive on error
        return Result<void>::error(tr("Failed to parse SELECT response"));
    }

    // Update device ID if we got one from response
    if (!deviceId.isEmpty()) {
        m_deviceId = deviceId;
    }

    qCDebug(YubiKeyOathDeviceLog) << "SELECT successful, device ID:" << m_deviceId
             << "challenge:" << outChallenge.toHex();

    // Mark session as active after successful SELECT
    m_sessionActive = true;

    return Result<void>::success();
}

Result<QString> OathSession::calculateCode(const QString &name)
{
    qCDebug(YubiKeyOathDeviceLog) << "calculateCode() for" << name << "on device" << m_deviceId;

    // Ensure OATH session is active before sending CALCULATE
    if (!m_sessionActive) {
        qCDebug(YubiKeyOathDeviceLog) << "Session not active, executing SELECT first";
        QByteArray challenge;
        auto selectResult = selectOathApplication(challenge);
        if (selectResult.isError()) {
            qCDebug(YubiKeyOathDeviceLog) << "SELECT failed:" << selectResult.error();
            return Result<QString>::error(tr("Failed to establish OATH session: %1").arg(selectResult.error()));
        }
        qCDebug(YubiKeyOathDeviceLog) << "Session established successfully";
    }

    // Create challenge from current time
    QByteArray challenge = OathProtocol::createTotpChallenge();

    QByteArray command = OathProtocol::createCalculateCommand(name, challenge);
    QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from CALCULATE";
        return Result<QString>::error(tr("Failed to communicate with YubiKey"));
    }

    // Check for touch required and authentication before parsing
    quint16 sw = OathProtocol::getStatusWord(response);
    if (sw == 0x6985) {
        qCDebug(YubiKeyOathDeviceLog) << "Touch required (SW=6985)";
        Q_EMIT touchRequired();
        return Result<QString>::error(tr("Touch required"));
    }
    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Password required for CALCULATE (SW=6982)";
        return Result<QString>::error(tr("Password required"));
    }

    QString code = OathProtocol::parseCode(response);
    if (code.isEmpty()) {
        return Result<QString>::error(tr("Failed to parse TOTP code from response"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Generated code:" << code;
    return Result<QString>::success(code);
}

Result<QList<OathCredential>> OathSession::calculateAll()
{
    qCDebug(YubiKeyOathDeviceLog) << "calculateAll() for device" << m_deviceId;

    // Ensure OATH session is active before sending CALCULATE ALL
    if (!m_sessionActive) {
        qCDebug(YubiKeyOathDeviceLog) << "Session not active, executing SELECT first";
        QByteArray selectChallenge;
        auto selectResult = selectOathApplication(selectChallenge);
        if (selectResult.isError()) {
            qCDebug(YubiKeyOathDeviceLog) << "SELECT failed:" << selectResult.error();
            return Result<QList<OathCredential>>::error(tr("Failed to establish OATH session: %1").arg(selectResult.error()));
        }
        qCDebug(YubiKeyOathDeviceLog) << "Session established successfully";
    }

    // Create challenge from current time
    QByteArray challenge = OathProtocol::createTotpChallenge();

    QByteArray command = OathProtocol::createCalculateAllCommand(challenge);
    QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from CALCULATE ALL";
        return Result<QList<OathCredential>>::error(tr("Failed to calculate codes"));
    }

    // Check status word for authentication requirement
    quint16 sw = OathProtocol::getStatusWord(response);
    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Password required for CALCULATE ALL";
        return Result<QList<OathCredential>>::error(tr("Password required"));
    }

    QList<OathCredential> credentials = OathProtocol::parseCalculateAllResponse(response);

    // Set device ID for all credentials
    for (auto &cred : credentials) {
        cred.deviceId = m_deviceId;
    }

    qCDebug(YubiKeyOathDeviceLog) << "Calculated codes for" << credentials.size() << "credentials";
    return Result<QList<OathCredential>>::success(credentials);
}

Result<void> OathSession::authenticate(const QString &password, const QString &deviceId)
{
    qCDebug(YubiKeyOathDeviceLog) << "authenticate() for device" << m_deviceId;

    // STEP 1: Execute SELECT to get fresh challenge from YubiKey
    // YubiKey does NOT maintain challenge state between VALIDATE commands
    qCDebug(YubiKeyOathDeviceLog) << "Executing SELECT to obtain fresh challenge";

    QByteArray freshChallenge;
    auto selectResult = selectOathApplication(freshChallenge);
    if (selectResult.isError()) {
        qCDebug(YubiKeyOathDeviceLog) << "SELECT failed:" << selectResult.error();
        return selectResult;
    }

    if (freshChallenge.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "No challenge in SELECT response";
        return Result<void>::error(tr("No challenge received from YubiKey"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Fresh challenge from SELECT:" << freshChallenge.toHex();

    // STEP 2: Derive key from password using PBKDF2
    QByteArray salt = QByteArray::fromHex(deviceId.toLatin1());
    QByteArray key = deriveKeyPbkdf2(
        password.toUtf8(),
        salt,
        1000, // iterations
        16    // key length
    );

    qCDebug(YubiKeyOathDeviceLog) << "Derived key from password, salt:" << salt.toHex()
             << "key:" << key.toHex();

    // STEP 3: Calculate HMAC-SHA1 response using fresh challenge
    QByteArray hmacResponse = QMessageAuthenticationCode::hash(
        freshChallenge,
        key,
        QCryptographicHash::Sha1
    );

    qCDebug(YubiKeyOathDeviceLog) << "HMAC response for fresh challenge:" << hmacResponse.toHex();

    // STEP 4: Create and send VALIDATE command
    // Generate our challenge for mutual authentication
    QByteArray ourChallenge;
    for (int i = 0; i < 8; ++i) {
        ourChallenge.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }
    qCDebug(YubiKeyOathDeviceLog) << "Generated our challenge for VALIDATE:" << ourChallenge.toHex();

    QByteArray command = OathProtocol::createValidateCommand(hmacResponse, ourChallenge);
    QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from VALIDATE";
        return Result<void>::error(tr("Authentication failed - no response"));
    }

    // STEP 5: Check status word
    quint16 sw = OathProtocol::getStatusWord(response);

    qCDebug(YubiKeyOathDeviceLog) << "VALIDATE status word:" << QString::number(sw, 16);

    if (sw == OathProtocol::SW_OK) {
        qCDebug(YubiKeyOathDeviceLog) << "Authentication successful";

        // STEP 6: Verify YubiKey's response (optional but recommended)
        QByteArray responseTag = OathProtocol::findTlvTag(
            response.left(response.length() - 2),
            OathProtocol::TAG_RESPONSE
        );

        if (!responseTag.isEmpty()) {
            qCDebug(YubiKeyOathDeviceLog) << "YubiKey response to our challenge:" << responseTag.toHex();

            QByteArray expectedResponse = QMessageAuthenticationCode::hash(
                ourChallenge,
                key,
                QCryptographicHash::Sha1
            );

            if (responseTag == expectedResponse) {
                qCDebug(YubiKeyOathDeviceLog) << "YubiKey response verified successfully";
            } else {
                qCDebug(YubiKeyOathDeviceLog) << "YubiKey response verification failed (expected:"
                         << expectedResponse.toHex() << ")";
            }
        }

        return Result<void>::success();
    } else if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Wrong password";
        return Result<void>::error(tr("Wrong password"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Authentication failed with unknown error";
    return Result<void>::error(tr("Authentication failed"));
}

Result<void> OathSession::putCredential(const OathCredentialData &data)
{
    qCDebug(YubiKeyOathDeviceLog) << "putCredential() for device" << m_deviceId
                                   << "credential:" << data.name;

    // Validate credential data
    QString validationError = data.validate();
    if (!validationError.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Invalid credential data:" << validationError;
        return Result<void>::error(validationError);
    }

    // Create PUT command
    QByteArray command = OathProtocol::createPutCommand(data);
    if (command.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to create PUT command";
        return Result<void>::error(tr("Failed to encode credential data"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Sending PUT command, length:" << command.length();

    // Send command
    QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty response from PUT command";
        return Result<void>::error(tr("No response from YubiKey"));
    }

    // Check status word
    quint16 sw = OathProtocol::getStatusWord(response);
    qCDebug(YubiKeyOathDeviceLog) << "PUT status word:" << QString::number(sw, 16);

    if (sw == OathProtocol::SW_SUCCESS) {
        qCDebug(YubiKeyOathDeviceLog) << "Credential added successfully";
        return Result<void>::success();
    }

    // Handle specific error cases
    if (sw == OathProtocol::SW_INSUFFICIENT_SPACE) {
        qCWarning(YubiKeyOathDeviceLog) << "Insufficient space on YubiKey";
        return Result<void>::error(tr("Insufficient space on YubiKey"));
    }

    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCWarning(YubiKeyOathDeviceLog) << "Authentication required";
        return Result<void>::error(tr("Authentication required - YubiKey is password protected"));
    }

    if (sw == OathProtocol::SW_WRONG_DATA) {
        qCWarning(YubiKeyOathDeviceLog) << "Wrong data format";
        return Result<void>::error(tr("Invalid credential data format"));
    }

    // Unknown error
    qCWarning(YubiKeyOathDeviceLog) << "PUT failed with status word:" << QString::number(sw, 16);
    return Result<void>::error(tr("Failed to add credential (error code: 0x%1)")
                                .arg(sw, 4, 16, QLatin1Char('0')));
}

Result<void> OathSession::deleteCredential(const QString &name)
{
    qCDebug(YubiKeyOathDeviceLog) << "deleteCredential() for device" << m_deviceId
                                   << "credential:" << name;

    if (name.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty credential name";
        return Result<void>::error(tr("Credential name cannot be empty"));
    }

    // Create DELETE command
    QByteArray command = OathProtocol::createDeleteCommand(name);
    if (command.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to create DELETE command";
        return Result<void>::error(tr("Failed to encode credential name"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Sending DELETE command, length:" << command.length();

    // Send command
    QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty response from DELETE command";
        return Result<void>::error(tr("No response from YubiKey"));
    }

    // Check status word
    quint16 sw = OathProtocol::getStatusWord(response);
    qCDebug(YubiKeyOathDeviceLog) << "DELETE status word:" << QString::number(sw, 16);

    if (sw == OathProtocol::SW_SUCCESS) {
        qCDebug(YubiKeyOathDeviceLog) << "Credential deleted successfully";
        return Result<void>::success();
    }

    // Handle specific error cases
    if (sw == OathProtocol::SW_NO_SUCH_OBJECT) {
        qCWarning(YubiKeyOathDeviceLog) << "Credential not found";
        return Result<void>::error(tr("Credential not found on YubiKey"));
    }

    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCWarning(YubiKeyOathDeviceLog) << "Authentication required";
        return Result<void>::error(tr("Authentication required - YubiKey is password protected"));
    }

    if (sw == OathProtocol::SW_WRONG_DATA) {
        qCWarning(YubiKeyOathDeviceLog) << "Wrong data format";
        return Result<void>::error(tr("Invalid credential name format"));
    }

    // Unknown error
    qCWarning(YubiKeyOathDeviceLog) << "DELETE failed with status word:" << QString::number(sw, 16);
    return Result<void>::error(tr("Failed to delete credential (error code: 0x%1)")
                                .arg(sw, 4, 16, QLatin1Char('0')));
}

void OathSession::cancelOperation()
{
    qCDebug(YubiKeyOathDeviceLog) << "cancelOperation() for device" << m_deviceId;

    // Send SELECT command to reset device state
    QByteArray command = OathProtocol::createSelectCommand();
    sendApdu(command);

    // Mark session as inactive after reset (caller should re-establish session)
    m_sessionActive = false;
    qCDebug(YubiKeyOathDeviceLog) << "Session marked as inactive after cancel operation";
}

// =============================================================================
// Helper Functions
// =============================================================================

QByteArray OathSession::deriveKeyPbkdf2(const QByteArray &password,
                                       const QByteArray &salt,
                                       int iterations,
                                       int keyLength)
{
    QByteArray derivedKey;
    int blockCount = (keyLength + 19) / 20; // SHA1 produces 20 bytes per block

    for (int block = 1; block <= blockCount; ++block) {
        // Create block salt: salt || INT(block)
        QByteArray blockSalt = salt;
        blockSalt.append(static_cast<char>((block >> 24) & 0xFF));
        blockSalt.append(static_cast<char>((block >> 16) & 0xFF));
        blockSalt.append(static_cast<char>((block >> 8) & 0xFF));
        blockSalt.append(static_cast<char>(block & 0xFF));

        // U1 = PRF(password, salt || INT(block))
        QByteArray U = QMessageAuthenticationCode::hash(blockSalt, password, QCryptographicHash::Sha1);
        QByteArray result = U;

        // U2..Uc = PRF(password, U{c-1})
        for (int i = 1; i < iterations; ++i) {
            U = QMessageAuthenticationCode::hash(U, password, QCryptographicHash::Sha1);

            // XOR with result
            for (int j = 0; j < U.length(); ++j) {
                result[j] = result[j] ^ U[j];
            }
        }

        derivedKey.append(result);
    }

    return derivedKey.left(keyLength);
}

} // namespace Daemon
} // namespace YubiKeyOath
