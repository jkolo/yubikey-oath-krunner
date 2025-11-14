/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_service.h"
#include "password_service.h"
#include "device_lifecycle_service.h"
#include "credential_service.h"
#include "../oath/yubikey_device_manager.h"
#include "types/oath_credential_data.h"
#include "../storage/yubikey_database.h"
#include "../storage/secret_storage.h"
#include "../config/daemon_configuration.h"
#include "../actions/yubikey_action_coordinator.h"
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

YubiKeyService::YubiKeyService(QObject *parent)
    : QObject(parent)
    , m_deviceManager(std::make_unique<YubiKeyDeviceManager>(nullptr))
    , m_database(std::make_unique<YubiKeyDatabase>(nullptr))
    , m_secretStorage(std::make_unique<SecretStorage>(nullptr))
    , m_config(std::make_unique<DaemonConfiguration>(this))
    , m_actionCoordinator(std::make_unique<YubiKeyActionCoordinator>(this, m_deviceManager.get(), m_database.get(), m_secretStorage.get(), m_config.get(), this))
    , m_passwordService(std::make_unique<PasswordService>(m_deviceManager.get(), m_database.get(), m_secretStorage.get(), this))
    , m_deviceLifecycleService(std::make_unique<DeviceLifecycleService>(m_deviceManager.get(), m_database.get(), m_secretStorage.get(), this))
    , m_credentialService(std::make_unique<CredentialService>(m_deviceManager.get(), m_database.get(), m_config.get(), this))
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Initializing";

    // Initialize database
    if (!m_database->initialize()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to initialize database";
    }

    // Initialize OATH
    auto initResult = m_deviceManager->initialize();
    if (initResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to initialize OATH:" << initResult.error();
    }

    // Connect device lifecycle signals (delegate to DeviceLifecycleService)
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::deviceConnected,
            m_deviceLifecycleService.get(), &DeviceLifecycleService::onDeviceConnected);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::deviceDisconnected,
            m_deviceLifecycleService.get(), &DeviceLifecycleService::onDeviceDisconnected);

    // Forward device lifecycle signals from service
    connect(m_deviceLifecycleService.get(), &DeviceLifecycleService::deviceConnected,
            this, &YubiKeyService::deviceConnected);
    connect(m_deviceLifecycleService.get(), &DeviceLifecycleService::deviceDisconnected,
            this, &YubiKeyService::deviceDisconnected);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::deviceForgotten,
            this, &YubiKeyService::deviceForgotten);  // Forward signal directly

    // Forward credential signals from service
    connect(m_credentialService.get(), &CredentialService::credentialsUpdated,
            this, &YubiKeyService::credentialsUpdated);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::credentialCacheFetchedForDevice,
            this, &YubiKeyService::onCredentialCacheFetched);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::reconnectStarted,
            this, &YubiKeyService::onReconnectStarted);
    connect(m_deviceManager.get(), &YubiKeyDeviceManager::reconnectCompleted,
            this, &YubiKeyService::onReconnectCompleted);
    connect(m_config.get(), &DaemonConfiguration::configurationChanged,
            this, &YubiKeyService::onConfigurationChanged);

    // ASYNC: Device initialization happens via deviceConnected signal from YubiKeyDeviceManager
    // The manager's initialize() now triggers async device enumeration which will emit
    // deviceConnected signals, and those are already connected to onDeviceConnected above (line 60-61).
    // This removes the blocking loop that was causing 5-30s startup delay.

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Initialization complete (async device enumeration in progress)";
}

YubiKeyService::~YubiKeyService()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Destructor";

    if (m_deviceManager) {
        m_deviceManager->cleanup();
    }
}

QList<DeviceInfo> YubiKeyService::listDevices()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating listDevices to DeviceLifecycleService";
    return m_deviceLifecycleService->listDevices();
}


QList<OathCredential> YubiKeyService::getCredentials(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating getCredentials to CredentialService";
    return m_credentialService->getCredentials(deviceId);
}

// ICredentialUpdateNotifier interface implementation
QList<OathCredential> YubiKeyService::getCredentials()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating getCredentials (all devices) to CredentialService";
    return m_credentialService->getCredentials();
}

OathDevice* YubiKeyService::getDevice(const QString &deviceId)
{
    return m_deviceLifecycleService->getDevice(deviceId);
}

CredentialService* YubiKeyService::getCredentialService() const
{
    return m_credentialService.get();
}

QList<QString> YubiKeyService::getConnectedDeviceIds() const
{
    return m_deviceLifecycleService->getConnectedDeviceIds();
}

QDateTime YubiKeyService::getDeviceLastSeen(const QString &deviceId) const
{
    return m_deviceLifecycleService->getDeviceLastSeen(deviceId);
}

GenerateCodeResult YubiKeyService::generateCode(const QString &deviceId,
                                                 const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating generateCode to CredentialService";
    return m_credentialService->generateCode(deviceId, credentialName);
}

bool YubiKeyService::savePassword(const QString &deviceId, const QString &password)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating savePassword to PasswordService";
    return m_passwordService->savePassword(deviceId, password);
}

