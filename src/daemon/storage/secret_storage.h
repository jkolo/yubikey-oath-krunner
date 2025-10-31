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
 * @brief Manages secure secret storage using KWallet
 *
 * Single Responsibility: Handle secret persistence in KWallet (passwords, tokens)
 */
class SecretStorage : public QObject
{
    Q_OBJECT

public:
    explicit SecretStorage(QObject *parent = nullptr);
    ~SecretStorage() override;

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

    /**
     * @brief Loads portal restore token from KWallet
     * @return Token or empty string if not found
     */
    QString loadRestoreToken();

    /**
     * @brief Saves portal restore token to KWallet
     * @param token Token to save
     * @return true if successful
     */
    bool saveRestoreToken(const QString &token);

    /**
     * @brief Removes portal restore token from KWallet
     * @return true if successful
     */
    bool removeRestoreToken();

private:
    KWallet::Wallet *m_wallet;

    // Helper methods for constants (to avoid QStringLiteral macro issues)
    static QString walletFolder() { return QStringLiteral("YubiKey OATH Application"); }
    static QString passwordKey(const QString &deviceId) {
        return QStringLiteral("yubikey_") + deviceId;
    }

    // Portal restore token key (used by portal_text_input for session persistence)
    static constexpr const char* PORTAL_TOKEN_KEY = "portal_restore_token";

    bool ensureWalletOpen();
};

} // namespace Daemon
} // namespace YubiKeyOath
