/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_oath_device.h"
#include "oath_session.h"
#include "oath_protocol.h"
#include "../logging_categories.h"

#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QtConcurrent>

// C++ standard library for timeout support
#include <future>
#include <chrono>

// PC/SC includes
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;
#endif

namespace {

/**
 * @brief SCardConnect with timeout support
 * @param context PC/SC context
 * @param readerName Reader name
 * @param shareMode Share mode (SCARD_SHARE_SHARED, etc.)
 * @param protocols Protocols (SCARD_PROTOCOL_T0, etc.)
 * @param handle Output: Card handle
 * @param activeProtocol Output: Active protocol
 * @param timeoutMs Timeout in milliseconds (default: 2000ms)
 * @return SCARD_* status code
 *
 * Launches SCardConnect in background thread and waits with timeout.
 * Returns SCARD_E_TIMEOUT if connection takes longer than timeout.
 *
 * SECURITY: Uses capture-by-value to avoid dangling references if timeout occurs.
 * Background thread may continue running after timeout, but safely with its own copies.
 */
LONG SCardConnectWithTimeout(SCARDCONTEXT context,
                              const char* readerName,
                              DWORD shareMode,
                              DWORD protocols,
                              SCARDHANDLE* handle,
                              DWORD* activeProtocol,
                              int timeoutMs = 2000)
{
    // Launch SCardConnect in background thread
    // SECURITY: Capture by value to avoid dangling references if timeout occurs
    auto future = std::async(std::launch::async, [context, readerName, shareMode, protocols, handle, activeProtocol]() {
        return SCardConnect(context, readerName, shareMode, protocols, handle, activeProtocol);
    });

    // Wait with timeout
    if (future.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::timeout) {
        qCWarning(YubiKeyOathDeviceLog) << "SCardConnect timeout after" << timeoutMs << "ms";
        // Note: Background thread may still be running, but it has its own copies of parameters
        return SCARD_E_TIMEOUT;
    }

    return future.get();
}

} // anonymous namespace

YubiKeyOathDevice::YubiKeyOathDevice(const QString &deviceId,
                                     const QString &readerName,
                                     SCARDHANDLE cardHandle,
                                     DWORD protocol,
                                     const QByteArray &challenge,
                                     SCARDCONTEXT context,
                                     QObject *parent)
    : QObject(parent)
    , m_deviceId(deviceId)
    , m_readerName(readerName)
    , m_cardHandle(cardHandle)
    , m_protocol(protocol)
    , m_context(context)
    , m_challenge(challenge)
    , m_session(std::make_unique<OathSession>(cardHandle, protocol, deviceId, this))
{
    qCDebug(YubiKeyOathDeviceLog) << "Created for device" << m_deviceId
             << "reader:" << m_readerName;

    // Forward signals from OathSession
    connect(m_session.get(), &OathSession::touchRequired,
            this, &YubiKeyOathDevice::touchRequired);
    connect(m_session.get(), &OathSession::errorOccurred,
            this, &YubiKeyOathDevice::errorOccurred);

    // Forward cardResetDetected to needsReconnect with device information
    connect(m_session.get(), &OathSession::cardResetDetected,
            this, [this](const QByteArray &command) {
        qCDebug(YubiKeyOathDeviceLog) << "Card reset detected, emitting needsReconnect for device" << m_deviceId;
        Q_EMIT needsReconnect(m_deviceId, m_readerName, command);
    });

    // Initialize OATH session immediately (following Yubico yubikey-manager pattern)
    // This ensures the session is active and ready for CALCULATE ALL without
    // executing SELECT before every request (major performance optimization)
    QByteArray sessionChallenge;
    auto selectResult = m_session->selectOathApplication(sessionChallenge);
    if (selectResult.isError()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to initialize OATH session:" << selectResult.error();
        // Continue anyway - session will retry on first operation if needed
    } else {
        qCDebug(YubiKeyOathDeviceLog) << "OATH session initialized successfully";
    }

    // Connect to our own credentialCacheFetched signal to update internal state
    connect(this, &YubiKeyOathDevice::credentialCacheFetched,
            this, [this](const QList<OathCredential> &credentials) {
                qCDebug(YubiKeyOathDeviceLog) << "Updating credential cache with" << credentials.size() << "credentials";
                m_credentials = credentials;
                m_updateInProgress = false;
                qCDebug(YubiKeyOathDeviceLog) << "Credential cache updated, updateInProgress reset to false";
            });
}

