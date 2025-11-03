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
#include <QEventLoop>
#include <QTimer>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

// PC/SC error codes not always defined in headers
#ifndef SCARD_W_RESET_CARD
#define SCARD_W_RESET_CARD ((LONG)0x80100068)
#endif

OathSession::OathSession(SCARDHANDLE cardHandle,
                         DWORD protocol,
                         const QString &deviceId, // NOLINT(modernize-pass-by-value) - const ref for consistency
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

QByteArray OathSession::sendApdu(const QByteArray &command, int retryCount)
{
    qCDebug(YubiKeyOathDeviceLog) << "sendApdu() for device:" << m_deviceId
             << "command length:" << command.length() << "command:" << command.toHex()
             << "retryCount:" << retryCount;

    if (m_cardHandle == 0) {
        qCDebug(YubiKeyOathDeviceLog) << "Device" << m_deviceId << "not connected (invalid handle)";
        return {};
    }

    SCARD_IO_REQUEST pioSendPci;
    pioSendPci.dwProtocol = m_protocol;
    pioSendPci.cbPciLength = sizeof(SCARD_IO_REQUEST);

    BYTE response[4096]; // NOLINT(cppcoreguidelines-avoid-c-arrays) - PC/SC API requirement
    DWORD responseLen = sizeof(response);

    qCDebug(YubiKeyOathDeviceLog) << "Transmitting APDU, protocol:" << m_protocol
             << "command length:" << command.length();

    LONG result = SCardTransmit(m_cardHandle, &pioSendPci,
                               reinterpret_cast<const BYTE*>(command.constData()), command.length(),
                               nullptr, static_cast<BYTE*>(response), &responseLen); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

    qCDebug(YubiKeyOathDeviceLog) << "SCardTransmit result:" << result
             << "response length:" << responseLen;

    if (result != SCARD_S_SUCCESS) {
        qCDebug(YubiKeyOathDeviceLog) << "Failed to send APDU, error code:" << QString::number(result, 16);

        // Handle card reset - emit signal and wait for reconnect result
        if (result == SCARD_W_RESET_CARD && retryCount == 0) {
            qCWarning(YubiKeyOathDeviceLog) << "Card reset detected (SCARD_W_RESET_CARD), emitting signal and waiting for reconnect";
            m_sessionActive = false;

            // Emit signal to trigger reconnect workflow in upper layers
            Q_EMIT cardResetDetected(command);

            // Wait for reconnect result using QEventLoop
            QEventLoop loop;
            bool reconnectSuccess = false;

            // Connect to signals from upper layer (YubiKeyOathDevice)
            const QMetaObject::Connection connReady = connect(this, &OathSession::reconnectReady, &loop, [&]() {
                qCInfo(YubiKeyOathDeviceLog) << "Received reconnectReady signal";
                reconnectSuccess = true;
                loop.quit();
            });

            const QMetaObject::Connection connFailed = connect(this, &OathSession::reconnectFailed, &loop, [&]() {
                qCWarning(YubiKeyOathDeviceLog) << "Received reconnectFailed signal";
                reconnectSuccess = false;
                loop.quit();
            });

            // Set timeout (10 seconds) to prevent infinite waiting
            QTimer::singleShot(10000, &loop, [&]() {
                qCWarning(YubiKeyOathDeviceLog) << "Reconnect timeout after 10 seconds";
                reconnectSuccess = false;
                loop.quit();
            });

            qCDebug(YubiKeyOathDeviceLog) << "Waiting for reconnect result...";
            loop.exec(); // Block here until signal or timeout

            // Disconnect temporary connections
            disconnect(connReady);
            disconnect(connFailed);

            if (reconnectSuccess) {
                qCInfo(YubiKeyOathDeviceLog) << "Reconnect successful, retrying APDU";
                // Retry the command with incremented retry count to prevent infinite recursion
                return sendApdu(command, retryCount + 1);
            } else {
                qCWarning(YubiKeyOathDeviceLog) << "Reconnect failed or timeout, cannot retry APDU";
                Q_EMIT errorOccurred(tr("Card reset and reconnect failed"));
                return {};
            }
        }

        // Check if card was removed/disconnected (non-recoverable errors)
        if (result == SCARD_W_REMOVED_CARD ||
            result == SCARD_E_NO_SMARTCARD ||
            result == SCARD_W_RESET_CARD) {
            qCDebug(YubiKeyOathDeviceLog) << "Device" << m_deviceId << "was removed, disconnected, or reset (after retry)";
            // Mark session as inactive when card is removed/reset
            m_sessionActive = false;
        }

        Q_EMIT errorOccurred(tr("Failed to send APDU: 0x%1").arg(result, 0, 16));
        return {};
    }

    const QByteArray responseData(reinterpret_cast<const char*>(response), static_cast<qsizetype>(responseLen));
    qCDebug(YubiKeyOathDeviceLog) << "APDU response:" << responseData.toHex();

    // Handle chained responses (0x61XX = more data available)
    // Accumulate all data parts into single response
    QByteArray fullData;

    while (responseLen >= 2) {
        const quint8 sw1 = response[responseLen - 2];
        const quint8 sw2 = response[responseLen - 1];

        // Accumulate data (without status word)
        fullData.append(reinterpret_cast<const char*>(response), static_cast<qsizetype>(responseLen - 2));

        // Check if more data is available
        if (sw1 == 0x61) {
            qCDebug(YubiKeyOathDeviceLog) << "More data available (0x61" << QString::number(sw2, 16)
                     << "), sending SEND REMAINING";

            // Use OATH-specific SEND REMAINING (0xA5)
            const QByteArray sendRemCmd = OathProtocol::createSendRemainingCommand();

            qCDebug(YubiKeyOathDeviceLog) << "Sending SEND REMAINING:" << sendRemCmd.toHex();

            responseLen = sizeof(response);
            result = SCardTransmit(m_cardHandle, &pioSendPci,
                                 reinterpret_cast<const BYTE*>(sendRemCmd.constData()), sendRemCmd.length(),
                                 nullptr, static_cast<BYTE*>(response), &responseLen); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

            if (result != SCARD_S_SUCCESS) {
                qCDebug(YubiKeyOathDeviceLog) << "SEND REMAINING failed:" << QString::number(result, 16);
                break;
            }

            qCDebug(YubiKeyOathDeviceLog) << "SEND REMAINING received" << responseLen << "bytes";

            // Log the response hex for debugging
            if (responseLen > 0) {
                const QByteArray sendRemData(reinterpret_cast<const char*>(response), static_cast<qsizetype>(responseLen));
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

    const QByteArray command = OathProtocol::createSelectCommand();
    const QByteArray response = sendApdu(command);

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

Result<QString> OathSession::calculateCode(const QString &name, int period)
{
    qCDebug(YubiKeyOathDeviceLog) << "calculateCode() for" << name << "on device" << m_deviceId
                                   << "with period" << period;

    // Ensure OATH session is active (reactivate if needed after external app interaction)
    auto sessionResult = ensureSessionActive();
    if (sessionResult.isError()) {
        return Result<QString>::error(sessionResult.error());
    }

    // Retry loop for session loss recovery
    for (int attempt = 0; attempt < 2; ++attempt) {
        // Create challenge from current time with specified period
        const QByteArray challenge = OathProtocol::createTotpChallenge(period);

        const QByteArray command = OathProtocol::createCalculateCommand(name, challenge);
        const QByteArray response = sendApdu(command);

        if (response.isEmpty()) {
            qCDebug(YubiKeyOathDeviceLog) << "Empty response from CALCULATE";
            return Result<QString>::error(tr("Failed to communicate with YubiKey"));
        }

        // Check status word
        const quint16 sw = OathProtocol::getStatusWord(response);

        // Check for session loss (applet not selected) - retry once
        if (sw == OathProtocol::SW_INS_NOT_SUPPORTED || sw == OathProtocol::SW_CLA_NOT_SUPPORTED) {
            qCWarning(YubiKeyOathDeviceLog) << "Session lost (SW=" << QString::number(sw, 16)
                                            << "), attempt" << (attempt + 1) << "of 2";
            m_sessionActive = false;

            if (attempt == 0) {
                // Retry after reactivating session
                auto reactivateResult = ensureSessionActive();
                if (reactivateResult.isError()) {
                    return Result<QString>::error(tr("Failed to reactivate session: %1").arg(reactivateResult.error()));
                }
                continue; // Retry operation
            } else {
                return Result<QString>::error(tr("Session lost and retry failed"));
            }
        }

        // Check for touch required
        if (sw == 0x6985) {
            qCDebug(YubiKeyOathDeviceLog) << "Touch required (SW=6985)";
            Q_EMIT touchRequired();
            return Result<QString>::error(tr("Touch required"));
        }

        // Check for authentication required
        if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
            qCDebug(YubiKeyOathDeviceLog) << "Password required for CALCULATE (SW=6982)";
            return Result<QString>::error(tr("Password required"));
        }

        // Parse code
        const QString code = OathProtocol::parseCode(response);
        if (code.isEmpty()) {
            return Result<QString>::error(tr("Failed to parse TOTP code from response"));
        }

        qCDebug(YubiKeyOathDeviceLog) << "Generated code:" << code;
        return Result<QString>::success(code);
    }

    // Should never reach here
    return Result<QString>::error(tr("Unexpected error in calculateCode"));
}

Result<QList<OathCredential>> OathSession::calculateAll()
{
    qCDebug(YubiKeyOathDeviceLog) << "calculateAll() for device" << m_deviceId;

    // Ensure OATH session is active (reactivate if needed after external app interaction)
    auto sessionResult = ensureSessionActive();
    if (sessionResult.isError()) {
        return Result<QList<OathCredential>>::error(sessionResult.error());
    }

    // Retry loop for session loss recovery
    for (int attempt = 0; attempt < 2; ++attempt) {
        // Create challenge from current time
        const QByteArray challenge = OathProtocol::createTotpChallenge();

        const QByteArray command = OathProtocol::createCalculateAllCommand(challenge);
        const QByteArray response = sendApdu(command);

        if (response.isEmpty()) {
            qCDebug(YubiKeyOathDeviceLog) << "Empty response from CALCULATE ALL";
            return Result<QList<OathCredential>>::error(tr("Failed to calculate codes"));
        }

        // Check status word
        const quint16 sw = OathProtocol::getStatusWord(response);

        // Check for session loss (applet not selected) - retry once
        if (sw == OathProtocol::SW_INS_NOT_SUPPORTED || sw == OathProtocol::SW_CLA_NOT_SUPPORTED) {
            qCWarning(YubiKeyOathDeviceLog) << "Session lost (SW=" << QString::number(sw, 16)
                                            << "), attempt" << (attempt + 1) << "of 2";
            m_sessionActive = false;

            if (attempt == 0) {
                // Retry after reactivating session
                auto reactivateResult = ensureSessionActive();
                if (reactivateResult.isError()) {
                    return Result<QList<OathCredential>>::error(tr("Failed to reactivate session: %1").arg(reactivateResult.error()));
                }
                continue; // Retry operation
            } else {
                return Result<QList<OathCredential>>::error(tr("Session lost and retry failed"));
            }
        }

        // Check for authentication requirement
        if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
            qCDebug(YubiKeyOathDeviceLog) << "Password required for CALCULATE ALL";
            return Result<QList<OathCredential>>::error(tr("Password required"));
        }

        // Parse response
        QList<OathCredential> credentials = OathProtocol::parseCalculateAllResponse(response);

        // Set device ID for all credentials
        for (auto &cred : credentials) {
            cred.deviceId = m_deviceId;
        }

        qCDebug(YubiKeyOathDeviceLog) << "Calculated codes for" << credentials.size() << "credentials";
        return Result<QList<OathCredential>>::success(credentials);
    }

    // Should never reach here
    return Result<QList<OathCredential>>::error(tr("Unexpected error in calculateAll"));
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
    const QByteArray salt = QByteArray::fromHex(deviceId.toLatin1());
    const QByteArray key = deriveKeyPbkdf2(
        password.toUtf8(),
        salt,
        1000, // iterations
        16    // key length
    );

    qCDebug(YubiKeyOathDeviceLog) << "Derived key from password, salt:" << salt.toHex()
             << "key:" << key.toHex();

    // STEP 3: Calculate HMAC-SHA1 response using fresh challenge
    const QByteArray hmacResponse = QMessageAuthenticationCode::hash(
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

    const QByteArray command = OathProtocol::createValidateCommand(hmacResponse, ourChallenge);
    const QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from VALIDATE";
        return Result<void>::error(tr("Authentication failed - no response"));
    }

    // STEP 5: Check status word
    const quint16 sw = OathProtocol::getStatusWord(response);

    qCDebug(YubiKeyOathDeviceLog) << "VALIDATE status word:" << QString::number(sw, 16);

    if (sw == OathProtocol::SW_OK) {
        qCDebug(YubiKeyOathDeviceLog) << "Authentication successful";

        // STEP 6: Verify YubiKey's response (optional but recommended)
        const QByteArray responseTag = OathProtocol::findTlvTag(
            response.left(response.length() - 2),
            OathProtocol::TAG_RESPONSE
        );

        if (!responseTag.isEmpty()) {
            qCDebug(YubiKeyOathDeviceLog) << "YubiKey response to our challenge:" << responseTag.toHex();

            const QByteArray expectedResponse = QMessageAuthenticationCode::hash(
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
    const QString validationError = data.validate();
    if (!validationError.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Invalid credential data:" << validationError;
        return Result<void>::error(validationError);
    }

    // Create PUT command
    const QByteArray command = OathProtocol::createPutCommand(data);
    if (command.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to create PUT command";
        return Result<void>::error(tr("Failed to encode credential data"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Sending PUT command, length:" << command.length();

    // Send command
    const QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty response from PUT command";
        return Result<void>::error(tr("No response from YubiKey"));
    }

    // Check status word
    const quint16 sw = OathProtocol::getStatusWord(response);
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
    const QByteArray command = OathProtocol::createDeleteCommand(name);
    if (command.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to create DELETE command";
        return Result<void>::error(tr("Failed to encode credential name"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Sending DELETE command, length:" << command.length();

    // Send command
    const QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty response from DELETE command";
        return Result<void>::error(tr("No response from YubiKey"));
    }

    // Check status word
    const quint16 sw = OathProtocol::getStatusWord(response);
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

Result<void> OathSession::setPassword(const QString &newPassword, const QString &deviceId)
{
    qCDebug(YubiKeyOathDeviceLog) << "setPassword() for device" << m_deviceId;

    if (newPassword.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty new password";
        return Result<void>::error(tr("Password cannot be empty"));
    }

    if (deviceId.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty device ID";
        return Result<void>::error(tr("Device ID required for password derivation"));
    }

    // Execute SELECT to ensure OATH application is selected before SET_CODE
    qCDebug(YubiKeyOathDeviceLog) << "Executing SELECT before SET_CODE";
    QByteArray selectChallenge;
    auto selectResult = selectOathApplication(selectChallenge);
    if (selectResult.isError()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to SELECT OATH application:" << selectResult.error();
        return Result<void>::error(tr("Failed to select OATH application: %1").arg(selectResult.error()));
    }

    // Derive key from password using PBKDF2
    QByteArray const key = deriveKeyPbkdf2(
        newPassword.toUtf8(),
        QByteArray::fromHex(deviceId.toUtf8()),
        1000,  // iterations
        16     // key length
    );

    if (key.length() != 16) {
        qCWarning(YubiKeyOathDeviceLog) << "PBKDF2 failed to derive 16-byte key";
        return Result<void>::error(tr("Failed to derive encryption key"));
    }

    // Generate random challenge for mutual authentication
    QByteArray challenge(8, Qt::Uninitialized);
    for (int i = 0; i < 8; ++i) {
        challenge[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }

    // Calculate HMAC-SHA1 response to our challenge
    QByteArray const response = QMessageAuthenticationCode::hash(
        challenge,
        key,
        QCryptographicHash::Sha1
    );

    qCDebug(YubiKeyOathDeviceLog) << "Generated challenge and response for SET_CODE";

    // Create SET_CODE command
    const QByteArray command = OathProtocol::createSetCodeCommand(key, challenge, response);
    if (command.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to create SET_CODE command";
        return Result<void>::error(tr("Failed to create SET_CODE command"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Sending SET_CODE command, length:" << command.length();

    // Send command
    const QByteArray apduResponse = sendApdu(command);

    if (apduResponse.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty response from SET_CODE command";
        return Result<void>::error(tr("No response from YubiKey"));
    }

    // Parse response
    QByteArray verificationResponse;
    bool const success = OathProtocol::parseSetCodeResponse(apduResponse, verificationResponse);

    if (!success) {
        quint16 const sw = OathProtocol::getStatusWord(apduResponse);
        qCWarning(YubiKeyOathDeviceLog) << "SET_CODE failed with status word:" << QString::number(sw, 16);

        if (sw == 0x6984) {
            return Result<void>::error(tr("Password verification failed - wrong old password"));
        }
        if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
            return Result<void>::error(tr("Authentication required - authenticate with old password first"));
        }
        return Result<void>::error(tr("Failed to set password (error code: 0x%1)")
                                    .arg(sw, 4, 16, QLatin1Char('0')));
    }

    // Verify YubiKey's response to our challenge (mutual authentication)
    QByteArray const expectedResponse = QMessageAuthenticationCode::hash(
        challenge,
        key,
        QCryptographicHash::Sha1
    );

    if (!verificationResponse.isEmpty() && verificationResponse != expectedResponse) {
        qCWarning(YubiKeyOathDeviceLog) << "YubiKey response verification failed";
        return Result<void>::error(tr("YubiKey authentication verification failed"));
    }

    qCInfo(YubiKeyOathDeviceLog) << "Password set successfully on device" << m_deviceId;
    return Result<void>::success();
}

Result<void> OathSession::removePassword()
{
    qCDebug(YubiKeyOathDeviceLog) << "removePassword() for device" << m_deviceId;

    // Create SET_CODE command with length 0 (removes password)
    // Note: This command relies on the existing authenticated session from earlier VALIDATE
    // Do NOT call SELECT here as it would reset the authentication session
    const QByteArray command = OathProtocol::createRemoveCodeCommand();

    qCDebug(YubiKeyOathDeviceLog) << "Sending REMOVE_CODE command (SET_CODE with Lc=0)";

    // Send command
    const QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty response from REMOVE_CODE command";
        return Result<void>::error(tr("No response from YubiKey"));
    }

    // Check status word
    quint16 const sw = OathProtocol::getStatusWord(response);
    qCDebug(YubiKeyOathDeviceLog) << "REMOVE_CODE status word:" << QString::number(sw, 16);

    if (sw == OathProtocol::SW_SUCCESS) {
        qCInfo(YubiKeyOathDeviceLog) << "Password removed successfully from device" << m_deviceId;
        return Result<void>::success();
    }

    // Handle errors
    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCWarning(YubiKeyOathDeviceLog) << "Authentication required";
        return Result<void>::error(tr("Authentication required - authenticate with current password first"));
    }

    qCWarning(YubiKeyOathDeviceLog) << "REMOVE_CODE failed with status word:" << QString::number(sw, 16);
    return Result<void>::error(tr("Failed to remove password (error code: 0x%1)")
                                .arg(sw, 4, 16, QLatin1Char('0')));
}

Result<void> OathSession::changePassword(const QString &oldPassword,
                                         const QString &newPassword,
                                         const QString &deviceId)
{
    qCDebug(YubiKeyOathDeviceLog) << "changePassword() for device" << m_deviceId;

    // If old password provided, authenticate first
    if (!oldPassword.isEmpty()) {
        auto authResult = authenticate(oldPassword, deviceId);
        if (authResult.isError()) {
            qCWarning(YubiKeyOathDeviceLog) << "Authentication with old password failed:" << authResult.error();
            return Result<void>::error(tr("Wrong current password: %1").arg(authResult.error()));
        }
        qCDebug(YubiKeyOathDeviceLog) << "Authenticated successfully with old password";
    } else {
        qCDebug(YubiKeyOathDeviceLog) << "No old password provided - skipping authentication (device has no password)";
    }

    // If new password is empty, remove password
    if (newPassword.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "New password is empty - removing password";
        return removePassword();
    }

    // Otherwise, set new password
    return setPassword(newPassword, deviceId);
}

void OathSession::cancelOperation()
{
    qCDebug(YubiKeyOathDeviceLog) << "cancelOperation() for device" << m_deviceId;

    // Send SELECT command to reset device state
    const QByteArray command = OathProtocol::createSelectCommand();
    sendApdu(command);

    // PERFORMANCE: Don't reset m_sessionActive - SELECT was just executed
    // Session remains active and ready for next operation
    // This prevents unnecessary SELECT overhead on next request
    qCDebug(YubiKeyOathDeviceLog) << "Operation cancelled, session remains active";
}

void OathSession::updateCardHandle(SCARDHANDLE newHandle, DWORD newProtocol)
{
    qCDebug(YubiKeyOathDeviceLog) << "updateCardHandle() for device" << m_deviceId
             << "newHandle:" << newHandle << "newProtocol:" << newProtocol;

    m_cardHandle = newHandle;
    m_protocol = newProtocol;
    m_sessionActive = false;  // Requires SELECT after reconnect

    qCDebug(YubiKeyOathDeviceLog) << "Card handle updated, session marked as inactive";
}

// =============================================================================
// Helper Functions
// =============================================================================

bool OathSession::reconnectCard()
{
    qCDebug(YubiKeyOathDeviceLog) << "Attempting to reconnect card for device" << m_deviceId;

    if (m_cardHandle == 0) {
        qCWarning(YubiKeyOathDeviceLog) << "Cannot reconnect - invalid card handle";
        return false;
    }

    // Use SCardReconnect to refresh the connection after card reset
    // SCARD_LEAVE_CARD means don't do anything to the card on reconnect
    DWORD activeProtocol = 0;
    const LONG result = SCardReconnect(
        m_cardHandle,
        SCARD_SHARE_SHARED,     // Same share mode as original connect
        SCARD_PROTOCOL_T1,      // Same protocol as original connect
        SCARD_LEAVE_CARD,       // Don't reset the card
        &activeProtocol
    );

    if (result == SCARD_S_SUCCESS) {
        qCInfo(YubiKeyOathDeviceLog) << "Card reconnected successfully for device" << m_deviceId
                                     << "protocol:" << activeProtocol;

        // Update protocol if it changed
        m_protocol = activeProtocol;

        // Session must be reactivated - SELECT is needed
        m_sessionActive = false;

        return true;
    }

    qCWarning(YubiKeyOathDeviceLog) << "SCardReconnect failed for device" << m_deviceId
                                    << "error:" << QString::number(result, 16);
    return false;
}

Result<void> OathSession::ensureSessionActive()
{
    // If session is already active, nothing to do
    if (m_sessionActive) {
        return Result<void>::success();
    }

    qCDebug(YubiKeyOathDeviceLog) << "Session inactive, reactivating with SELECT for device" << m_deviceId;

    // Execute SELECT to reactivate OATH applet
    QByteArray challenge;
    auto result = selectOathApplication(challenge);

    if (result.isError()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to reactivate session:" << result.error();
        return result;
    }

    qCDebug(YubiKeyOathDeviceLog) << "Session reactivated successfully";
    return Result<void>::success();
}

QByteArray OathSession::deriveKeyPbkdf2(const QByteArray &password,
                                       const QByteArray &salt,
                                       int iterations,
                                       int keyLength)
{
    QByteArray derivedKey;
    const int blockCount = (keyLength + 19) / 20; // SHA1 produces 20 bytes per block

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
                result[j] = static_cast<char>(result[j] ^ U[j]);
            }
        }

        derivedKey.append(result);
    }

    return derivedKey.left(keyLength);
}

} // namespace Daemon
} // namespace YubiKeyOath
