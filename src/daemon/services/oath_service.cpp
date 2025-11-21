/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_service.h"
#include "password_service.h"
#include "device_lifecycle_service.h"
#include "credential_service.h"
#include "../oath/oath_device_manager.h"
#include "types/oath_credential_data.h"
#include "../storage/oath_database.h"
#include "../storage/secret_storage.h"
#include "../config/daemon_configuration.h"
#include "../actions/oath_action_coordinator.h"
#include "../workflows/notification_orchestrator.h"
#include "../logging_categories.h"
#include "../utils/qr_code_parser.h"
#include "../utils/otpauth_uri_parser.h"
#include "../ui/add_credential_dialog.h"
#include "utils/device_name_formatter.h"

#include <QDateTime>
#include <QDebug>
#include <QVariantMap>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <KLocalizedString>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

OathService::OathService(QObject *parent)
    : QObject(parent)
    , m_deviceManager(std::make_unique<OathDeviceManager>(nullptr))
    , m_database(std::make_unique<OathDatabase>(nullptr))
    , m_secretStorage(std::make_unique<SecretStorage>(nullptr))
    , m_config(std::make_unique<DaemonConfiguration>(this))
    , m_actionCoordinator(std::make_unique<OathActionCoordinator>(this, m_deviceManager.get(), m_database.get(), m_secretStorage.get(), m_config.get(), this))
    , m_passwordService(std::make_unique<PasswordService>(m_deviceManager.get(), m_database.get(), m_secretStorage.get(), this))
    , m_deviceLifecycleService(std::make_unique<DeviceLifecycleService>(m_deviceManager.get(), m_database.get(), m_secretStorage.get(), this))
    , m_credentialService(std::make_unique<CredentialService>(m_deviceManager.get(), m_database.get(), m_config.get(), this))
{
    qCDebug(OathDaemonLog) << "OathService: Initializing";

    // Initialize database
    if (!m_database->initialize()) {
        qCWarning(OathDaemonLog) << "OathService: Failed to initialize database";
    }

    // Initialize OATH
    auto initResult = m_deviceManager->initialize();
    if (initResult.isError()) {
        qCWarning(OathDaemonLog) << "OathService: Failed to initialize OATH:" << initResult.error();
    }

    // Connect device lifecycle signals (delegate to DeviceLifecycleService)
    connect(m_deviceManager.get(), &OathDeviceManager::deviceConnected,
            m_deviceLifecycleService.get(), &DeviceLifecycleService::onDeviceConnected);
    connect(m_deviceManager.get(), &OathDeviceManager::deviceDisconnected,
            m_deviceLifecycleService.get(), &DeviceLifecycleService::onDeviceDisconnected);

    // Forward device lifecycle signals from service
    connect(m_deviceLifecycleService.get(), &DeviceLifecycleService::deviceConnected,
            this, &OathService::deviceConnected);
    connect(m_deviceLifecycleService.get(), &DeviceLifecycleService::deviceDisconnected,
            this, &OathService::deviceDisconnected);
    connect(m_deviceManager.get(), &OathDeviceManager::deviceForgotten,
            this, &OathService::deviceForgotten);  // Forward signal directly

    // Forward credential signals from service
    connect(m_credentialService.get(), &CredentialService::credentialsUpdated,
            this, &OathService::credentialsUpdated);
    connect(m_deviceManager.get(), &OathDeviceManager::credentialCacheFetchedForDevice,
            this, &OathService::onCredentialCacheFetched);
    connect(m_deviceManager.get(), &OathDeviceManager::reconnectStarted,
            this, &OathService::onReconnectStarted);
    connect(m_deviceManager.get(), &OathDeviceManager::reconnectCompleted,
            this, &OathService::onReconnectCompleted);
    connect(m_config.get(), &DaemonConfiguration::configurationChanged,
            this, &OathService::onConfigurationChanged);

    // ASYNC: Device initialization happens via deviceConnected signal from OathDeviceManager
    // The manager's initialize() now triggers async device enumeration which will emit
    // deviceConnected signals, and those are already connected to onDeviceConnected above (line 60-61).
    // This removes the blocking loop that was causing 5-30s startup delay.

    qCDebug(OathDaemonLog) << "OathService: Initialization complete (async device enumeration in progress)";
}

OathService::~OathService()
{
    qCDebug(OathDaemonLog) << "OathService: Destructor";

    if (m_deviceManager) {
        m_deviceManager->cleanup();
    }
}

QList<DeviceInfo> OathService::listDevices()
{
    qCDebug(OathDaemonLog) << "OathService: Delegating listDevices to DeviceLifecycleService";
    return m_deviceLifecycleService->listDevices();
}