YubiKeyOathDevice::~YubiKeyOathDevice()
{
    qCDebug(YubiKeyOathDeviceLog) << "Destroying device" << m_deviceId;

    // IMPORTANT: Wait for background threads to finish
    // QtConcurrent::run() threads may still be accessing this object
    if (m_updateInProgress) {
        qCDebug(YubiKeyOathDeviceLog) << "Waiting for background operation to complete...";

        // Wait up to 5 seconds for background operation to finish
        int waitCount = 0;
        const int maxWait = 50; // 50 * 100ms = 5 seconds
        while (m_updateInProgress && waitCount < maxWait) {
            QThread::msleep(100);
            waitCount++;
        }

        if (m_updateInProgress) {
            qCWarning(YubiKeyOathDeviceLog) << "Background operation did not finish in time!";
            // Continue anyway, but this may cause issues
        } else {
            qCDebug(YubiKeyOathDeviceLog) << "Background operation completed";
        }
    }

    // Disconnect from card
    if (m_cardHandle != 0) {
        qCDebug(YubiKeyOathDeviceLog) << "Disconnecting card handle for device" << m_deviceId;
        SCardDisconnect(m_cardHandle, SCARD_LEAVE_CARD);
        m_cardHandle = 0;
    }
}

// =============================================================================
// OATH Operations - delegated to OathSession
// =============================================================================


Result<QString> YubiKeyOathDevice::generateCode(const QString& name)
{
    qCDebug(YubiKeyOathDeviceLog) << "generateCode() for" << name << "on device" << m_deviceId;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);

    // Find credential to get its period
    int period = 30; // Default period
    for (const auto &cred : m_credentials) {
        if (cred.originalName == name) {
            period = cred.period;
            qCDebug(YubiKeyOathDeviceLog) << "Found credential period:" << period << "for" << name;
            break;
        }
    }

    auto result = m_session->calculateCode(name, period);

    // Check if password required
    if (result.isError() && result.error() == tr("Password required")) {
        qCDebug(YubiKeyOathDeviceLog) << "Password required for CALCULATE";

        if (!m_password.isEmpty()) {
            qCDebug(YubiKeyOathDeviceLog) << "Attempting re-authentication";
            auto authResult = m_session->authenticate(m_password, m_deviceId);
            if (authResult.isSuccess()) {
                // Retry CALCULATE command after authentication
                qCDebug(YubiKeyOathDeviceLog) << "Re-authentication successful, retrying CALCULATE";
                result = m_session->calculateCode(name, period);
            } else {
                qCDebug(YubiKeyOathDeviceLog) << "Re-authentication failed:" << authResult.error();
                return Result<QString>::error(tr("Authentication failed"));
            }
        } else {
            qCDebug(YubiKeyOathDeviceLog) << "No password available for re-authentication";
            return Result<QString>::error(tr("Password required"));
        }
    }

    return result;
}

Result<void> YubiKeyOathDevice::authenticateWithPassword(const QString& password)
{
    qCDebug(YubiKeyOathDeviceLog) << "authenticateWithPassword() for device" << m_deviceId;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);

    auto result = m_session->authenticate(password, m_deviceId);
    if (result.isSuccess()) {
        m_password = password;
    }

    return result;
}

Result<void> YubiKeyOathDevice::addCredential(const OathCredentialData &data)
{
    qCDebug(YubiKeyOathDeviceLog) << "addCredential() for device" << m_deviceId
                                   << "credential:" << data.name;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);

    // If device requires password and we have one, authenticate first
    if (!m_password.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Authenticating before adding credential";
        auto authResult = m_session->authenticate(m_password, m_deviceId);
        if (authResult.isError()) {
            qCWarning(YubiKeyOathDeviceLog) << "Authentication failed:" << authResult.error();
            return authResult;
        }
    }

    // Add credential via session
    auto result = m_session->putCredential(data);

    if (result.isSuccess()) {
        qCDebug(YubiKeyOathDeviceLog) << "Credential added successfully, triggering cache update";
        // Trigger credential cache refresh to include new credential
        updateCredentialCacheAsync(m_password);
    }

    return result;
}

