/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "credential_cache_searcher.h"
#include "../oath/yubikey_device_manager.h"
#include "../storage/yubikey_database.h"
#include "../config/daemon_configuration.h"
#include "../logging_categories.h"

namespace YubiKeyOath {
namespace Daemon {

CredentialCacheSearcher::CredentialCacheSearcher(YubiKeyDeviceManager *deviceManager,
                                                 YubiKeyDatabase *database,
                                                 DaemonConfiguration *config)
    : m_deviceManager(deviceManager)
    , m_database(database)
    , m_config(config)
{
}

std::optional<QString> CredentialCacheSearcher::findCachedCredentialDevice(
    const QString &credentialName,
    const QString &deviceIdHint)
{
    if (!m_config->enableCredentialsCache()) {
        return std::nullopt;
    }

    qCDebug(YubiKeyDaemonLog) << "CredentialCacheSearcher: Searching for cached credential"
                              << credentialName;

    // If deviceId hint provided, check that device first
    if (!deviceIdHint.isEmpty()) {
        // Skip if device is currently connected
        if (!m_deviceManager->getDevice(deviceIdHint)) {
            auto cachedCreds = m_database->getCredentials(deviceIdHint);
            qCDebug(YubiKeyDaemonLog) << "CredentialCacheSearcher: Found" << cachedCreds.size()
                                      << "cached credentials for device:" << deviceIdHint;

            for (const auto &cred : cachedCreds) {
                if (cred.originalName == credentialName) {
                    qCDebug(YubiKeyDaemonLog) << "CredentialCacheSearcher: Found in hinted device";
                    return deviceIdHint;
                }
            }
        }

        // Credential not found in hinted device
        return std::nullopt;
    }

    // No hint - search all offline devices
    auto allDevices = m_database->getAllDevices();
    for (const auto &deviceRecord : allDevices) {
        // Skip connected devices
        if (m_deviceManager->getDevice(deviceRecord.deviceId)) {
            continue;
        }

        auto cachedCreds = m_database->getCredentials(deviceRecord.deviceId);
        for (const auto &cred : cachedCreds) {
            if (cred.originalName == credentialName) {
                qCDebug(YubiKeyDaemonLog)
                    << "CredentialCacheSearcher: Found cached credential in offline device:"
                    << deviceRecord.deviceId;
                return deviceRecord.deviceId;
            }
        }
    }

    // Not found
    qCDebug(YubiKeyDaemonLog) << "CredentialCacheSearcher: Credential not found in cache";
    return std::nullopt;
}

} // namespace Daemon
} // namespace YubiKeyOath
