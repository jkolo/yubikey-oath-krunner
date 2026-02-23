/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_device.h"
#include "oath_error_codes.h"
#include "yk_oath_session.h"
#include "../logging_categories.h"
#include "../pcsc/card_transaction.h"
#include "shared/types/device_state.h"

#include <QMutexLocker>
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
#endif

namespace YubiKeyOath {
namespace Daemon {

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

// Constructor and destructor must be in .cpp for Qt MOC to generate vtable
OathDevice::OathDevice(QObject *parent)
    : QObject(parent)
{
}

OathDevice::~OathDevice() = default;

// ============================================================================
// State Management Implementation
// ============================================================================

Shared::DeviceState OathDevice::state() const
{
    const QMutexLocker locker(&m_stateMutex);
    return m_state;
}

QString OathDevice::lastError() const
{
    const QMutexLocker locker(&m_stateMutex);
    return m_lastError;
}

void OathDevice::setState(Shared::DeviceState state)
{
    Shared::DeviceState oldState = Shared::DeviceState::Disconnected;
    {
        const QMutexLocker locker(&m_stateMutex);
        if (m_state == state) {
            return;  // No change
        }
        oldState = m_state;
        m_state = state;

        // Clear error message when leaving Error state
        if (state != Shared::DeviceState::Error) {
            m_lastError.clear();
        }
    }

    // Log state transition
    qCInfo(YubiKeyOathDeviceLog) << "Device" << m_deviceId << "state:"
                                   << Shared::deviceStateToString(oldState) << "→"
                                   << Shared::deviceStateToString(state);

    // Emit signal outside of lock to avoid potential deadlocks
    Q_EMIT stateChanged(state);
}

void OathDevice::setErrorState(const QString &error)
{
    {
        const QMutexLocker locker(&m_stateMutex);
        m_state = Shared::DeviceState::Error;
        m_lastError = error;
    }

    // Emit signals outside of lock
    Q_EMIT stateChanged(Shared::DeviceState::Error);
    Q_EMIT errorOccurred(error);
}

// =============================================================================
// Password Management
// =============================================================================

void OathDevice::setPassword(const QString& password)
{
    qCDebug(YubiKeyOathDeviceLog) << "setPassword() for device" << m_deviceId;
    m_password = SecureMemory::SecureString(password);
}

// =============================================================================
// OATH Operations - Common implementations using polymorphic m_session
// =============================================================================

Result<QString> OathDevice::generateCode(const QString& name)
{
    qCDebug(YubiKeyOathDeviceLog) << "generateCode() for" << name << "on device" << m_deviceId
                                  << "- credentials cache size:" << m_credentials.size();

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks

    // Find credential to get its period and touch requirement
    int period = 30; // Default period
    bool requiresTouch = false;
    bool found = false;
    for (const auto &cred : m_credentials) {
        if (cred.originalName == name) {
            period = cred.period;
            requiresTouch = cred.requiresTouch;
            found = true;
            qCDebug(YubiKeyOathDeviceLog) << "Found credential period:" << period << "requiresTouch:" << requiresTouch << "for" << name;
            break;
        }
    }

    // Validate credential exists before calling PC/SC
    if (!found) {
        qCWarning(YubiKeyOathDeviceLog) << "Credential" << name << "not found in cache"
                                        << "(cache size:" << m_credentials.size() << ")"
                                        << "- cannot generate code safely";
        return Result<QString>::error(OathErrorCodes::CREDENTIAL_NOT_FOUND);
    }

    // Begin PC/SC transaction
    // Skip SELECT OATH when password is set - authenticate() does its own SELECT to get
    // a fresh challenge, so CardTransaction's SELECT would be redundant (saves ~100-500ms)
    const bool skipSelect = !m_password.isEmpty();
    const CardTransaction transaction(m_cardHandle, m_session.get(), skipSelect);
    if (!transaction.isValid()) {
        qCWarning(YubiKeyOathDeviceLog) << "Transaction failed:" << transaction.errorMessage();
        return Result<QString>::error(transaction.errorMessage());
    }

    // Authenticate if password required
    if (!m_password.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Authenticating within transaction";
        auto authResult = m_session->authenticate(m_password, m_deviceId);
        if (authResult.isError()) {
            qCWarning(YubiKeyOathDeviceLog) << "Authentication failed:" << authResult.error();
            return Result<QString>::error(i18n("Authentication failed"));
        }
    }

    // Emit touchRequired signal BEFORE calculateCode for touch-required credentials
    // This allows notification to appear before CALCULATE blocks waiting for user touch
    if (requiresTouch) {
        qCDebug(YubiKeyOathDeviceLog) << "Emitting touchRequired signal before CALCULATE (preemptive notification)";
        Q_EMIT touchRequired();
    }

    // Calculate code (session no longer does its own transaction/SELECT/auth)
    auto result = m_session->calculateCode(name, period);

    return result;
}

Result<void> OathDevice::authenticateWithPassword(const QString& password)
{
    qCDebug(YubiKeyOathDeviceLog) << "authenticateWithPassword() for device" << m_deviceId;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks

    // Begin PC/SC transaction with automatic SELECT OATH
    const CardTransaction transaction(m_cardHandle, m_session.get());
    if (!transaction.isValid()) {
        qCWarning(YubiKeyOathDeviceLog) << "Transaction failed:" << transaction.errorMessage();
        return Result<void>::error(transaction.errorMessage());
    }

    // Authenticate within transaction
    auto result = m_session->authenticate(password, m_deviceId);
    if (result.isSuccess()) {
        m_password = SecureMemory::SecureString(password);
    }

    return result;
}

Result<void> OathDevice::addCredential(const OathCredentialData &data)
{
    qCDebug(YubiKeyOathDeviceLog) << "addCredential() for device" << m_deviceId
                                   << "credential:" << data.name;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks

    // Begin PC/SC transaction with automatic SELECT OATH
    const CardTransaction transaction(m_cardHandle, m_session.get());
    if (!transaction.isValid()) {
        qCWarning(YubiKeyOathDeviceLog) << "Transaction failed:" << transaction.errorMessage();
        return Result<void>::error(transaction.errorMessage());
    }

    // Authenticate if password required
    if (!m_password.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Authenticating within transaction before adding credential";
        auto authResult = m_session->authenticate(m_password, m_deviceId);
        if (authResult.isError()) {
            qCWarning(YubiKeyOathDeviceLog) << "Authentication failed:" << authResult.error();
            return authResult;
        }
    }

    // Add credential via session (no longer does its own transaction/SELECT/auth)
    auto result = m_session->putCredential(data);

    if (result.isSuccess()) {
        qCDebug(YubiKeyOathDeviceLog) << "Credential added successfully, triggering cache update";
        // Trigger credential cache refresh to include new credential
        updateCredentialCacheAsync(m_password);
    }

    return result;
}

Result<void> OathDevice::deleteCredential(const QString &name)
{
    qCDebug(YubiKeyOathDeviceLog) << "deleteCredential() for device" << m_deviceId
                                   << "credential:" << name;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks

    // Begin PC/SC transaction with automatic SELECT OATH
    const CardTransaction transaction(m_cardHandle, m_session.get());
    if (!transaction.isValid()) {
        qCWarning(YubiKeyOathDeviceLog) << "Transaction failed:" << transaction.errorMessage();
        return Result<void>::error(transaction.errorMessage());
    }

    // Authenticate if password required
    if (!m_password.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Authenticating within transaction before deleting credential";
        auto authResult = m_session->authenticate(m_password, m_deviceId);
        if (authResult.isError()) {
            qCWarning(YubiKeyOathDeviceLog) << "Authentication failed:" << authResult.error();
            return authResult;
        }
    }

    // Delete credential via session (no longer does its own transaction/SELECT/auth)
    auto result = m_session->deleteCredential(name);

    if (result.isSuccess()) {
        qCDebug(YubiKeyOathDeviceLog) << "Credential deleted successfully, triggering cache update";
        // Trigger credential cache refresh to remove deleted credential
        updateCredentialCacheAsync(m_password);
    }

    return result;
}

Result<void> OathDevice::changePassword(const QString &oldPassword, const QString &newPassword)
{
    qCDebug(YubiKeyOathDeviceLog) << "changePassword() for device" << m_deviceId;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks

    // Begin PC/SC transaction with automatic SELECT OATH
    const CardTransaction transaction(m_cardHandle, m_session.get());
    if (!transaction.isValid()) {
        qCWarning(YubiKeyOathDeviceLog) << "Transaction failed:" << transaction.errorMessage();
        return Result<void>::error(transaction.errorMessage());
    }

    // Change password via session (handles authentication internally, no longer does its own transaction/SELECT)
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

void OathDevice::updateCredentialCacheAsync(const QString& password)
{
    qCDebug(YubiKeyOathDeviceLog) << "updateCredentialCacheAsync() for device" << m_deviceId;

    if (m_updateInProgress) {
        qCDebug(YubiKeyOathDeviceLog) << "Update already in progress";
        return;
    }

    m_updateInProgress = true;

    // Set state to FetchingCredentials if not already in error state
    if (state() != Shared::DeviceState::Error) {
        setState(Shared::DeviceState::FetchingCredentials);
    }

    const QString passwordToUse = password.isEmpty() ? m_password : password;

    // Note: We don't store the QFuture because we communicate via signals/slots.
    // The m_updateInProgress flag tracks whether an update is running.
    [[maybe_unused]] auto future = QtConcurrent::run([this, passwordToUse]() {
        qCDebug(YubiKeyOathDeviceLog) << "Background thread started for credential fetch";

        const QList<OathCredential> credentials = this->fetchCredentialsSync(passwordToUse);

        qCDebug(YubiKeyOathDeviceLog) << "Fetched" << credentials.size() << "credentials in background thread";

        // Transition to Ready state on success
        // setState() is thread-safe (uses mutex + emits signal)
        const Shared::DeviceState currentState = state();
        qCDebug(YubiKeyOathDeviceLog) << "After fetch, current state:" << Shared::deviceStateToString(currentState);

        if (currentState == Shared::DeviceState::FetchingCredentials) {
            qCDebug(YubiKeyOathDeviceLog) << "Transitioning to Ready state";
            setState(Shared::DeviceState::Ready);
        } else {
            qCWarning(YubiKeyOathDeviceLog) << "NOT transitioning to Ready - state is" << Shared::deviceStateToString(currentState);
        }

        // Update credentials cache BEFORE emitting signal
        // This ensures cache is populated when signal handlers execute and when getCredentials() is called
        m_credentials = credentials;
        qCDebug(YubiKeyOathDeviceLog) << "Updated credentials cache with" << credentials.size() << "credentials";

        // Clear the update-in-progress flag
        m_updateInProgress = false;
        qCDebug(YubiKeyOathDeviceLog) << "Cleared updateInProgress flag";

        // Emit signal AFTER cache is updated
        // Signal handlers in derived classes are now redundant but kept for backwards compatibility
        Q_EMIT credentialCacheFetched(credentials);
    });
}

void OathDevice::cancelPendingOperation()
{
    qCDebug(YubiKeyOathDeviceLog) << "cancelPendingOperation() for device" << m_deviceId;

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks

    m_session->cancelOperation();
}

void OathDevice::onReconnectResult(bool success)
{
    qCDebug(YubiKeyOathDeviceLog) << "onReconnectResult() for device" << m_deviceId
             << "success:" << success;

    // Forward result to session to unblock waiting sendApdu()
    if (success) {
        qCInfo(YubiKeyOathDeviceLog) << "Reconnect successful, emitting reconnectReady to session";
        Q_EMIT m_session->reconnectReady();
    } else {
        qCWarning(YubiKeyOathDeviceLog) << "Reconnect failed, emitting reconnectFailed to session";
        Q_EMIT m_session->reconnectFailed();
    }
}

void OathDevice::onCardResetDetected(const QByteArray &command)
{
    qCDebug(YubiKeyOathDeviceLog) << "Card reset detected, emitting needsReconnect for device" << m_deviceId;
    Q_EMIT needsReconnect(m_deviceId, m_readerName, command);
}

QList<OathCredential> OathDevice::fetchCredentialsSync(const QString& password)
{
    qCDebug(YubiKeyOathDeviceLog) << "fetchCredentialsSync() for device" << m_deviceId;
    if (password.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "  - password parameter: EMPTY";
    } else {
        qCDebug(YubiKeyOathDeviceLog) << "  - password parameter: PROVIDED (length:" << password.length() << ")";
    }
    if (m_password.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "  - m_password member: EMPTY";
    } else {
        qCDebug(YubiKeyOathDeviceLog) << "  - m_password member: SET (length:" << m_password.data().length() << ")";
    }

    // Serialize card access to prevent race conditions between threads
    QMutexLocker locker(&m_cardMutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks

    // Begin PC/SC transaction with automatic SELECT OATH
    const CardTransaction transaction(m_cardHandle, m_session.get());
    if (!transaction.isValid()) {
        qCWarning(YubiKeyOathDeviceLog) << "Transaction failed:" << transaction.errorMessage();
        return {};
    }

    // Determine which password to use
    const QString devicePassword = password.isEmpty() ? m_password : password;

    // Authenticate if password required
    if (!devicePassword.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Authenticating within transaction before CALCULATE ALL";
        auto authResult = m_session->authenticate(devicePassword, m_deviceId);
        if (authResult.isSuccess()) {
            qCDebug(YubiKeyOathDeviceLog) << ">>> AUTHENTICATION SUCCESSFUL <<<";
            // Update stored password
            m_password = SecureMemory::SecureString(devicePassword);
        } else {
            qCWarning(YubiKeyOathDeviceLog) << ">>> AUTHENTICATION FAILED <<<";
            qCWarning(YubiKeyOathDeviceLog) << "Error:" << authResult.error();
            qCWarning(YubiKeyOathDeviceLog) << "Returning EMPTY credentials list";
            return {};
        }
    }

    // Use CALCULATE ALL to get credentials with codes (no longer does its own transaction/SELECT/auth)
    qCDebug(YubiKeyOathDeviceLog) << "Calling CALCULATE ALL within transaction";
    auto result = m_session->calculateAll();

    if (result.isError()) {
        qCWarning(YubiKeyOathDeviceLog) << "CALCULATE ALL FAILED with error:" << result.error();
        qCWarning(YubiKeyOathDeviceLog) << "Returning EMPTY credentials list";
        return {};
    }

    const QList<OathCredential> credentials = result.value();
    qCDebug(YubiKeyOathDeviceLog) << "Fetched" << credentials.size() << "credentials";

    // Log credential names for debugging
    if (!credentials.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Credential names:";
        for (const auto &cred : credentials) {
            qCDebug(YubiKeyOathDeviceLog) << "  -" << cred.originalName;
        }
    } else {
        qCWarning(YubiKeyOathDeviceLog) << ">>> CREDENTIALS LIST IS EMPTY <<<";
    }

    return credentials;
}

Result<void> OathDevice::reconnectCardHandle(const QString &readerName)
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
    static constexpr int delays[] = {100, 200, 400, 800, 1600, 3000};  // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    const int maxAttempts = 6;

    for (int attempt = 0; attempt < maxAttempts; attempt++) {
        if (attempt > 0) {
            qCDebug(YubiKeyOathDeviceLog) << "Reconnect attempt" << attempt
                     << "after" << delays[attempt - 1] << "ms delay";
            QThread::msleep(delays[attempt - 1]);
        }

        SCARDHANDLE newHandle = 0;
        DWORD activeProtocol = 0;
        const LONG result = SCardConnectWithTimeout(m_context,
                                              readerName.toUtf8().constData(),
                                              SCARD_SHARE_SHARED,
                                              SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1,
                                              &newHandle, &activeProtocol,
                                              2000);  // 2 second timeout

        if (result == SCARD_S_SUCCESS) {
            qCDebug(YubiKeyOathDeviceLog) << "SCardConnect successful, handle:" << newHandle;

            // 3. SELECT OATH applet to verify functionality
            // Use factory method to create brand-specific session
            auto tempSession = createTempSession(newHandle, activeProtocol);
            QByteArray challenge;
            Version firmwareVersion;  // Not used in reconnect verification
            auto selectResult = tempSession->selectOathApplication(challenge, firmwareVersion);

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
    return Result<void>::error(i18n("Failed to reconnect after multiple attempts"));
}

void OathDevice::setSessionRateLimitMs(qint64 intervalMs)
{
    if (m_session) {
        qCDebug(YubiKeyOathDeviceLog) << "Setting session rate limit to" << intervalMs << "ms for device" << m_deviceId;
        m_session->setRateLimitMs(intervalMs);
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
