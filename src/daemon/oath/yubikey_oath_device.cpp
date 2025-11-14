/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_oath_device.h"
#include "yk_oath_session.h"
#include "oath_protocol.h"
#include "../logging_categories.h"
#include "shared/types/yubikey_model.h"

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

YubiKeyOathDevice::YubiKeyOathDevice(const QString &deviceId,
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
    m_session = std::make_unique<YkOathSession>(cardHandle, protocol, deviceId, this);

    qCDebug(YubiKeyOathDeviceLog) << "Created for device" << m_deviceId
             << "reader:" << m_readerName;

    // Forward signals from session
    connect(m_session.get(), &YkOathSession::touchRequired,
            this, &YubiKeyOathDevice::touchRequired);
    connect(m_session.get(), &YkOathSession::errorOccurred,
            this, &YubiKeyOathDevice::errorOccurred);
    connect(m_session.get(), &YkOathSession::cardResetDetected,
            this, &YubiKeyOathDevice::onCardResetDetected);

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
    // This uses Management interface for YubiKey 4/5 or OTP GET_SERIAL + reader name for NEO
    auto extResult = m_session->getExtendedDeviceInfo(m_readerName);
    if (extResult.isError()) {
        qCWarning(YubiKeyOathDeviceLog) << "Failed to get extended device info:" << extResult.error();
        // Fallback to firmware-based model detection
        const YubiKeyModel detectedModel = detectYubiKeyModel(m_firmwareVersion);
        m_deviceModel = toDeviceModel(detectedModel);
        qCDebug(YubiKeyOathDeviceLog) << "Using fallback model detection:" << m_deviceModel.modelString
                                       << "(" << QString::number(detectedModel, 16) << ")";
    } else {
        // Use precise data from Management/PIV interface
        const ExtendedDeviceInfo &extInfo = extResult.value();
        if (extInfo.firmwareVersion.isValid()) {
            m_firmwareVersion = extInfo.firmwareVersion;
        }
        m_deviceModel = toDeviceModel(extInfo.deviceModel);
        m_serialNumber = extInfo.serialNumber;
        m_formFactor = extInfo.formFactor;

        qCDebug(YubiKeyOathDeviceLog) << "Extended device info:"
                                       << "model=" << m_deviceModel.modelString
                                       << "(" << QString::number(m_deviceModel.modelCode, 16) << ")"
                                       << "serial=" << m_serialNumber
                                       << "formFactor=" << m_formFactor;
    }

    // NOTE: credentialCacheFetched signal handler is no longer needed here
    // m_credentials and m_updateInProgress are now updated directly in OathDevice::updateCredentialCacheAsync()
    // This eliminates the signal delivery race condition that caused empty credentials cache
    // The lambda handler below is commented out but kept for reference:
    /*
    connect(this, &OathDevice::credentialCacheFetched,
            this, [this](const QList<OathCredential> &credentials) {
                qCDebug(YubiKeyOathDeviceLog) << "Updating credential cache with" << credentials.size() << "credentials";
                m_credentials = credentials;
                m_updateInProgress = false;
                qCDebug(YubiKeyOathDeviceLog) << "Credential cache updated, updateInProgress reset to false";
            }, Qt::QueuedConnection);
    */
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

// NOTE: OATH Operations (generateCode, authenticateWithPassword, addCredential,
// deleteCredential, changePassword, setPassword, updateCredentialCacheAsync,
// cancelPendingOperation, onReconnectResult, onCardResetDetected,
// fetchCredentialsSync, reconnectCardHandle) are now implemented in
// OathDevice base class using polymorphic m_session and factory pattern


std::unique_ptr<YkOathSession> YubiKeyOathDevice::createTempSession(
    SCARDHANDLE handle,
    DWORD protocol)
{
    qCDebug(YubiKeyOathDeviceLog) << "Creating temporary YubiKey session for reconnect verification";
    return std::make_unique<YkOathSession>(handle, protocol, m_deviceId, this);
}

} // namespace Daemon
} // namespace YubiKeyOath
