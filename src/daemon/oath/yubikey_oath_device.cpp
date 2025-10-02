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

// PC/SC includes
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>

namespace KRunner {
namespace YubiKey {
#endif

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

    return m_session->calculateCode(name);
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

} // namespace YubiKey
} // namespace KRunner