Result<void> YubiKeyOathDevice::deleteCredential(const QString &name)
{
    qCDebug(YubiKeyOathDeviceLog) << "deleteCredential() for device" << m_deviceId
                                   << "credential:" << name;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);

    // If device requires password and we have one, authenticate first
    if (!m_password.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Authenticating before deleting credential";
        auto authResult = m_session->authenticate(m_password, m_deviceId);
        if (authResult.isError()) {
            qCWarning(YubiKeyOathDeviceLog) << "Authentication failed:" << authResult.error();
            return authResult;
        }
    }

    // Delete credential via session
    auto result = m_session->deleteCredential(name);

    if (result.isSuccess()) {
        qCDebug(YubiKeyOathDeviceLog) << "Credential deleted successfully, triggering cache update";
        // Trigger credential cache refresh to remove deleted credential
        updateCredentialCacheAsync(m_password);
    }

    return result;
}

Result<void> YubiKeyOathDevice::changePassword(const QString &oldPassword, const QString &newPassword)
{
    qCDebug(YubiKeyOathDeviceLog) << "changePassword() for device" << m_deviceId;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);

    // Change password via session (handles authentication internally)
    auto result = m_session->changePassword(oldPassword, newPassword, m_deviceId);

    if (result.isSuccess()) {
        if (newPassword.isEmpty()) {
            qCDebug(YubiKeyOathDeviceLog) << "Password removed successfully";
        } else {
            qCDebug(YubiKeyOathDeviceLog) << "Password changed successfully";
        }
    } else {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to change password:" << result.error();
    }

    return result;
}

void YubiKeyOathDevice::setPassword(const QString& password)
{
    qCDebug(YubiKeyOathDeviceLog) << "setPassword() for device" << m_deviceId;
    m_password = password;
}

QList<OathCredential> YubiKeyOathDevice::fetchCredentialsSync(const QString& password)
{
    qCDebug(YubiKeyOathDeviceLog) << "fetchCredentialsSync() for device" << m_deviceId;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);

    // Use CALCULATE ALL to get credentials with codes
    auto result = m_session->calculateAll();

    if (result.isError()) {
        // Check if password required
        if (result.error() == tr("Password required")) {
            qCDebug(YubiKeyOathDeviceLog) << "Password required for CALCULATE ALL";

            QString devicePassword = password.isEmpty() ? m_password : password;
            if (!devicePassword.isEmpty()) {
                qCDebug(YubiKeyOathDeviceLog) << "Attempting authentication";
                auto authResult = m_session->authenticate(devicePassword, m_deviceId);
                if (authResult.isSuccess()) {
                    // Update stored password
                    m_password = devicePassword;

                    // Retry CALCULATE ALL command after authentication
                    qCDebug(YubiKeyOathDeviceLog) << "Authentication successful, retrying CALCULATE ALL";
                    result = m_session->calculateAll();

                    // BUG FIX #3: Check if retry succeeded before accessing value()
                    if (result.isError()) {
                        qCWarning(YubiKeyOathDeviceLog) << "CALCULATE ALL failed after authentication:" << result.error();
                        return QList<OathCredential>();
                    }
                } else {
                    qCDebug(YubiKeyOathDeviceLog) << "Authentication failed:" << authResult.error();
                    return QList<OathCredential>();
                }
            } else {
                qCDebug(YubiKeyOathDeviceLog) << "No password available";
                return QList<OathCredential>();
            }
        } else {
            qCDebug(YubiKeyOathDeviceLog) << "CALCULATE ALL failed:" << result.error();
            return QList<OathCredential>();
        }
    }

    QList<OathCredential> credentials = result.value();
    qCDebug(YubiKeyOathDeviceLog) << "Fetched" << credentials.size() << "credentials";

    return credentials;
}

void YubiKeyOathDevice::updateCredentialCacheAsync(const QString& password)
{
    qCDebug(YubiKeyOathDeviceLog) << "updateCredentialCacheAsync() for device" << m_deviceId;

    if (m_updateInProgress) {
        qCDebug(YubiKeyOathDeviceLog) << "Update already in progress";
        return;
    }

    m_updateInProgress = true;

    QString passwordToUse = password.isEmpty() ? m_password : password;

    // Note: We don't store the QFuture because we communicate via signals/slots.
    // The m_updateInProgress flag tracks whether an update is running.
    [[maybe_unused]] auto future = QtConcurrent::run([this, passwordToUse]() {
        qCDebug(YubiKeyOathDeviceLog) << "Background thread started for credential fetch";
        QList<OathCredential> credentials = this->fetchCredentialsSync(passwordToUse);

        qCDebug(YubiKeyOathDeviceLog) << "Fetched" << credentials.size() << "credentials in background thread";

        Q_EMIT credentialCacheFetched(credentials);
    });
}