bool YubiKeyService::changePassword(const QString &deviceId,
                                    const QString &oldPassword,
                                    const QString &newPassword)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating changePassword to PasswordService";
    return m_passwordService->changePassword(deviceId, oldPassword, newPassword);
}

void YubiKeyService::forgetDevice(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating forgetDevice to DeviceLifecycleService";
    m_deviceLifecycleService->forgetDevice(deviceId);
}

bool YubiKeyService::setDeviceName(const QString &deviceId, const QString &newName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating setDeviceName to DeviceLifecycleService";
    return m_deviceLifecycleService->setDeviceName(deviceId, newName);
}

AddCredentialResult YubiKeyService::addCredential(const QString &deviceId,
                                                  const QString &name,
                                                  const QString &secret,
                                                  const QString &type,
                                                  const QString &algorithm,
                                                  int digits,
                                                  int period,
                                                  int counter,
                                                  bool requireTouch)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating addCredential to CredentialService";
    return m_credentialService->addCredential(deviceId, name, secret, type, algorithm, digits, period, counter, requireTouch);
}

bool YubiKeyService::deleteCredential(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Delegating deleteCredential to CredentialService";
    return m_credentialService->deleteCredential(deviceId, credentialName);
}
bool YubiKeyService::copyCodeToClipboard(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: copyCodeToClipboard" << credentialName << "device:" << deviceId;
    return m_actionCoordinator->copyCodeToClipboard(deviceId, credentialName);
}

bool YubiKeyService::typeCode(const QString &deviceId, const QString &credentialName)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: typeCode" << credentialName << "device:" << deviceId;
    return m_actionCoordinator->typeCode(deviceId, credentialName);
}

void YubiKeyService::onCredentialCacheFetched(const QString &deviceId,
                                             const QList<OathCredential> &credentials)
{
    qWarning() << "YubiKeyService: >>> onCredentialCacheFetched CALLED for device:" << deviceId
               << "count:" << credentials.size();

    // Log credential names for debugging
    if (!credentials.isEmpty()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials list:";
        for (const auto &cred : credentials) {
            qCDebug(YubiKeyDaemonLog) << "  - " << cred.originalName;
        }
    } else {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials list is EMPTY";
    }

    // Get device instance to check authentication state
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Device disappeared during credential fetch:" << deviceId;
        return;
    }

    // Check authentication state (helper extracts authentication logic)
    bool authenticationFailed = false;
    QString authError;
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: >>> Calling checkAuthenticationState";
    checkAuthenticationState(deviceId, device, credentials, authenticationFailed, authError);
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: >>> checkAuthenticationState returned: authenticationFailed=" << authenticationFailed;

    // Handle authentication result - delegates to appropriate handler
    if (authenticationFailed) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: >>> Authentication FAILED, calling handleAuthenticationFailure";
        handleAuthenticationFailure(deviceId, authError);
    } else {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: >>> Authentication SUCCESS, checking rate limit";
        // If rate-limited, still emit signals but skip database save
        if (!shouldSaveCredentialsToCache(deviceId)) {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: >>> Rate-limited - emitting signals without saving";
            Q_EMIT credentialsUpdated(deviceId);
            Q_EMIT deviceConnectedAndAuthenticated(deviceId);
            return;
        }

        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: >>> Not rate-limited, calling handleAuthenticationSuccess";
        handleAuthenticationSuccess(deviceId, device, credentials);
    }
}

void YubiKeyService::onReconnectStarted(const QString &deviceId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect started for device:" << deviceId;

    // Show persistent notification that reconnect is in progress
    if (m_config->showNotifications() && m_actionCoordinator) {
        const QString deviceName = m_deviceLifecycleService->getDeviceName(deviceId);
        const QString title = tr("Reconnecting to YubiKey");
        const QString message = tr("Restoring connection to %1...").arg(deviceName);

        // Show persistent notification (no timeout) - will be closed when reconnect completes
        m_reconnectNotificationId = m_actionCoordinator->showPersistentNotification(title, message, 0);
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect notification shown with ID:" << m_reconnectNotificationId;
    }
}

void YubiKeyService::onReconnectCompleted(const QString &deviceId, bool success)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect completed for device:" << deviceId
                              << "success:" << success;

    if (!m_config->showNotifications() || !m_actionCoordinator) {
        return;
    }

    // Get device name for user-friendly notification
    const QString deviceName = m_deviceLifecycleService->getDeviceName(deviceId);

    if (success) {
        // Success - close the reconnecting notification
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect successful, closing notification ID:" << m_reconnectNotificationId;
        m_actionCoordinator->closeNotification(m_reconnectNotificationId);
        m_reconnectNotificationId = 0;
    } else {
        // Failure - close reconnecting notification and show error
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Reconnect failed, closing old notification and showing error";
        m_actionCoordinator->closeNotification(m_reconnectNotificationId);
        m_reconnectNotificationId = 0;

        const QString title = tr("Reconnect Failed");
        const QString message = tr("Could not restore connection to %1. Please remove and reinsert the YubiKey.").arg(deviceName);

        m_actionCoordinator->showSimpleNotification(title, message, 1);
    }
}

