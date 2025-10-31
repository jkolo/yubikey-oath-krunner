/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "secret_storage.h"
#include "../logging_categories.h"

#include <kwallet.h>
#include <QDebug>
#include <QDateTime>

namespace YubiKeyOath {
namespace Daemon {

SecretStorage::SecretStorage(QObject *parent)
    : QObject(parent)
    , m_wallet(nullptr)
{
    qCDebug(SecretStorageLog) << "Initialized";
}

SecretStorage::~SecretStorage()
{
    if (m_wallet) {
        delete m_wallet;
        m_wallet = nullptr;
    }
}

QString SecretStorage::loadPasswordSync(const QString &deviceId)
{
    qCDebug(SecretStorageLog) << "Loading password synchronously from KWallet for device:" << deviceId;

    if (deviceId.isEmpty()) {
        qCWarning(SecretStorageLog) << "Device ID is empty";
        return {};
    }

    // Ensure wallet is open and folder is set
    if (!ensureWalletOpen()) {
        qCWarning(SecretStorageLog) << "Could not open KWallet";
        return {};
    }

    // Read password for this device
    QString password;
    const QString key = passwordKey(deviceId);
    if (m_wallet->readPassword(key, password) == 0) {
        qCDebug(SecretStorageLog) << "Password loaded from KWallet for key:" << key << ", empty:" << password.isEmpty();
    } else {
        qCDebug(SecretStorageLog) << "No password found in KWallet for key:" << key;
        password = QString();
    }

    return password;
}

bool SecretStorage::savePassword(const QString &password, const QString &deviceId)
{
    qCDebug(SecretStorageLog) << "Saving password to KWallet for device:" << deviceId;

    if (deviceId.isEmpty()) {
        qCWarning(SecretStorageLog) << "Device ID is empty, cannot save password";
        return false;
    }

    // Ensure wallet is open and folder is set
    if (!ensureWalletOpen()) {
        qCWarning(SecretStorageLog) << "Could not open wallet for saving";
        return false;
    }

    // Write password with device-specific key
    const QString key = passwordKey(deviceId);
    if (m_wallet->writePassword(key, password) == 0) {
        qCDebug(SecretStorageLog) << "Password saved to KWallet with key:" << key;
        return true;
    }

    qCWarning(SecretStorageLog) << "Failed to save password to KWallet";
    return false;
}


bool SecretStorage::ensureWalletOpen()
{
    if (m_wallet && m_wallet->isOpen()) {
        return true;
    }

    if (!m_wallet) {
        using namespace KWallet;
        m_wallet = Wallet::openWallet(Wallet::LocalWallet(), 0, Wallet::Synchronous);
    }

    if (!m_wallet || !m_wallet->isOpen()) {
        qCWarning(SecretStorageLog) << "Could not open wallet";
        return false;
    }

    // Create folder if it doesn't exist
    if (!m_wallet->hasFolder(walletFolder())) {
        m_wallet->createFolder(walletFolder());
    }

    // Switch to our folder
    if (!m_wallet->setFolder(walletFolder())) {
        qCWarning(SecretStorageLog) << "Failed to set wallet folder";
        return false;
    }

    return true;
}

bool SecretStorage::removePassword(const QString &deviceId)
{
    qCDebug(SecretStorageLog) << "Removing password for device:" << deviceId;

    if (!ensureWalletOpen()) {
        return false;
    }

    QString const key = passwordKey(deviceId);
    int const result = m_wallet->removeEntry(key);

    if (result == 0) {
        qCDebug(SecretStorageLog) << "Password removed successfully for:" << deviceId;
        return true;
    } else {
        qCWarning(SecretStorageLog) << "Failed to remove password for:" << deviceId;
        return false;
    }
}

QString SecretStorage::loadRestoreToken()
{
    qCDebug(SecretStorageLog) << "Loading portal restore token from KWallet";
    return loadPasswordSync(QString::fromLatin1(PORTAL_TOKEN_KEY));
}

bool SecretStorage::saveRestoreToken(const QString &token)
{
    qCDebug(SecretStorageLog) << "Saving portal restore token to KWallet";
    return savePassword(token, QString::fromLatin1(PORTAL_TOKEN_KEY));
}

bool SecretStorage::removeRestoreToken()
{
    qCDebug(SecretStorageLog) << "Removing portal restore token from KWallet";
    return removePassword(QString::fromLatin1(PORTAL_TOKEN_KEY));
}

} // namespace Daemon
} // namespace YubiKeyOath
