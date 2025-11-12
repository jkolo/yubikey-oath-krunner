/*
 * SPDX-FileCopyrightText: 2024 Nitrokey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "nitrokey_oath_device.h"
#include "nitrokey_oath_session.h"
#include "nitrokey_model_detector.h"
#include "oath_protocol.h"
#include "../logging_categories.h"

#include <QDebug>
#include <QThread>

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

// NOTE: SCardConnectWithTimeout moved to oath_device.cpp (base class)

NitrokeyOathDevice::NitrokeyOathDevice(const QString &deviceId,
                                     const QString &readerName,
                                     SCARDHANDLE cardHandle,
                                     DWORD protocol,
                                     const QByteArray &challenge,
                                     bool requiresPassword,
                                     SCARDCONTEXT context,
                                     QObject *parent)
    : OathDevice(parent)
{
    // Initialize base class protected members
    m_deviceId = deviceId;
    m_readerName = readerName;
    m_cardHandle = cardHandle;
    m_protocol = protocol;
    m_context = context;
    m_challenge = challenge;
    m_requiresPassword = requiresPassword;

    // Initialize session after all members are set
    m_session = std::make_unique<NitrokeyOathSession>(cardHandle, protocol, deviceId, this);

    qCDebug(YubiKeyOathDeviceLog) << "Created for device" << m_deviceId
             << "reader:" << m_readerName;

    // Forward signals from session
    connect(m_session.get(), &NitrokeyOathSession::touchRequired,
            this, &NitrokeyOathDevice::touchRequired);
    connect(m_session.get(), &NitrokeyOathSession::errorOccurred,
            this, &NitrokeyOathDevice::errorOccurred);
    connect(m_session.get(), &NitrokeyOathSession::cardResetDetected,
            this, &NitrokeyOathDevice::onCardResetDetected);

    // Initialize OATH session immediately (following Yubico yubikey-manager pattern)
    // This ensures the session is active and ready for CALCULATE ALL without
    // executing SELECT before every request (major performance optimization)
    QByteArray sessionChallenge;
    auto selectResult = m_session->selectOathApplication(sessionChallenge, m_firmwareVersion);
    if (selectResult.isError()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to initialize OATH session:" << selectResult.error();
        // Continue anyway - session will retry on first operation if needed
    } else {
        qCDebug(YubiKeyOathDeviceLog) << "OATH session initialized successfully, firmware version:" << m_firmwareVersion.toString();
    }

    // Get extended device information (model, serial number, form factor)
    // Nitrokey uses SELECT command (0x79 tag) for firmware and serial
    auto extResult = m_session->getExtendedDeviceInfo(m_readerName);
    if (extResult.isError()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to get extended device info:" << extResult.error();
        // Fallback to Nitrokey-specific model detection from reader name + firmware
        const Shared::DeviceModel detectedModel = detectNitrokeyModel(m_readerName, m_firmwareVersion, 0);
        m_deviceModel = detectedModel;
        qCDebug(YubiKeyOathDeviceLog) << "Using Nitrokey model detection:"
                                       << detectedModel.modelString
                                       << "(" << QString::number(detectedModel.modelCode, 16) << ")";
    } else {
        // Use precise data from SELECT response
        const ExtendedDeviceInfo &extInfo = extResult.value();
        if (extInfo.firmwareVersion.isValid()) {
            m_firmwareVersion = extInfo.firmwareVersion;
        }
        m_serialNumber = extInfo.serialNumber;

        // Detect model with serial number for better variant detection
        const Shared::DeviceModel detectedModel = detectNitrokeyModel(m_readerName, m_firmwareVersion, m_serialNumber);
        m_deviceModel = detectedModel;
        m_formFactor = detectedModel.formFactor;

        qCDebug(YubiKeyOathDeviceLog) << "Nitrokey device info:"
                                       << "model=" << detectedModel.modelString
                                       << "(" << QString::number(detectedModel.modelCode, 16) << ")"
                                       << "serial=" << m_serialNumber
                                       << "formFactor=" << m_formFactor;
    }

    // Connect to our own credentialCacheFetched signal to update internal state
    // IMPORTANT: Qt::QueuedConnection required because signal is emitted from QtConcurrent thread
    connect(this, &NitrokeyOathDevice::credentialCacheFetched,
            this, [this](const QList<OathCredential> &credentials) {
                qCDebug(YubiKeyOathDeviceLog) << "Updating credential cache with" << credentials.size() << "credentials";
                m_credentials = credentials;
                m_updateInProgress = false;
                qCDebug(YubiKeyOathDeviceLog) << "Credential cache updated, updateInProgress reset to false";
            }, Qt::QueuedConnection);
}

NitrokeyOathDevice::~NitrokeyOathDevice()
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

// NOTE: OATH Operations (generateCode, authenticateWithPassword, addCredential,
// deleteCredential, changePassword, setPassword, updateCredentialCacheAsync,
// cancelPendingOperation, onReconnectResult, onCardResetDetected,
// fetchCredentialsSync, reconnectCardHandle) are now implemented in
// OathDevice base class using polymorphic m_session and factory pattern


std::unique_ptr<YkOathSession> NitrokeyOathDevice::createTempSession(
    SCARDHANDLE handle,
    DWORD protocol)
{
    qCDebug(YubiKeyOathDeviceLog) << "Creating temporary Nitrokey session for reconnect verification";
    // Cast to base type (YkOathSession) for polymorphism
    return std::make_unique<NitrokeyOathSession>(handle, protocol, m_deviceId, this);
}

} // namespace Daemon
} // namespace YubiKeyOath
