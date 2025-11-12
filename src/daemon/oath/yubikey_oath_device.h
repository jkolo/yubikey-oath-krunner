/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "types/oath_credential.h"
#include "types/oath_credential_data.h"
#include "oath_device.h"
#include "yk_oath_session.h"
#include "common/result.h"
#include "shared/types/device_model.h"

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

namespace YubiKeyOath {
namespace Daemon {
#endif

/**
 * @brief YubiKey-specific OATH device implementation
 *
 * Single Responsibility: Handles communication with ONE YubiKey OATH application.
 * Each instance manages connection, authentication, and operations for a specific device.
 *
 * YubiKey-specific behavior:
 * - Creates YkOathSession internally (CALCULATE_ALL without fallback)
 * - Serial number via Management/PIV APIs (not in SELECT response)
 * - Touch required status word: 0x6985
 *
 * This class encapsulates all state and operations for a single YubiKey device,
 * following the Single Responsibility Principle. Methods do not require deviceId
 * parameter as the instance itself represents a specific device.
 */
class YubiKeyOathDevice : public OathDevice
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
     * @param requiresPassword Whether device requires password (from TAG_CHALLENGE presence in SELECT)
     * @param context PC/SC context (not owned, must outlive this object)
     * @param parent Parent QObject
     */
    explicit YubiKeyOathDevice(const QString &deviceId,
                               const QString &readerName,
                               SCARDHANDLE cardHandle,
                               DWORD protocol,
                               const QByteArray &challenge,
                               bool requiresPassword,
                               SCARDCONTEXT context,
                               QObject *parent = nullptr);

    /**
     * @brief Destructor - disconnects from device
     */
    ~YubiKeyOathDevice();

    // IOathDevice interface implementation - Device information getters
    /**
     * @brief Gets device ID (unique identifier)
     * @return Device ID as hex string
     */
    QString deviceId() const override { return m_deviceId; }

    /**
     * @brief Gets PC/SC reader name
     * @return Reader name
     */
    QString readerName() const override { return m_readerName; }

    /**
     * @brief Gets firmware version
     * @return Firmware version from TAG_VERSION (0x79)
     */
    Version firmwareVersion() const override { return m_firmwareVersion; }

    /**
     * @brief Gets device model
     * @return Device model with brand information
     */
    DeviceModel deviceModel() const override { return m_deviceModel; }

    /**
     * @brief Gets device serial number
     * @return Serial number (0 if unavailable)
     */
    quint32 serialNumber() const override { return m_serialNumber; }

    /**
     * @brief Gets password requirement status
     * @return true if device requires password (TAG_CHALLENGE present in SELECT), false otherwise
     */
    bool requiresPassword() const override { return m_requiresPassword; }

    /**
     * @brief Gets device form factor
     * @return Form factor code (0 if unavailable)
     */
    quint8 formFactor() const override { return m_formFactor; }

    /**
     * @brief Gets cached credentials
     * @return List of OATH credentials
     */
    QList<OathCredential> credentials() const override { return m_credentials; }

    /**
     * @brief Checks if credential cache update is in progress
     * @return true if async update running
     */
    bool isUpdateInProgress() const override { return m_updateInProgress; }

    // NOTE: All OATH operations (generateCode, authenticateWithPassword, addCredential,
    // deleteCredential, changePassword, setPassword, hasPassword,
    // updateCredentialCacheAsync, cancelPendingOperation, onReconnectResult,
    // fetchCredentialsSync, reconnectCardHandle) are now implemented in
    // OathDevice base class using polymorphic m_session and factory pattern

    // NOTE: All signals are declared in OathDevice base class
    // DO NOT redeclare signals here - it causes Qt signal shadowing where:
    // - Base class signal (OathDevice::credentialCacheFetched) is never emitted
    // - Derived class shadow signal (YubiKeyOathDevice::credentialCacheFetched) is emitted
    // - External connections to &OathDevice::credentialCacheFetched receive nothing
    // See: src/daemon/oath/oath_device.h for signal declarations

    // NOTE: onCardResetDetected slot is now implemented in OathDevice base class

protected:
    /**
     * @brief Factory method for creating temporary YubiKey session
     *
     * Creates a YkOathSession instance for temporary use during reconnect.
     * Implementation of pure virtual from OathDevice base class.
     *
     * @param handle PC/SC card handle
     * @param protocol PC/SC protocol
     * @return YubiKey-specific session instance
     */
    std::unique_ptr<YkOathSession> createTempSession(
        SCARDHANDLE handle,
        DWORD protocol) override;

private:
    // Note: All common device state members moved to OathDevice base class
    // (m_deviceId, m_readerName, m_cardHandle, m_protocol, m_context,
    //  m_challenge, m_firmwareVersion, m_deviceModel, m_serialNumber,
    //  m_formFactor, m_requiresPassword, m_credentials, m_password,
    //  m_updateInProgress, m_cardMutex, m_session)
};

} // namespace Daemon
} // namespace YubiKeyOath
