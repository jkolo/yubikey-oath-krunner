/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "password_storage.h"
#include "../logging_categories.h"

#include <kwallet.h>
#include <QDebug>
#include <QDateTime>

namespace KRunner {
namespace YubiKey {

PasswordStorage::PasswordStorage(QObject *parent)
    : QObject(parent)
    , m_wallet(nullptr)
{
    qCDebug(PasswordStorageLog) << "Initialized";
}

PasswordStorage::~PasswordStorage()
{
    if (m_wallet) {
        delete m_wallet;
        m_wallet = nullptr;
    }
}

QString PasswordStorage::loadPasswordSync(const QString &deviceId)
{
    qCDebug(PasswordStorageLog) << "Loading password synchronously from KWallet for device:" << deviceId;

    if (deviceId.isEmpty()) {
        qCWarning(PasswordStorageLog) << "Device ID is empty";
        return QString();
    }

    using namespace KWallet;

    // Open wallet synchronously
    Wallet *wallet = Wallet::openWallet(Wallet::LocalWallet(), 0, Wallet::Synchronous);

    if (!wallet || !wallet->isOpen()) {
        qCWarning(PasswordStorageLog) << "Could not open KWallet synchronously";
        if (wallet) {
            delete wallet;
        }
        return QString();
    }

    // Create folder if it doesn't exist
    if (!wallet->hasFolder(walletFolder())) {
        wallet->createFolder(walletFolder());
    }

    // Switch to our folder
    if (!wallet->setFolder(walletFolder())) {
        qCWarning(PasswordStorageLog) << "Failed to set KWallet folder";
        delete wallet;
        return QString();
    }

    // Read password for this device
    QString password;
    const QString key = passwordKey(deviceId);
    if (wallet->readPassword(key, password) == 0) {
        qCDebug(PasswordStorageLog) << "Password loaded synchronously from KWallet for key:" << key << ", empty:" << password.isEmpty();
    } else {
        qCDebug(PasswordStorageLog) << "No password found in KWallet for key:" << key;
        password = QString();
    }

    delete wallet;
    return password;
}

bool PasswordStorage::savePassword(const QString &password, const QString &deviceId)
{
    qCDebug(PasswordStorageLog) << "Saving password to KWallet for device:" << deviceId;

    if (deviceId.isEmpty()) {
        qCWarning(PasswordStorageLog) << "Device ID is empty, cannot save password";
        return false;
    }

    if (!m_wallet) {
        using namespace KWallet;
        m_wallet = Wallet::openWallet(Wallet::LocalWallet(), 0, Wallet::Synchronous);
    }

    if (!m_wallet || !m_wallet->isOpen()) {
        qCWarning(PasswordStorageLog) << "Could not open wallet for saving";
        return false;
    }

    // Create folder if it doesn't exist
    if (!m_wallet->hasFolder(walletFolder())) {
        m_wallet->createFolder(walletFolder());
    }

    // Switch to our folder
    if (!m_wallet->setFolder(walletFolder())) {
        qCWarning(PasswordStorageLog) << "Failed to set wallet folder";
        return false;
    }

    // Write password with device-specific key
    const QString key = passwordKey(deviceId);
    if (m_wallet->writePassword(key, password) == 0) {
        qCDebug(PasswordStorageLog) << "Password saved to KWallet with key:" << key;
        return true;
    }

    qCWarning(PasswordStorageLog) << "Failed to save password to KWallet";
    return false;
}


bool PasswordStorage::ensureWalletOpen()
{
    if (m_wallet && m_wallet->isOpen()) {
        return true;
    }

    if (!m_wallet) {
        using namespace KWallet;
        m_wallet = Wallet::openWallet(Wallet::LocalWallet(), 0, Wallet::Synchronous);
    }

    if (!m_wallet || !m_wallet->isOpen()) {
        qCWarning(PasswordStorageLog) << "Could not open wallet";
        return false;
    }

    // Create folder if it doesn't exist
    if (!m_wallet->hasFolder(walletFolder())) {
        m_wallet->createFolder(walletFolder());
    }

    // Switch to our folder
    if (!m_wallet->setFolder(walletFolder())) {
        qCWarning(PasswordStorageLog) << "Failed to set wallet folder";
        return false;
    }

    return true;
}

bool PasswordStorage::removePassword(const QString &deviceId)
{
    qCDebug(PasswordStorageLog) << "Removing password for device:" << deviceId;

    if (!ensureWalletOpen()) {
        return false;
    }

    QString key = passwordKey(deviceId);
    int result = m_wallet->removeEntry(key);

    if (result == 0) {
        qCDebug(PasswordStorageLog) << "Password removed successfully for:" << deviceId;
        return true;
    } else {
        qCWarning(PasswordStorageLog) << "Failed to remove password for:" << deviceId;
        return false;
    }
}

} // namespace YubiKey
} // namespace KRunner
