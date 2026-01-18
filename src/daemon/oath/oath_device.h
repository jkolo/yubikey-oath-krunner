/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMutex>
#include <memory>
#include "types/oath_credential.h"
#include "types/oath_credential_data.h"
#include "types/device_state.h"
#include "common/result.h"
#include "shared/types/device_model.h"
#include "shared/utils/version.h"
#include "../utils/secure_memory.h"

// PC/SC forward declarations
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

// Forward declarations
class YkOathSession;

/**
 * @brief Abstract base class for OATH device implementations
 *
 * Base class for brand-specific OATH device implementations (YubiKey, Nitrokey, etc.)
 * Each device manages connection, authentication, and operations for a specific device.
 *
 * Single Responsibility Principle:
 * Each instance handles communication with ONE OATH device.
 * Methods do not require deviceId parameter - the instance represents a specific device.
 */
class OathDevice : public QObject
{
    Q_OBJECT

public:
    explicit OathDevice(QObject *parent = nullptr);
    ~OathDevice() override;

    // Device information getters (pure virtual - must be implemented)
    virtual QString deviceId() const = 0;
    virtual QString readerName() const = 0;
    virtual Version firmwareVersion() const = 0;
    virtual DeviceModel deviceModel() const = 0;
    virtual quint32 serialNumber() const = 0;
    virtual bool requiresPassword() const = 0;
    virtual quint8 formFactor() const = 0;
    virtual QList<OathCredential> credentials() const = 0;
    virtual bool isUpdateInProgress() const = 0;

    // State management
    /**
     * @brief Gets current device state
     * @return Current state (Disconnected, Connecting, Authenticating, etc.)
     */
    Shared::DeviceState state() const;

    /**
     * @brief Gets last error message (only valid when state == Error)
     * @return Error description or empty string if no error
     */
    QString lastError() const;

    /**
     * @brief Sets device state and emits stateChanged signal
     * @param state New state to set
     *
     * Thread-safe setter that emits stateChanged() signal.
     * Can be called by services to update device state during initialization.
     */
    void setState(Shared::DeviceState state);

    /**
     * @brief Sets device state to Error with error message
     * @param error Error description
     *
     * Convenience method for error states. Emits stateChanged(Error).
     */
    void setErrorState(const QString &error);

    // OATH operations (implemented in base class using polymorphic m_session)
    virtual Result<QString> generateCode(const QString &name);
    virtual Result<void> authenticateWithPassword(const QString &password);
    virtual Result<void> addCredential(const OathCredentialData &data);
    virtual Result<void> deleteCredential(const QString &name);
    virtual Result<void> changePassword(const QString &oldPassword, const QString &newPassword);
    virtual void setPassword(const QString &password);
    [[nodiscard]] virtual bool hasPassword() const { return !m_password.isEmpty(); }
    virtual void updateCredentialCacheAsync(const QString &password = QString());
    virtual void cancelPendingOperation();
    virtual void onReconnectResult(bool success);
    virtual Result<void> reconnectCardHandle(const QString &readerName);
    virtual QList<OathCredential> fetchCredentialsSync(const QString &password = QString());

    /**
     * @brief Sets PC/SC rate limit for session APDU operations
     * @param intervalMs Minimum milliseconds between operations (0 = no delay)
     *
     * Forwards to internal session's setRateLimitMs().
     */
    void setSessionRateLimitMs(qint64 intervalMs);

Q_SIGNALS:
    void touchRequired();
    void errorOccurred(const QString &error);
    void credentialsChanged();
    void credentialCacheFetched(const QList<OathCredential> &credentials);
    void needsReconnect(const QString &deviceId, const QString &readerName, const QByteArray &command);

    /**
     * @brief Emitted when device state changes
     * @param newState New device state
     *
     * Allows tracking async initialization progress:
     * - Connecting → Authenticating → FetchingCredentials → Ready
     * - Any state → Error on failure
     */
    void stateChanged(Shared::DeviceState newState);

protected Q_SLOTS:
    /**
     * @brief Handles cardResetDetected signal from session
     * @param command APDU command that failed due to card reset
     *
     * Forwards the signal as needsReconnect with device information.
     */
    void onCardResetDetected(const QByteArray &command);

protected:
    // Common member variables shared by all OATH device implementations
    // Moved from YubiKeyOathDevice and NitrokeyOathDevice to eliminate duplication

    // Device identification
    QString m_deviceId;
    QString m_readerName;

    // PC/SC handles
    SCARDHANDLE m_cardHandle{0};
    DWORD m_protocol{0};
    SCARDCONTEXT m_context{0};

    // Device info from SELECT response
    QByteArray m_challenge;
    Version m_firmwareVersion;
    DeviceModel m_deviceModel;
    quint32 m_serialNumber{0};
    quint8 m_formFactor{0};

    // Authentication state
    bool m_requiresPassword{false};
    SecureMemory::SecureString m_password;  // Secure storage, auto-wiped on destruction

    // Credential cache
    QList<OathCredential> m_credentials;
    bool m_updateInProgress{false};

    // Device state machine
    Shared::DeviceState m_state{Shared::DeviceState::Disconnected};
    QString m_lastError;
    mutable QMutex m_stateMutex;  // Protects m_state and m_lastError

    // Thread safety
    QMutex m_cardMutex;

    // OATH session (polymorphic base type)
    // Each derived class provides brand-specific session implementation
    std::unique_ptr<YkOathSession> m_session;

    /**
     * @brief Factory method for creating temporary session during reconnect
     *
     * This is needed because reconnectCardHandle() creates a temporary session
     * to verify card connection, and each brand needs different session type.
     *
     * @param handle PC/SC card handle
     * @param protocol PC/SC protocol
     * @return Brand-specific session instance
     */
    virtual std::unique_ptr<YkOathSession> createTempSession(
        SCARDHANDLE handle,
        DWORD protocol) = 0;
};

} // namespace Daemon
} // namespace YubiKeyOath
