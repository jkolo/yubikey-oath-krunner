/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class OathDeviceManager;
class OathDatabase;
class SecretStorage;

/**
 * @brief Service responsible for YubiKey password management operations
 *
 * Handles password validation, storage, and modification for YubiKey devices.
 * Coordinates between device authentication, KWallet storage, and database state.
 *
 * Extracted from OathService to follow Single Responsibility Principle.
 */
class PasswordService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct PasswordService
     * @param deviceManager Device manager for accessing YubiKey devices
     * @param database Database for storing device metadata
     * @param secretStorage KWallet storage for secure password persistence
     * @param parent Parent QObject
     */
    explicit PasswordService(OathDeviceManager *deviceManager,
                            OathDatabase *database,
                            SecretStorage *secretStorage,
                            QObject *parent = nullptr);

    ~PasswordService() override = default;

    /**
     * @brief Save password for device
     *
     * Validates password by attempting authentication, then saves to KWallet and database.
     * Also handles devices that don't require passwords.
     *
     * @param deviceId Device ID (hex string)
     * @param password Password to save
     * @return true if password saved successfully or device doesn't require password
     */
    bool savePassword(const QString &deviceId, const QString &password);

    /**
     * @brief Change device password
     *
     * Changes password on YubiKey hardware, updates KWallet and database.
     * Handles password removal (empty new password).
     *
     * @param deviceId Device ID (hex string)
     * @param oldPassword Current password
     * @param newPassword New password (empty to remove password)
     * @return true if password changed successfully
     */
    bool changePassword(const QString &deviceId,
                       const QString &oldPassword,
                       const QString &newPassword);

private:
    OathDeviceManager *m_deviceManager;  // Not owned
    OathDatabase *m_database;            // Not owned
    SecretStorage *m_secretStorage;         // Not owned
};

} // namespace Daemon
} // namespace YubiKeyOath
