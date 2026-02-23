/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yk_oath_session.h"
#include "extended_device_info_fetcher.h"
#include "oath_error_codes.h"
#include "../logging_categories.h"
#include "../utils/secure_logging.h"
#include "../utils/password_derivation.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>
#include <QThread>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

// PC/SC error codes not always defined in headers
#ifndef SCARD_W_RESET_CARD
#define SCARD_W_RESET_CARD ((LONG)0x80100068)
#endif

YkOathSession::YkOathSession(SCARDHANDLE cardHandle,
                         DWORD protocol,
                         const QString &deviceId, // NOLINT(modernize-pass-by-value) - const ref for consistency
                         QObject *parent)
    : QObject(parent)
    , m_cardHandle(cardHandle)
    , m_protocol(protocol)
    , m_deviceId(deviceId)
    , m_oathProtocol(std::make_unique<YKOathProtocol>())
{
    qCDebug(YubiKeyOathDeviceLog) << "YkOathSession created for device" << m_deviceId;
}

YkOathSession::~YkOathSession()
{
    qCDebug(YubiKeyOathDeviceLog) << "YkOathSession destroyed for device" << m_deviceId;
    // Note: We do NOT disconnect card handle - caller owns it
}

// =============================================================================
// PC/SC Communication
// =============================================================================

