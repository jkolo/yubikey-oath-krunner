/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "types/oath_credential.h"
#include "common/result.h"
#include <QObject>
#include <QString>
#include <QList>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of YubiKeyOathDevice for testing
 *
 * Simplified mock focused on workflow testing
 */
class MockYubiKeyOathDevice : public QObject
{
    Q_OBJECT

public:
    explicit MockYubiKeyOathDevice(const QString &deviceId, QObject *parent = nullptr)
        : QObject(parent)
        , m_deviceId(deviceId)
        , m_readerName(QStringLiteral("Mock Reader"))
        , m_updateInProgress(false)
    {}

    ~MockYubiKeyOathDevice() override = default;

    // ========== Device Information ==========

    QString deviceId() const { return m_deviceId; }
    QString readerName() const { return m_readerName; }
    QList<Shared::OathCredential> credentials() const { return m_credentials; }
    bool isUpdateInProgress() const { return m_updateInProgress; }

    // ========== OATH Operations ==========

    Shared::Result<QString> generateCode(const QString &name)
    {
        // Find credential
        for (const auto &cred : m_credentials) {
            if (cred.originalName == name) {
                // Check if should fail
                if (m_failingCredentials.contains(name)) {
                    return Shared::Result<QString>::error(QStringLiteral("Mock error: credential failed"));
                }

                // Generate mock code based on touch requirement
                if (cred.requiresTouch) {
                    // Touch-required credentials return error until touch simulated
                    if (!m_touchedCredentials.contains(name)) {
                        return Shared::Result<QString>::error(QStringLiteral("Touch required"));
                    }
                }

                return Shared::Result<QString>::success(m_mockCode);
            }
        }

        return Shared::Result<QString>::error(QStringLiteral("Credential not found"));
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Sets mock credentials
     */
    void setCredentials(const QList<Shared::OathCredential> &credentials)
    {
        m_credentials = credentials;
    }

    /**
     * @brief Sets mock code to return
     */
    void setMockCode(const QString &code)
    {
        m_mockCode = code;
    }

    /**
     * @brief Marks credential as touched (allows code generation)
     */
    void simulateTouch(const QString &credentialName)
    {
        m_touchedCredentials.insert(credentialName);
    }

    /**
     * @brief Clears touch for credential
     */
    void clearTouch(const QString &credentialName)
    {
        m_touchedCredentials.remove(credentialName);
    }

    /**
     * @brief Marks credential as failing
     */
    void setCredentialFailing(const QString &credentialName, bool failing)
    {
        if (failing) {
            m_failingCredentials.insert(credentialName);
        } else {
            m_failingCredentials.remove(credentialName);
        }
    }

    /**
     * @brief Creates test credential
     */
    static Shared::OathCredential createTestCredential(
        const QString &name,
        const QString &issuer,
        const QString &account,
        bool requiresTouch = false
    )
    {
        Shared::OathCredential cred;
        cred.originalName = name;
        cred.issuer = issuer;
        cred.account = account;
        cred.requiresTouch = requiresTouch;
        cred.isTotp = true;
        cred.type = 2; // TOTP
        cred.algorithm = 1; // SHA1
        cred.digits = 6;
        cred.period = 30;
        return cred;
    }

private:
    QString m_deviceId;
    QString m_readerName;
    bool m_updateInProgress;
    QList<Shared::OathCredential> m_credentials;
    QString m_mockCode{QStringLiteral("123456")};
    QSet<QString> m_touchedCredentials;
    QSet<QString> m_failingCredentials;
};

} // namespace Daemon
} // namespace YubiKeyOath