QList<OathCredential> OathService::getCredentials(const QString &deviceId)
{
    qCDebug(OathDaemonLog) << "OathService: Delegating getCredentials to CredentialService";
    return m_credentialService->getCredentials(deviceId);
}

// ICredentialUpdateNotifier interface implementation
QList<OathCredential> OathService::getCredentials()
{
    qCDebug(OathDaemonLog) << "OathService: Delegating getCredentials (all devices) to CredentialService";
    return m_credentialService->getCredentials();
}

OathDevice* OathService::getDevice(const QString &deviceId)
{
    return m_deviceLifecycleService->getDevice(deviceId);
}

CredentialService* OathService::getCredentialService() const
{
    return m_credentialService.get();
}

QList<QString> OathService::getConnectedDeviceIds() const
{
    return m_deviceLifecycleService->getConnectedDeviceIds();
}

QDateTime OathService::getDeviceLastSeen(const QString &deviceId) const
{
    return m_deviceLifecycleService->getDeviceLastSeen(deviceId);
}

GenerateCodeResult OathService::generateCode(const QString &deviceId,
                                                 const QString &credentialName)
{
    qCDebug(OathDaemonLog) << "OathService: Delegating generateCode to CredentialService";
    return m_credentialService->generateCode(deviceId, credentialName);
}

bool OathService::savePassword(const QString &deviceId, const QString &password)
{
    qCDebug(OathDaemonLog) << "OathService: Delegating savePassword to PasswordService";
    return m_passwordService->savePassword(deviceId, password);
}

bool OathService::changePassword(const QString &deviceId,
                                    const QString &oldPassword,
                                    const QString &newPassword)
{
    qCDebug(OathDaemonLog) << "OathService: Delegating changePassword to PasswordService";
    return m_passwordService->changePassword(deviceId, oldPassword, newPassword);
}

void OathService::forgetDevice(const QString &deviceId)
{
    qCDebug(OathDaemonLog) << "OathService: Delegating forgetDevice to DeviceLifecycleService";
    m_deviceLifecycleService->forgetDevice(deviceId);
}

bool OathService::setDeviceName(const QString &deviceId, const QString &newName)
{
    qCDebug(OathDaemonLog) << "OathService: Delegating setDeviceName to DeviceLifecycleService";
    return m_deviceLifecycleService->setDeviceName(deviceId, newName);
}

AddCredentialResult OathService::addCredential(const QString &deviceId,
                                                  const QString &name,
                                                  const QString &secret,
                                                  const QString &type,
                                                  const QString &algorithm,
                                                  int digits,
                                                  int period,
                                                  int counter,
                                                  bool requireTouch)
{
    qCDebug(OathDaemonLog) << "OathService: Delegating addCredential to CredentialService";
    return m_credentialService->addCredential(deviceId, name, secret, type, algorithm, digits, period, counter, requireTouch);
}

bool OathService::deleteCredential(const QString &deviceId, const QString &credentialName)
{
    qCDebug(OathDaemonLog) << "OathService: Delegating deleteCredential to CredentialService";
    return m_credentialService->deleteCredential(deviceId, credentialName);
}
bool OathService::copyCodeToClipboard(const QString &deviceId, const QString &credentialName)
{
    qCDebug(OathDaemonLog) << "OathService: copyCodeToClipboard" << credentialName << "device:" << deviceId;
    return m_actionCoordinator->copyCodeToClipboard(deviceId, credentialName);
}

bool OathService::typeCode(const QString &deviceId, const QString &credentialName)
{
    qCDebug(OathDaemonLog) << "OathService: typeCode" << credentialName << "device:" << deviceId;
    return m_actionCoordinator->typeCode(deviceId, credentialName);
}

