/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QList>

// Forward declarations
namespace YubiKeyOath {
namespace Shared {
    struct OathCredential;
}
namespace Daemon {
    class YubiKeyOathDevice;
}
}

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Interface for components that notify about credential updates
 *
 * Single Responsibility: Define contract for credential update notifications
 *
 * This interface breaks the circular dependency between YubiKeyService and
 * ReconnectWorkflowCoordinator by using dependency inversion principle.
 *
 * @par Dependency Inversion Principle (DIP)
 * Instead of:
 * - ReconnectWorkflowCoordinator → YubiKeyService (concrete dependency)
 *
 * We have:
 * - ReconnectWorkflowCoordinator → ICredentialUpdateNotifier (abstract)
 * - YubiKeyService implements ICredentialUpdateNotifier
 *
 * This allows ReconnectWorkflowCoordinator to work with any credential
 * update notifier without knowing the concrete implementation.
 */
class ICredentialUpdateNotifier : public QObject
{
    Q_OBJECT

public:
    explicit ICredentialUpdateNotifier(QObject *parent = nullptr)
        : QObject(parent)
    {}

    ~ICredentialUpdateNotifier() override = default;

    /**
     * @brief Gets all credentials from all connected devices
     * @return List of credentials from all devices
     *
     * Used by workflows that need to search across all devices.
     */
    virtual QList<OathCredential> getCredentials() = 0;

    /**
     * @brief Gets device instance by ID
     * @param deviceId Device ID to retrieve
     * @return Pointer to device or nullptr if not found
     *
     * Used by workflows that need direct device access for operations.
     */
    virtual Daemon::YubiKeyOathDevice* getDevice(const QString &deviceId) = 0;

    /**
     * @brief Gets IDs of all currently connected devices
     * @return List of connected device IDs
     *
     * Used for display formatting (show device name when multiple devices).
     */
    virtual QList<QString> getConnectedDeviceIds() const = 0;

Q_SIGNALS:
    /**
     * @brief Emitted when credentials are updated for a device
     * @param deviceId Device ID whose credentials were updated
     *
     * This signal should be emitted whenever:
     * - Device credentials are fetched
     * - Credentials are added/deleted
     * - Device is reconnected
     */
    void credentialsUpdated(const QString &deviceId);
};

} // namespace Shared
} // namespace YubiKeyOath
