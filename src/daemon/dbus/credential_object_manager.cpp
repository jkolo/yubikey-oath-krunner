/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "credential_object_manager.h"
#include "oath_credential_object.h"
#include "services/oath_service.h"
#include "utils/credential_id_encoder.h"
#include "logging_categories.h"

#include <utility>

namespace YubiKeyOath {
namespace Daemon {

CredentialObjectManager::CredentialObjectManager(QString deviceId,
                                                   QString devicePath,
                                                   OathService *service,
                                                   QDBusConnection connection,
                                                   QObject *parent)
    : QObject(parent)
    , m_deviceId(std::move(deviceId))
    , m_devicePath(std::move(devicePath))
    , m_service(service)
    , m_connection(std::move(connection))
{
    qCDebug(OathDaemonLog) << "CredentialObjectManager: Created for device:" << m_deviceId;
}

CredentialObjectManager::~CredentialObjectManager()
{
    qCDebug(OathDaemonLog) << "CredentialObjectManager: Destroying for device:" << m_deviceId;
    removeAllCredentials();
}

OathCredentialObject* CredentialObjectManager::addCredential(const Shared::OathCredential &credential)
{
    const QString credId = CredentialIdEncoder::encode(credential.originalName);

    qCDebug(OathDaemonLog) << "CredentialObjectManager: Adding credential:" << credential.originalName
                              << "id:" << credId << "for device:" << m_deviceId;

    // Check if already exists
    if (m_credentials.contains(credId)) {
        qCWarning(OathDaemonLog) << "CredentialObjectManager: Credential already exists:" << credId;
        return m_credentials.value(credId);
    }

    // Create credential object
    const QString path = credentialPath(credId);
    auto *credObj = new OathCredentialObject(credential, m_deviceId, m_service,
                                              m_connection, this);

    // Set object path before registration
    credObj->setObjectPath(path);

    if (!credObj->registerObject()) {
        qCCritical(OathDaemonLog) << "CredentialObjectManager: Failed to register credential object"
                                     << credId;
        delete credObj;
        return nullptr;
    }

    m_credentials.insert(credId, credObj);

    // Emit signal for parent to forward to D-Bus
    Q_EMIT credentialAdded(path);

    qCInfo(OathDaemonLog) << "CredentialObjectManager: Credential added:" << credential.originalName
                             << "at" << path;

    return credObj;
}

void CredentialObjectManager::removeCredential(const QString &credentialId)
{
    qCDebug(OathDaemonLog) << "CredentialObjectManager: Removing credential:" << credentialId
                              << "from device:" << m_deviceId;

    if (!m_credentials.contains(credentialId)) {
        qCWarning(OathDaemonLog) << "CredentialObjectManager: Credential not found:" << credentialId;
        return;
    }

    OathCredentialObject *const credObj = m_credentials.value(credentialId);
    const QString path = credObj->objectPath();

    // Unregister and delete
    credObj->unregisterObject();
    delete credObj;

    m_credentials.remove(credentialId);

    // Emit signal for parent to forward to D-Bus
    Q_EMIT credentialRemoved(path);

    qCInfo(OathDaemonLog) << "CredentialObjectManager: Credential removed:" << credentialId;
}

OathCredentialObject* CredentialObjectManager::getCredential(const QString &credentialId) const
{
    return m_credentials.value(credentialId, nullptr);
}

QStringList CredentialObjectManager::credentialPaths() const
{
    QStringList paths;
    for (auto it = m_credentials.constBegin(); it != m_credentials.constEnd(); ++it) {
        paths.append(it.value()->objectPath());
    }
    return paths;
}

void CredentialObjectManager::updateCredentials()
{
    qCDebug(OathDaemonLog) << "CredentialObjectManager: Updating credentials for device:"
                              << m_deviceId;

    // Get current credentials from service
    const QList<Shared::OathCredential> currentCreds = m_service->getCredentials(m_deviceId);

    // Build set of current credential IDs
    QSet<QString> currentCredIds;
    for (const auto &cred : currentCreds) {
        currentCredIds.insert(CredentialIdEncoder::encode(cred.originalName));
    }

    // Build set of existing credential IDs
    const QSet<QString> existingCredIds = QSet<QString>(m_credentials.keyBegin(),
                                                         m_credentials.keyEnd());

    // Remove credentials that no longer exist
    const QSet<QString> toRemove = existingCredIds - currentCredIds;
    for (const QString &credId : toRemove) {
        removeCredential(credId);
    }

    // Add new credentials
    const QSet<QString> toAdd = currentCredIds - existingCredIds;
    for (const auto &cred : currentCreds) {
        const QString credId = CredentialIdEncoder::encode(cred.originalName);
        if (toAdd.contains(credId)) {
            addCredential(cred);
        }
    }

    qCDebug(OathDaemonLog) << "CredentialObjectManager: Credentials updated for device:"
                              << m_deviceId << "- total:" << m_credentials.size();
}

void CredentialObjectManager::removeAllCredentials()
{
    qCDebug(OathDaemonLog) << "CredentialObjectManager: Removing all credentials for device:"
                              << m_deviceId;

    const QStringList credIds = m_credentials.keys();
    for (const QString &credId : credIds) {
        removeCredential(credId);
    }
}

QVariantMap CredentialObjectManager::getManagedObjects() const
{
    QVariantMap result;

    for (auto it = m_credentials.constBegin(); it != m_credentials.constEnd(); ++it) {
        const QString path = it.value()->objectPath();
        const QVariantMap credData = it.value()->getManagedObjectData();
        result.insert(path, credData);
    }

    return result;
}

QString CredentialObjectManager::credentialPath(const QString &credentialId) const
{
    return QString::fromLatin1("%1/credentials/%2").arg(m_devicePath, credentialId);
}

} // namespace Daemon
} // namespace YubiKeyOath