void YubiKeyService::onConfigurationChanged()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Configuration changed";

    // Check if credentials cache was disabled
    if (!m_config->enableCredentialsCache()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials cache disabled, clearing all cached credentials";
        if (!m_database->clearAllCredentials()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to clear cached credentials";
        } else {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: All cached credentials cleared successfully";
        }
    }
}

void YubiKeyService::checkAuthenticationState(const QString &deviceId,
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

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Authentication state check:";
    qCDebug(YubiKeyDaemonLog) << "  - credentials.isEmpty():" << credentials.isEmpty();
    qCDebug(YubiKeyDaemonLog) << "  - requiresPassword:" << requiresPassword;
    qCDebug(YubiKeyDaemonLog) << "  - device->hasPassword():" << device->hasPassword();

    // AUTHENTICATION DETECTION LOGIC:
    // Empty credentials + requires password + has password = authentication failed (wrong password)
    // Empty credentials + requires password + no password = authentication failed (no password available)
    // Empty credentials + !requires password = device has no credentials (success)
    // Non-empty credentials = authentication succeeded (or not required)

    authenticationFailed = false;
    authError.clear();

    if (credentials.isEmpty() && requiresPassword) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Empty credentials for password-protected device - authentication failed";
        authenticationFailed = true;

        if (device->hasPassword()) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: MARKING AS WRONG PASSWORD - device has password but returned empty credentials";
            authError = tr("Wrong password");
        } else {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: MARKING AS NO PASSWORD - device requires password but none available";
            authError = tr("Password required but not available");
        }
    } else {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Authentication check passed - proceeding";
    }

    // AUTO-DETECT: If credentials were fetched successfully without password, device doesn't require password
    if (!authenticationFailed && !device->hasPassword() && !credentials.isEmpty()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Auto-detected - device doesn't require password";
        m_database->setRequiresPassword(deviceId, false);
    }
}

void YubiKeyService::handleAuthenticationFailure(const QString &deviceId,
                                                 const QString &authError)
{
    qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Authentication failed for device:" << deviceId
                                 << "error:" << authError;
    qCWarning(YubiKeyDaemonLog) << "YubiKeyService: >>> EMITTING deviceConnectedAuthenticationFailed signal";

    // DO NOT emit credentialsUpdated for auth failures
    // Emit authentication failure signal instead
    Q_EMIT deviceConnectedAuthenticationFailed(deviceId, authError);
}

void YubiKeyService::handleAuthenticationSuccess(const QString &deviceId,
                                                 OathDevice *device,
                                                 const QList<OathCredential> &credentials)
{
    Q_UNUSED(device)  // device parameter not used yet, but may be needed for future extensions

    // Save credentials to cache if enabled and rate limit allows
    if (m_config->enableCredentialsCache() && shouldSaveCredentialsToCache(deviceId)) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials cache enabled, saving" << credentials.size() << "credentials";
        if (!m_database->saveCredentials(deviceId, credentials)) {
            qCWarning(YubiKeyDaemonLog) << "YubiKeyService: Failed to save credentials to cache";
        } else {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials saved to cache successfully";
            const QMutexLocker locker(&m_lastCredentialSaveMutex);
            m_lastCredentialSave[deviceId] = QDateTime::currentMSecsSinceEpoch();
        }
    } else if (!m_config->enableCredentialsCache()) {
        qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Credentials cache disabled, NOT saving to database";
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: Authentication successful for device:" << deviceId;
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: >>> EMITTING credentialsUpdated and deviceConnectedAndAuthenticated signals";

    // Emit both signals: credentialsUpdated (for backward compat) and deviceConnectedAndAuthenticated (new)
    Q_EMIT credentialsUpdated(deviceId);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: this=" << this << "thread=" << QThread::currentThreadId()
                              << "about to emit deviceConnectedAndAuthenticated for device:" << deviceId;
    Q_EMIT deviceConnectedAndAuthenticated(deviceId);
    qCDebug(YubiKeyDaemonLog) << "YubiKeyService: deviceConnectedAndAuthenticated signal emitted successfully";
}

bool YubiKeyService::shouldSaveCredentialsToCache(const QString &deviceId)
{
    // Rate limiting check - prevent excessive database writes
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int rateLimitMs = m_config->credentialSaveRateLimit();

    // Thread-safe access to m_lastCredentialSave
    const QMutexLocker locker(&m_lastCredentialSaveMutex);
    if (m_lastCredentialSave.contains(deviceId)) {
        const qint64 timeSinceLastSave = now - m_lastCredentialSave[deviceId];
        if (timeSinceLastSave < rateLimitMs) {
            qCDebug(YubiKeyDaemonLog)
                << "YubiKeyService: Rate limited credential save for" << deviceId
                << "- last save" << timeSinceLastSave << "ms ago"
                << "(limit:" << rateLimitMs << "ms)";
            return false;
        }
    }
    return true;
}

} // namespace Daemon
} // namespace YubiKeyOath
