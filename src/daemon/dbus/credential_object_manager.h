/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QDBusConnection>
#include "types/oath_credential.h"

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class OathService;
class OathCredentialObject;

/**
 * @brief Manages lifecycle of OathCredentialObject instances for a device
 *
 * This class is responsible for:
 * - Creating and registering OathCredentialObject instances on D-Bus
 * - Tracking credential objects in memory
 * - Synchronizing credential list with service state
 * - Emitting signals when credentials are added/removed
 *
 * @par Single Responsibility
 * Handles ONLY D-Bus object lifecycle, not credential business logic.
 * Business logic remains in OathService/CredentialService.
 *
 * @par Usage
 * Created by OathDeviceObject to manage its credential sub-objects.
 * OathDeviceObject forwards the signals for D-Bus hierarchy.
 */
class CredentialObjectManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs credential object manager
     * @param deviceId Device ID (for credential association)
     * @param devicePath D-Bus path of parent device (e.g., /pl/jkolo/yubikey/oath/devices/12345)
     * @param service Pointer to OathService
     * @param connection D-Bus connection
     * @param parent Parent QObject (typically OathDeviceObject)
     */
    explicit CredentialObjectManager(QString deviceId,
                                       QString devicePath,
                                       OathService *service,
                                       QDBusConnection connection,
                                       QObject *parent = nullptr);
    ~CredentialObjectManager() override;

    /**
     * @brief Creates and registers a credential D-Bus object
     * @param credential OathCredential data
     * @return Pointer to created CredentialObject (owned by manager) or nullptr on failure
     */
    OathCredentialObject* addCredential(const Shared::OathCredential &credential);

    /**
     * @brief Removes and unregisters a credential D-Bus object
     * @param credentialId Credential ID (encoded name)
     */
    void removeCredential(const QString &credentialId);

    /**
     * @brief Gets credential object by ID
     * @param credentialId Credential ID (encoded name)
     * @return Pointer to CredentialObject or nullptr if not found
     */
    [[nodiscard]] OathCredentialObject* getCredential(const QString &credentialId) const;

    /**
     * @brief Gets all credential object paths
     * @return List of D-Bus paths for all managed credentials
     */
    [[nodiscard]] QStringList credentialPaths() const;

    /**
     * @brief Synchronizes credential objects with service state
     *
     * Fetches current credentials from service, creates new objects
     * for new credentials, removes objects for deleted credentials.
     */
    void updateCredentials();

    /**
     * @brief Removes and unregisters all credential objects
     *
     * Called during device cleanup/unregistration.
     */
    void removeAllCredentials();

    /**
     * @brief Gets ObjectManager data for all credentials
     * @return Map of path → (interface → properties)
     */
    [[nodiscard]] QVariantMap getManagedObjects() const;

Q_SIGNALS:
    /**
     * @brief Emitted when a credential object is added
     * @param credentialPath D-Bus object path of added credential
     */
    void credentialAdded(const QString &credentialPath);

    /**
     * @brief Emitted when a credential object is removed
     * @param credentialPath D-Bus object path of removed credential
     */
    void credentialRemoved(const QString &credentialPath);

private:
    /**
     * @brief Builds credential D-Bus object path
     * @param credentialId Encoded credential ID
     * @return Full D-Bus object path (devicePath/credentials/credentialId)
     */
    [[nodiscard]] QString credentialPath(const QString &credentialId) const;

    QString m_deviceId;
    QString m_devicePath;
    OathService *m_service;
    QDBusConnection m_connection;
    QMap<QString, OathCredentialObject*> m_credentials;  ///< Credential ID → CredentialObject
};

} // namespace Daemon
} // namespace YubiKeyOath
