/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "password_service.h"
#include "../oath/yubikey_device_manager.h"
#include "../oath/oath_device.h"
#include "../storage/yubikey_database.h"
#include "../storage/secret_storage.h"
#include "../logging_categories.h"

namespace YubiKeyOath {
namespace Daemon {

PasswordService::PasswordService(YubiKeyDeviceManager *deviceManager,
                                YubiKeyDatabase *database,
                                SecretStorage *secretStorage,
                                QObject *parent)
    : QObject(parent)
    , m_deviceManager(deviceManager)
    , m_database(database)
    , m_secretStorage(secretStorage)
{
    Q_ASSERT(m_deviceManager);
    Q_ASSERT(m_database);
    Q_ASSERT(m_secretStorage);

    qCDebug(YubiKeyDaemonLog) << "PasswordService: Initialized";
}

bool PasswordService::savePassword(const QString &deviceId, const QString &password)
{
    qCDebug(YubiKeyDaemonLog) << "PasswordService: savePassword for device:" << deviceId;

    // First test the password by attempting authentication
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "PasswordService: Device not found:" << deviceId;
        return false;
    }

    auto authResult = device->authenticateWithPassword(password);
    if (authResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "PasswordService: Password is invalid:" << authResult.error();

        // FALLBACK: Maybe device doesn't require password at all?
        // Try fetching credentials without password
        qCDebug(YubiKeyDaemonLog) << "PasswordService: Testing if device requires password...";
        device->setPassword(QString());  // Clear password temporarily
        const QList<OathCredential> testCreds = device->fetchCredentialsSync(QString());
        if (!testCreds.isEmpty()) {
            qCDebug(YubiKeyDaemonLog) << "PasswordService: Device doesn't require password!";
            m_database->setRequiresPassword(deviceId, false);
            device->updateCredentialCacheAsync(QString());
            return true;  // Success - device doesn't need password
        }

        return false;  // Password really is invalid
    }

    // Save password in device for future use
    device->setPassword(password);

    // Save to KWallet
    if (!m_secretStorage->savePassword(password, deviceId)) {
        qCWarning(YubiKeyDaemonLog) << "PasswordService: Failed to save password to KWallet";
        return false;
    }

    // Update database flag
    m_database->setRequiresPassword(deviceId, true);

    // Trigger credential cache refresh with the new password
    qCDebug(YubiKeyDaemonLog) << "PasswordService: Password saved, triggering credential cache refresh";
    device->updateCredentialCacheAsync(password);

    qCDebug(YubiKeyDaemonLog) << "PasswordService: Password saved successfully";
    return true;
}

bool PasswordService::changePassword(const QString &deviceId,
                                    const QString &oldPassword,
                                    const QString &newPassword)
{
    qCDebug(YubiKeyDaemonLog) << "PasswordService: changePassword for device:" << deviceId;

    // Get device
    auto *device = m_deviceManager->getDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "PasswordService: Device not found:" << deviceId;
        return false;
    }

    // Change password via OathSession (handles auth + SET_CODE)
    auto changeResult = device->changePassword(oldPassword, newPassword);
    if (changeResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "PasswordService: Failed to change password:" << changeResult.error();
        return false;
    }

    qCDebug(YubiKeyDaemonLog) << "PasswordService: Password changed successfully on YubiKey";

    // Update password storage in KWallet
    if (newPassword.isEmpty()) {
        // Password was removed
        qCDebug(YubiKeyDaemonLog) << "PasswordService: Removing password from KWallet";
        m_secretStorage->removePassword(deviceId);

        // Update database - no longer requires password
        m_database->setRequiresPassword(deviceId, false);

        // Clear password from device
        device->setPassword(QString());

        qCInfo(YubiKeyDaemonLog) << "PasswordService: Password removed from device" << deviceId;
    } else {
        // Password was changed
        qCDebug(YubiKeyDaemonLog) << "PasswordService: Saving new password to KWallet";
        if (!m_secretStorage->savePassword(newPassword, deviceId)) {
            qCWarning(YubiKeyDaemonLog) << "PasswordService: Failed to save new password to KWallet";
            // Password changed on YubiKey but not in KWallet - this is a problem
            return false;
        }

        // Update database - still requires password
        m_database->setRequiresPassword(deviceId, true);

        // Update password in device for future operations
        device->setPassword(newPassword);

        qCInfo(YubiKeyDaemonLog) << "PasswordService: Password changed on device" << deviceId;
    }

    // Trigger credential cache refresh with new password (or empty if removed)
    qCDebug(YubiKeyDaemonLog) << "PasswordService: Triggering credential cache refresh";
    device->updateCredentialCacheAsync(newPassword);

    qCDebug(YubiKeyDaemonLog) << "PasswordService: changePassword completed successfully";
    return true;
}

} // namespace Daemon
} // namespace YubiKeyOath