void YubiKeyOathDevice::cancelPendingOperation()
{
    qCDebug(YubiKeyOathDeviceLog) << "cancelPendingOperation() for device" << m_deviceId;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);

    m_session->cancelOperation();
}

void YubiKeyOathDevice::onReconnectResult(bool success)
{
    qCDebug(YubiKeyOathDeviceLog) << "onReconnectResult() for device" << m_deviceId
             << "success:" << success;

    // Forward result to OathSession to unblock waiting sendApdu()
    if (success) {
        qCInfo(YubiKeyOathDeviceLog) << "Reconnect successful, emitting reconnectReady to OathSession";
        Q_EMIT m_session->reconnectReady();
    } else {
        qCWarning(YubiKeyOathDeviceLog) << "Reconnect failed, emitting reconnectFailed to OathSession";
        Q_EMIT m_session->reconnectFailed();
    }
}

Result<void> YubiKeyOathDevice::reconnectCardHandle(const QString &readerName)
{
    qCDebug(YubiKeyOathDeviceLog) << "reconnectCardHandle() for device" << m_deviceId
             << "reader:" << readerName;

    // NOTE: No mutex lock here - safe because:
    // 1. Only called from main thread (onReconnectTimer)
    // 2. Background thread waits in loop.exec() for reconnectReady signal, doesn't use m_cardHandle
    // 3. Using mutex here would cause DEADLOCK (background thread holds mutex while waiting)

    // 1. Disconnect old card handle to free PC/SC resource
    if (m_cardHandle != 0) {
        qCDebug(YubiKeyOathDeviceLog) << "Disconnecting old card handle";
        SCardDisconnect(m_cardHandle, SCARD_LEAVE_CARD);
        m_cardHandle = 0;
    }

    // 2. Exponential backoff reconnect attempts
    static const int delays[] = {100, 200, 400, 800, 1600, 3000};
    const int maxAttempts = 6;

    for (int attempt = 0; attempt < maxAttempts; attempt++) {
        if (attempt > 0) {
            qCDebug(YubiKeyOathDeviceLog) << "Reconnect attempt" << attempt
                     << "after" << delays[attempt - 1] << "ms delay";
            QThread::msleep(delays[attempt - 1]);
        }

        SCARDHANDLE newHandle;
        DWORD activeProtocol;
        LONG result = SCardConnectWithTimeout(m_context,
                                              readerName.toUtf8().constData(),
                                              SCARD_SHARE_SHARED,
                                              SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                                              &newHandle, &activeProtocol,
                                              2000);  // 2 second timeout

        if (result == SCARD_S_SUCCESS) {
            qCDebug(YubiKeyOathDeviceLog) << "SCardConnect successful, handle:" << newHandle;

            // 3. SELECT OATH applet to verify functionality
            OathSession tempSession(newHandle, activeProtocol, m_deviceId, this);
            QByteArray challenge;
            auto selectResult = tempSession.selectOathApplication(challenge);

            if (selectResult.isSuccess()) {
                qCInfo(YubiKeyOathDeviceLog) << "OATH SELECT successful, updating card handle";

                // 4. Update handle in existing session without destroying it
                m_cardHandle = newHandle;
                m_session->updateCardHandle(newHandle, activeProtocol);

                qCInfo(YubiKeyOathDeviceLog) << "Card handle reconnected successfully";
                return Result<void>::success();
            } else {
                qCWarning(YubiKeyOathDeviceLog) << "OATH SELECT failed:" << selectResult.error();
                SCardDisconnect(newHandle, SCARD_LEAVE_CARD);
            }
        } else {
            qCDebug(YubiKeyOathDeviceLog) << "SCardConnect failed:" << QString::number(result, 16);
        }
    }

    qCWarning(YubiKeyOathDeviceLog) << "Failed to reconnect after" << maxAttempts << "attempts";
    return Result<void>::error(tr("Failed to reconnect after multiple attempts"));
}

} // namespace Daemon
} // namespace YubiKeyOath
