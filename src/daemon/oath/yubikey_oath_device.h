/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../../shared/types/oath_credential.h"
#include "oath_session.h"
#include "../../shared/common/result.h"

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <memory>

// Forward declarations for PC/SC types
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>

namespace KRunner {
namespace YubiKey {
#endif

/**
 * @brief Represents a single YubiKey OATH device
 *
 * Single Responsibility: Handles communication with ONE YubiKey OATH application.
 * Each instance manages connection, authentication, and operations for a specific device.
 *
 * This class encapsulates all state and operations for a single YubiKey device,
 * following the Single Responsibility Principle. Methods do not require deviceId
 * parameter as the instance itself represents a specific device.
 */
class YubiKeyOathDevice : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs YubiKey OATH device instance
     *
     * @param deviceId Unique device identifier (from SELECT response)
     * @param readerName PC/SC reader name
     * @param cardHandle PC/SC card handle (ownership transferred)
     * @param protocol PC/SC protocol
     * @param challenge Challenge from YubiKey SELECT
     * @param context PC/SC context (not owned, must outlive this object)
     * @param parent Parent QObject
     */
    explicit YubiKeyOathDevice(const QString &deviceId,
                               const QString &readerName,
                               SCARDHANDLE cardHandle,
                               DWORD protocol,
                               const QByteArray &challenge,
                               SCARDCONTEXT context,
                               QObject *parent = nullptr);

    /**
     * @brief Destructor - disconnects from device
     */
    ~YubiKeyOathDevice();

    // Device information
    /**
     * @brief Gets device ID (unique identifier)
     * @return Device ID as hex string
     */
    QString deviceId() const { return m_deviceId; }

    /**
     * @brief Gets PC/SC reader name
     * @return Reader name
     */
    QString readerName() const { return m_readerName; }

    /**
     * @brief Gets cached credentials
     * @return List of OATH credentials
     */
    QList<OathCredential> credentials() const { return m_credentials; }

    /**
     * @brief Checks if credential cache update is in progress
     * @return true if async update running
     */
    bool isUpdateInProgress() const { return m_updateInProgress; }

    // OATH operations
    /**
     * @brief Generates TOTP code for specified credential
     * @param name Credential name
     * @return Result containing generated code on success, or error message on failure
     */
    Result<QString> generateCode(const QString &name);

    /**
     * @brief Authenticates device with password
     * @param password Password to use
     * @return Result indicating success or containing error message
     */
    Result<void> authenticateWithPassword(const QString &password);

    /**
     * @brief Sets password for this device
     * @param password Password to set
     */
    void setPassword(const QString &password);

    /**
     * @brief Asynchronously updates credential cache
     *
     * Runs credential fetching in background thread.
     *
     * @param password Password for authentication if required
     *
     * Emits credentialCacheFetched(credentials) on completion
     */
    void updateCredentialCacheAsync(const QString &password = QString());

    /**
     * @brief Cancels pending touch operation
     *
     * Sends SELECT command to interrupt pending CALCULATE operation.
     */
    void cancelPendingOperation();

    /**
     * @brief Fetches credentials synchronously
     * @param password Password for authentication if required
     * @return List of credentials or empty on error
     *
     * This method performs all YubiKey communication synchronously.
     * Safe to call from background thread.
     */
    QList<OathCredential> fetchCredentialsSync(const QString &password = QString());

Q_SIGNALS:
    /**
     * @brief Emitted when YubiKey touch is required
     */
    void touchRequired();

    /**
     * @brief Emitted when an error occurs
     * @param error Error description
     */
    void errorOccurred(const QString &error);

    /**
     * @brief Emitted when credential list changes
     */
    void credentialsChanged();

    /**
     * @brief Emitted when asynchronous credential cache fetching completes
     * @param credentials List of fetched credentials
     */
    void credentialCacheFetched(const QList<OathCredential> &credentials);

private:
    // Device state
    QString m_deviceId;
    QString m_readerName;
    SCARDHANDLE m_cardHandle;
    DWORD m_protocol;
    SCARDCONTEXT m_context;  // Not owned
    QByteArray m_challenge;
    QList<OathCredential> m_credentials;
    QString m_password;
    bool m_updateInProgress = false;
    QMutex m_cardMutex;  // Protects card access from concurrent threads (used by OathSession)

    // OATH session
    std::unique_ptr<OathSession> m_session;  ///< OATH protocol session handler
};

} // namespace YubiKey
} // namespace KRunner