void OathService::onCredentialCacheFetched(const QString &deviceId,
                                             const QList<OathCredential> &credentials)
{
    qWarning() << "OathService: >>> onCredentialCacheFetched CALLED for device:" << deviceId
               << "count:" << credentials.size();

    // Log credential names for debugging
    if (!credentials.isEmpty()) {
        qCDebug(OathDaemonLog) << "OathService: Credentials list:";
        for (const auto &cred : credentials) {
            qCDebug(OathDaemonLog) << "  - " << cred.originalName;
        }
    } else {
        qCDebug(OathDaemonLog) << "OathService: Credentials list is EMPTY";
    }

    // Get device instance to check authentication state
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(OathDaemonLog) << "OathService: Device disappeared during credential fetch:" << deviceId;
        return;
    }

    // Check authentication state (helper extracts authentication logic)
    bool authenticationFailed = false;
    QString authError;
    qCDebug(OathDaemonLog) << "OathService: >>> Calling checkAuthenticationState";
    checkAuthenticationState(deviceId, device, credentials, authenticationFailed, authError);
    qCDebug(OathDaemonLog) << "OathService: >>> checkAuthenticationState returned: authenticationFailed=" << authenticationFailed;

    // Handle authentication result - delegates to appropriate handler
    if (authenticationFailed) {
        qCDebug(OathDaemonLog) << "OathService: >>> Authentication FAILED, calling handleAuthenticationFailure";
        handleAuthenticationFailure(deviceId, authError);
    } else {
        qCDebug(OathDaemonLog) << "OathService: >>> Authentication SUCCESS, checking rate limit";
        // If rate-limited, still emit signals but skip database save
        if (!shouldSaveCredentialsToCache(deviceId)) {
            qCDebug(OathDaemonLog) << "OathService: >>> Rate-limited - emitting signals without saving";
            Q_EMIT credentialsUpdated(deviceId);
            Q_EMIT deviceConnectedAndAuthenticated(deviceId);
            return;
        }

        qCDebug(OathDaemonLog) << "OathService: >>> Not rate-limited, calling handleAuthenticationSuccess";
        handleAuthenticationSuccess(deviceId, device, credentials);
    }
}

void OathService::onReconnectStarted(const QString &deviceId)
{
    qCDebug(OathDaemonLog) << "OathService: Reconnect started for device:" << deviceId;

    // Show persistent notification that reconnect is in progress
    if (m_config->showNotifications() && m_actionCoordinator) {
        const QString deviceName = m_deviceLifecycleService->getDeviceName(deviceId);
        const QString title = i18n("Reconnecting to YubiKey");
        const QString message = i18n("Restoring connection to %1...").arg(deviceName);

        // Show persistent notification (no timeout) - will be closed when reconnect completes
        m_reconnectNotificationId = m_actionCoordinator->showPersistentNotification(title, message, 0);
        qCDebug(OathDaemonLog) << "OathService: Reconnect notification shown with ID:" << m_reconnectNotificationId;
    }
}

void OathService::onReconnectCompleted(const QString &deviceId, bool success)
{
    qCDebug(OathDaemonLog) << "OathService: Reconnect completed for device:" << deviceId
                              << "success:" << success;

    if (!m_config->showNotifications() || !m_actionCoordinator) {
        return;
    }

    // Get device name for user-friendly notification
    const QString deviceName = m_deviceLifecycleService->getDeviceName(deviceId);

    if (success) {
        // Success - close the reconnecting notification
        qCDebug(OathDaemonLog) << "OathService: Reconnect successful, closing notification ID:" << m_reconnectNotificationId;
        m_actionCoordinator->closeNotification(m_reconnectNotificationId);
        m_reconnectNotificationId = 0;
    } else {
        // Failure - close reconnecting notification and show error
        qCDebug(OathDaemonLog) << "OathService: Reconnect failed, closing old notification and showing error";
        m_actionCoordinator->closeNotification(m_reconnectNotificationId);
        m_reconnectNotificationId = 0;

        const QString title = i18n("Reconnect Failed");
        const QString message = i18n("Could not restore connection to %1. Please remove and reinsert the YubiKey.").arg(deviceName);

        m_actionCoordinator->showSimpleNotification(title, message, 1);
    }
}

void OathService::onConfigurationChanged()
{
    qCDebug(OathDaemonLog) << "OathService: Configuration changed";

    // Check if credentials cache was disabled
    if (!m_config->enableCredentialsCache()) {
        qCDebug(OathDaemonLog) << "OathService: Credentials cache disabled, clearing all cached credentials";
        if (!m_database->clearAllCredentials()) {
            qCWarning(OathDaemonLog) << "OathService: Failed to clear cached credentials";
        } else {
            qCDebug(OathDaemonLog) << "OathService: All cached credentials cleared successfully";
        }
    }
}