QByteArray YkOathSession::sendApdu(const QByteArray &command, int retryCount)
{
    qCDebug(YubiKeyOathDeviceLog) << "sendApdu() for device:" << m_deviceId
             << "command:" << SecureLogging::safeApduInfo(command)
             << "retryCount:" << retryCount;

    if (m_cardHandle == 0) {
        qCDebug(YubiKeyOathDeviceLog) << "Device" << m_deviceId << "not connected (invalid handle)";
        return {};
    }

    // PC/SC rate limiting: configurable interval between operations
    // Default is 0 (no delay) for maximum performance.
    // Users experiencing communication errors with specific readers can increase this value.
    if (m_rateLimitMs > 0 && m_lastPcscOperationTime > 0) {
        const qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
        const qint64 elapsed = currentTime - m_lastPcscOperationTime;
        if (elapsed < m_rateLimitMs) {
            const qint64 sleepTime = m_rateLimitMs - elapsed;
            qCDebug(YubiKeyOathDeviceLog) << "PC/SC rate limiting: sleeping for" << sleepTime << "ms"
                     << "(elapsed since last operation:" << elapsed << "ms, limit:" << m_rateLimitMs << "ms)";
            QThread::msleep(sleepTime);
        }
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

    // Update timestamp immediately after PC/SC operation (success or failure)
    // This ensures consistent rate limiting regardless of operation outcome
    m_lastPcscOperationTime = QDateTime::currentMSecsSinceEpoch();

    qCDebug(YubiKeyOathDeviceLog) << "SCardTransmit result:" << result
             << "response length:" << responseLen;

    if (result != SCARD_S_SUCCESS) {
        qCDebug(YubiKeyOathDeviceLog) << "Failed to send APDU, error code:" << QString::number(result, 16);

        // Handle card reset - emit signal and wait for reconnect result
        if (result == SCARD_W_RESET_CARD && retryCount == 0) {
            qCWarning(YubiKeyOathDeviceLog) << "Card reset detected (SCARD_W_RESET_CARD), emitting signal and waiting for reconnect";

            // Emit signal to trigger reconnect workflow in upper layers
            Q_EMIT cardResetDetected(command);

            // Wait for reconnect result using QEventLoop
            QEventLoop loop;
            bool reconnectSuccess = false;

            // Connect to signals from upper layer (YubiKeyOathDevice)
            const QMetaObject::Connection connReady = connect(this, &YkOathSession::reconnectReady, &loop, [&]() {
                qCInfo(YubiKeyOathDeviceLog) << "Received reconnectReady signal";
                reconnectSuccess = true;
                loop.quit();
            });

            const QMetaObject::Connection connFailed = connect(this, &YkOathSession::reconnectFailed, &loop, [&]() {
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
        }

        Q_EMIT errorOccurred(tr("Failed to send APDU: 0x%1").arg(result, 0, 16));
        return {};
    }

    const QByteArray responseData(reinterpret_cast<const char*>(response), static_cast<qsizetype>(responseLen));
    qCDebug(YubiKeyOathDeviceLog) << "APDU response:" << SecureLogging::safeByteInfo(responseData);

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

            qCDebug(YubiKeyOathDeviceLog) << "Sending SEND REMAINING command";

            responseLen = sizeof(response);
            result = SCardTransmit(m_cardHandle, &pioSendPci,
                                 reinterpret_cast<const BYTE*>(sendRemCmd.constData()), sendRemCmd.length(),
                                 nullptr, static_cast<BYTE*>(response), &responseLen); // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

            if (result != SCARD_S_SUCCESS) {
                qCDebug(YubiKeyOathDeviceLog) << "SEND REMAINING failed:" << QString::number(result, 16);
                break;
            }

            qCDebug(YubiKeyOathDeviceLog) << "SEND REMAINING received" << responseLen << "bytes";
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

Result<void> YkOathSession::selectOathApplication(QByteArray &outChallenge, Version &outFirmwareVersion)
{
    qCDebug(YubiKeyOathDeviceLog) << "selectOathApplication() for device" << m_deviceId;

    const QByteArray command = OathProtocol::createSelectCommand();
    const QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from SELECT";
        return Result<void>::error(tr("Failed to select OATH application"));
    }

    // Parse response to get device ID, challenge, firmware version, password requirement, and serial
    qCDebug(YubiKeyOathDeviceLog) << "SELECT response length:" << response.length();
    QString deviceId;
    bool requiresPassword = false;
    quint32 serialNumber = 0;
    if (!m_oathProtocol->parseSelectResponse(response, deviceId, outChallenge, outFirmwareVersion, requiresPassword, serialNumber)) {
        qCDebug(YubiKeyOathDeviceLog) << "Failed to parse SELECT response - length:" << response.length();
        return Result<void>::error(tr("Failed to parse SELECT response"));
    }

    // Update device ID if we got one from response
    if (!deviceId.isEmpty()) {
        m_deviceId = deviceId;
    }

    // Store firmware version from SELECT
    m_firmwareVersion = outFirmwareVersion;

    // Store serial number from SELECT (strategy #0 for serial detection)
    m_selectSerialNumber = serialNumber;

    // Store password requirement from SELECT
    m_requiresPassword = requiresPassword;

    qCDebug(YubiKeyOathDeviceLog) << "SELECT successful, device ID:" << m_deviceId
             << "firmware:" << outFirmwareVersion.toString()
             << "hasChallenge:" << !outChallenge.isEmpty()
             << "requiresPassword:" << requiresPassword
             << "serial:" << SecureLogging::maskSerial(serialNumber);

    return Result<void>::success();
}

Result<QString> YkOathSession::calculateCode(const QString &name, int period)
{
    qCDebug(YubiKeyOathDeviceLog) << "calculateCode() for" << name << "on device" << m_deviceId
                                   << "with period" << period;

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

    // Check for touch required
    if (sw == OathProtocol::SW_CONDITIONS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Touch required (SW=6985)";
        Q_EMIT touchRequired();
        return Result<QString>::error(tr("Touch required"));
    }

    // Check for authentication required
    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Password required for CALCULATE (SW=6982)";
        return Result<QString>::error(OathErrorCodes::PASSWORD_REQUIRED);
    }

    // Parse code
    const QString code = m_oathProtocol->parseCode(response);
    if (code.isEmpty()) {
        return Result<QString>::error(tr("Failed to parse TOTP code from response"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Code generated successfully";
    return Result<QString>::success(code);
}

Result<QList<OathCredential>> YkOathSession::calculateAll()
{
    qCDebug(YubiKeyOathDeviceLog) << "calculateAll() for device" << m_deviceId;

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

    // Check for authentication requirement
    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Password required for CALCULATE ALL";
        return Result<QList<OathCredential>>::error(OathErrorCodes::PASSWORD_REQUIRED);
    }

    // Parse response
    QList<OathCredential> credentials = m_oathProtocol->parseCalculateAllResponse(response);

    // Set device ID for all credentials
    for (auto &cred : credentials) {
        cred.deviceId = m_deviceId;
    }

    qCDebug(YubiKeyOathDeviceLog) << "Calculated codes for" << credentials.size() << "credentials";
    return Result<QList<OathCredential>>::success(credentials);
}

Result<QList<OathCredential>> YkOathSession::listCredentials()
{
    qCDebug(YubiKeyOathDeviceLog) << "listCredentials() for device" << m_deviceId;

    // Create LIST command
    const QByteArray command = OathProtocol::createListCommand();
    const QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from LIST";
        return Result<QList<OathCredential>>::error(tr("Failed to list credentials"));
    }

    // Check status word
    const quint16 sw = OathProtocol::getStatusWord(response);

    // Check for authentication requirement
    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Password required for LIST";
        return Result<QList<OathCredential>>::error(OathErrorCodes::PASSWORD_REQUIRED);
    }

    // Parse credential list
    QList<OathCredential> credentials = OathProtocol::parseCredentialList(response);

    // Set device ID for all credentials
    for (auto &cred : credentials) {
        cred.deviceId = m_deviceId;
    }

    qCDebug(YubiKeyOathDeviceLog) << "Listed" << credentials.size() << "credentials";
    return Result<QList<OathCredential>>::success(credentials);
}

Result<void> YkOathSession::authenticate(const QString &password, const QString &deviceId)
{
    qCDebug(YubiKeyOathDeviceLog) << "authenticate() for device" << m_deviceId;

// STEP 1: Execute SELECT to get fresh challenge from YubiKey
    // YubiKey does NOT maintain challenge state between VALIDATE commands
    qCDebug(YubiKeyOathDeviceLog) << "Executing SELECT to obtain fresh challenge";

    QByteArray freshChallenge;
    Version firmwareVersion;  // Not used in authentication flow
    auto selectResult = selectOathApplication(freshChallenge, firmwareVersion);
    if (selectResult.isError()) {
        qCDebug(YubiKeyOathDeviceLog) << "SELECT failed:" << selectResult.error();
        return selectResult;
    }

    if (freshChallenge.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "No challenge in SELECT response";
        return Result<void>::error(tr("No challenge received from YubiKey"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Fresh challenge obtained from SELECT";

    // STEP 2: Derive key from password using PBKDF2
    const QByteArray salt = QByteArray::fromHex(deviceId.toLatin1());
    const QByteArray key = PasswordDerivation::deriveKeyPbkdf2(
        password.toUtf8(),
        salt,
        PasswordDerivation::OATH_PBKDF2_ITERATIONS,
        PasswordDerivation::OATH_DERIVED_KEY_LENGTH
    );

    qCDebug(YubiKeyOathDeviceLog) << "Derived encryption key from password (PBKDF2)";

    // STEP 3: Calculate HMAC-SHA1 response using fresh challenge
    const QByteArray hmacResponse = QMessageAuthenticationCode::hash(
        freshChallenge,
        key,
        QCryptographicHash::Sha1
    );

    qCDebug(YubiKeyOathDeviceLog) << "Computed HMAC response for authentication";

    // STEP 4: Create and send VALIDATE command
    // Generate our challenge for mutual authentication
    QByteArray ourChallenge;
    for (int i = 0; i < 8; ++i) {
        ourChallenge.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }
    qCDebug(YubiKeyOathDeviceLog) << "Generated challenge for mutual authentication";

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
            qCDebug(YubiKeyOathDeviceLog) << "Verifying YubiKey mutual authentication response";

            const QByteArray expectedResponse = QMessageAuthenticationCode::hash(
                ourChallenge,
                key,
                QCryptographicHash::Sha1
            );

            if (responseTag == expectedResponse) {
                qCDebug(YubiKeyOathDeviceLog) << "YubiKey response verified successfully";
            } else {
                qCWarning(YubiKeyOathDeviceLog) << "YubiKey mutual authentication verification failed";
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

Result<void> YkOathSession::putCredential(const OathCredentialData &data)
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

Result<void> YkOathSession::deleteCredential(const QString &name)
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

Result<void> YkOathSession::setPassword(const QString &newPassword, const QString &deviceId)
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
    Version firmwareVersion;  // Not used in password change flow
    auto selectResult = selectOathApplication(selectChallenge, firmwareVersion);
    if (selectResult.isError()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to SELECT OATH application:" << selectResult.error();
        return Result<void>::error(tr("Failed to select OATH application: %1").arg(selectResult.error()));
    }

    // Derive key from password using PBKDF2
    QByteArray const key = PasswordDerivation::deriveKeyPbkdf2(
        newPassword.toUtf8(),
        QByteArray::fromHex(deviceId.toUtf8()),
        PasswordDerivation::OATH_PBKDF2_ITERATIONS,
        PasswordDerivation::OATH_DERIVED_KEY_LENGTH
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

Result<void> YkOathSession::removePassword()
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

Result<void> YkOathSession::changePassword(const QString &oldPassword,
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

Result<ExtendedDeviceInfo> YkOathSession::getExtendedDeviceInfo(const QString &readerName)
{
    qCDebug(YubiKeyOathDeviceLog) << "getExtendedDeviceInfo() for device" << m_deviceId;

    // Create parser lambda that captures m_oathProtocol
    auto parseSelectResponse = [this](const QByteArray &response,
                                       QString &deviceId,
                                       QByteArray &challenge,
                                       Version &firmware,
                                       bool &requiresPassword,
                                       quint32 &serialNumber) -> bool {
        return m_oathProtocol->parseSelectResponse(response, deviceId, challenge,
                                                    firmware, requiresPassword, serialNumber);
    };

    // Create fetcher with dependencies
    ExtendedDeviceInfoFetcher fetcher(
        [this](const QByteArray &command) { return sendApdu(command); },
        parseSelectResponse,
        m_deviceId,
        m_selectSerialNumber,
        m_firmwareVersion
    );

    return fetcher.fetch(readerName);
}

void YkOathSession::cancelOperation()
{
    qCDebug(YubiKeyOathDeviceLog) << "cancelOperation() for device" << m_deviceId;

    // Send SELECT command to reset device state
    const QByteArray command = OathProtocol::createSelectCommand();
    sendApdu(command);

    qCDebug(YubiKeyOathDeviceLog) << "Operation cancelled";
}

void YkOathSession::updateCardHandle(SCARDHANDLE newHandle, DWORD newProtocol)
{
    qCDebug(YubiKeyOathDeviceLog) << "updateCardHandle() for device" << m_deviceId
             << "newHandle:" << newHandle << "newProtocol:" << newProtocol;

    m_cardHandle = newHandle;
    m_protocol = newProtocol;

    qCDebug(YubiKeyOathDeviceLog) << "Card handle updated, session marked as inactive";
}

void YkOathSession::setRateLimitMs(qint64 intervalMs)
{
    m_rateLimitMs = intervalMs;
    qCDebug(YubiKeyOathDeviceLog) << "PC/SC rate limit set to" << intervalMs << "ms for device" << m_deviceId;
}

// =============================================================================
// Helper Functions
// =============================================================================

bool YkOathSession::reconnectCard()
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

        return true;
    }

    qCWarning(YubiKeyOathDeviceLog) << "SCardReconnect failed for device" << m_deviceId
                                    << "error:" << QString::number(result, 16);
    return false;
}

} // namespace Daemon
} // namespace YubiKeyOath
