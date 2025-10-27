/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>

// Forward declarations for KDE classes (must be outside namespace)
namespace KWallet
{
class Wallet;
}

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Manages secure password storage using KWallet
 *
 * Single Responsibility: Handle password persistence in KWallet
 */
class PasswordStorage : public QObject
{
    Q_OBJECT

public:
    explicit PasswordStorage(QObject *parent = nullptr);
    ~PasswordStorage() override;

    /**
     * @brief Loads password from KWallet synchronously
     * @param deviceId Unique device identifier (YubiKey device ID)
     * @return Password or empty string if not found
     */
    QString loadPasswordSync(const QString &deviceId);

    /**
     * @brief Saves password to KWallet
     * @param password Password to save
     * @param deviceId Unique device identifier (YubiKey device ID)
     * @return true if successful
     */
    bool savePassword(const QString &password, const QString &deviceId);

    /**
     * @brief Removes password for device from KWallet
     * @param deviceId Device ID to remove password for
     * @return true if successful
     */
    bool removePassword(const QString &deviceId);

private:
    KWallet::Wallet *m_wallet;

    // Helper methods for constants (to avoid QStringLiteral macro issues)
    static QString walletFolder() { return QStringLiteral("YubiKey OATH Application"); }
    static QString passwordKey(const QString &deviceId) {
        return QStringLiteral("yubikey_") + deviceId;
    }

    bool ensureWalletOpen();
};

} // namespace Daemon
} // namespace YubiKeyOath