void OathService::checkAuthenticationState(const QString &deviceId,
                                               OathDevice *device,
                                               const QList<OathCredential> &credentials,
                                               bool &authenticationFailed,
                                               QString &authError)
{
    // Check if device requires password from database
    bool requiresPassword = false;
    auto dbRecord = m_database->getDevice(deviceId);
    if (dbRecord.has_value()) {
        requiresPassword = dbRecord->requiresPassword;
    }

    qCDebug(OathDaemonLog) << "OathService: Authentication state check:";
    qCDebug(OathDaemonLog) << "  - credentials.isEmpty():" << credentials.isEmpty();
    qCDebug(OathDaemonLog) << "  - requiresPassword:" << requiresPassword;
    qCDebug(OathDaemonLog) << "  - device->hasPassword():" << device->hasPassword();

    // AUTHENTICATION DETECTION LOGIC:
    // Empty credentials + requires password + has password = authentication failed (wrong password)
    // Empty credentials + requires password + no password = authentication failed (no password available)
    // Empty credentials + !requires password = device has no credentials (success)
    // Non-empty credentials = authentication succeeded (or not required)

    authenticationFailed = false;
    authError.clear();

    if (credentials.isEmpty() && requiresPassword) {
        qCDebug(OathDaemonLog) << "OathService: Empty credentials for password-protected device - authentication failed";
        authenticationFailed = true;

        if (device->hasPassword()) {
            qCWarning(OathDaemonLog) << "OathService: MARKING AS WRONG PASSWORD - device has password but returned empty credentials";
            authError = i18n("Wrong password");
        } else {
            qCWarning(OathDaemonLog) << "OathService: MARKING AS NO PASSWORD - device requires password but none available";
            authError = i18n("Password required but not available");
        }
    } else {
        qCDebug(OathDaemonLog) << "OathService: Authentication check passed - proceeding";
    }

    // AUTO-DETECT: If credentials were fetched successfully without password, device doesn't require password
    if (!authenticationFailed && !device->hasPassword() && !credentials.isEmpty()) {
        qCDebug(OathDaemonLog) << "OathService: Auto-detected - device doesn't require password";
        m_database->setRequiresPassword(deviceId, false);
    }
}

void OathService::handleAuthenticationFailure(const QString &deviceId,
                                                 const QString &authError)
{
    qCWarning(OathDaemonLog) << "OathService: Authentication failed for device:" << deviceId
                                 << "error:" << authError;
    qCWarning(OathDaemonLog) << "OathService: >>> EMITTING deviceConnectedAuthenticationFailed signal";

    // DO NOT emit credentialsUpdated for auth failures
    // Emit authentication failure signal instead
    Q_EMIT deviceConnectedAuthenticationFailed(deviceId, authError);
}

void OathService::handleAuthenticationSuccess(const QString &deviceId,
                                                 OathDevice *device,
                                                 const QList<OathCredential> &credentials)
{
    Q_UNUSED(device)  // device parameter not used yet, but may be needed for future extensions

    // Save credentials to cache if enabled and rate limit allows
    if (m_config->enableCredentialsCache() && shouldSaveCredentialsToCache(deviceId)) {
        qCDebug(OathDaemonLog) << "OathService: Credentials cache enabled, saving" << credentials.size() << "credentials";
        if (!m_database->saveCredentials(deviceId, credentials)) {
            qCWarning(OathDaemonLog) << "OathService: Failed to save credentials to cache";
        } else {
            qCDebug(OathDaemonLog) << "OathService: Credentials saved to cache successfully";
            const QMutexLocker locker(&m_lastCredentialSaveMutex);
            m_lastCredentialSave[deviceId] = QDateTime::currentMSecsSinceEpoch();
        }
    } else if (!m_config->enableCredentialsCache()) {
        qCDebug(OathDaemonLog) << "OathService: Credentials cache disabled, NOT saving to database";
    }

    qCDebug(OathDaemonLog) << "OathService: Authentication successful for device:" << deviceId;
    qCDebug(OathDaemonLog) << "OathService: >>> EMITTING credentialsUpdated and deviceConnectedAndAuthenticated signals";

    // Emit both signals: credentialsUpdated (for backward compat) and deviceConnectedAndAuthenticated (new)
    Q_EMIT credentialsUpdated(deviceId);

    qCDebug(OathDaemonLog) << "OathService: this=" << this << "thread=" << QThread::currentThreadId()
                              << "about to emit deviceConnectedAndAuthenticated for device:" << deviceId;
    Q_EMIT deviceConnectedAndAuthenticated(deviceId);
    qCDebug(OathDaemonLog) << "OathService: deviceConnectedAndAuthenticated signal emitted successfully";
}

bool OathService::shouldSaveCredentialsToCache(const QString &deviceId)
{
    // Rate limiting check - prevent excessive database writes
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int rateLimitMs = m_config->credentialSaveRateLimit();

    // Thread-safe access to m_lastCredentialSave
    const QMutexLocker locker(&m_lastCredentialSaveMutex);
    if (m_lastCredentialSave.contains(deviceId)) {
        const qint64 timeSinceLastSave = now - m_lastCredentialSave[deviceId];
        if (timeSinceLastSave < rateLimitMs) {
            qCDebug(OathDaemonLog)
                << "OathService: Rate limited credential save for" << deviceId
                << "- last save" << timeSinceLastSave << "ms ago"
                << "(limit:" << rateLimitMs << "ms)";
            return false;
        }
    }
    return true;
}

} // namespace Daemon
} // namespace YubiKeyOath
