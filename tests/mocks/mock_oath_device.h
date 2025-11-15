/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "daemon/oath/oath_device.h"
#include "types/oath_credential.h"
#include "common/result.h"
#include "shared/utils/version.h"
#include "shared/types/device_model.h"
#include <QObject>
#include <QString>
#include <QList>
#include <optional>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of OathDevice for testing
 *
 * Inherits from OathDevice to be compatible with services that expect OathDevice*.
 * Provides simplified mock implementations of all required virtual methods.
 */
class MockOathDevice : public OathDevice
{
    Q_OBJECT

public:
    explicit MockOathDevice(const QString &deviceId, QObject *parent = nullptr)
        : OathDevice(parent)
        , m_mockDeviceId(deviceId)
        , m_mockReaderName(QStringLiteral("Mock Reader"))
        , m_mockFirmwareVersion(5, 4, 2)
        , m_mockSerialNumber(0x12345678)
        , m_mockFormFactor(1)
    {
        // Initialize mock device model
        m_mockDeviceModel.brand = Shared::DeviceBrand::YubiKey;
        m_mockDeviceModel.modelCode = 0x05010803;  // YubiKey 5C NFC
        m_mockDeviceModel.modelString = QStringLiteral("YubiKey 5C NFC - Mock");
        m_mockDeviceModel.formFactor = 1;
    }

    ~MockOathDevice() override = default;

    // ========== OathDevice Pure Virtual Method Implementations ==========

protected:
    /**
     * @brief Factory method for creating temporary session during reconnect
     * @param handle PC/SC card handle
     * @param protocol PC/SC protocol
     * @return Mock session (nullptr for testing - not used in password service tests)
     */
    std::unique_ptr<YkOathSession> createTempSession(SCARDHANDLE handle, DWORD protocol) override
    {
        // For password service tests, this method is not called
        // Return nullptr as we don't need actual PC/SC sessions in tests
        return nullptr;
    }

public:

    QString deviceId() const override { return m_mockDeviceId; }
    QString readerName() const override { return m_mockReaderName; }
    Shared::Version firmwareVersion() const override { return m_mockFirmwareVersion; }
    Shared::DeviceModel deviceModel() const override { return m_mockDeviceModel; }
    quint32 serialNumber() const override { return m_mockSerialNumber; }
    bool requiresPassword() const override { return m_mockRequiresPassword; }
    quint8 formFactor() const override { return m_mockFormFactor; }
    QList<Shared::OathCredential> credentials() const override { return m_mockCredentials; }
    bool isUpdateInProgress() const override { return m_mockUpdateInProgress; }

    // ========== OATH Operations ==========

    Shared::Result<QString> generateCode(const QString &name) override
    {
        // If custom result is set, return it
        if (m_mockGenerateCodeResult.has_value()) {
            return m_mockGenerateCodeResult.value();
        }

        // Find credential
        for (const auto &cred : m_mockCredentials) {
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

                return Shared::Result<QString>::success(m_generatedCode);
            }
        }

        return Shared::Result<QString>::error(QStringLiteral("Credential not found"));
    }

    Shared::Result<void> addCredential(const Shared::OathCredentialData &data) override
    {
        // If custom result is set, return it
        if (m_mockAddCredentialResult.has_value()) {
            return m_mockAddCredentialResult.value();
        }

        // Default behavior: check for duplicates
        for (const auto &cred : m_mockCredentials) {
            if (cred.originalName == data.name) {
                return Shared::Result<void>::error(QStringLiteral("Credential already exists"));
            }
        }

        // Add credential
        Shared::OathCredential newCred;
        newCred.originalName = data.name;
        newCred.issuer = data.issuer;
        newCred.account = data.account;
        newCred.requiresTouch = data.requireTouch;
        newCred.isTotp = (data.type == Shared::OathType::TOTP);
        newCred.type = data.type == Shared::OathType::TOTP ? 2 : 1;
        newCred.algorithm = static_cast<int>(data.algorithm);
        newCred.digits = data.digits;
        newCred.period = data.period;
        m_mockCredentials.append(newCred);

        return Shared::Result<void>::success();
    }

    Shared::Result<void> deleteCredential(const QString &name) override
    {
        // If custom result is set, return it
        if (m_mockDeleteCredentialResult.has_value()) {
            return m_mockDeleteCredentialResult.value();
        }

        // Default behavior: find and remove credential
        for (int i = 0; i < m_mockCredentials.size(); ++i) {
            if (m_mockCredentials[i].originalName == name) {
                m_mockCredentials.removeAt(i);
                return Shared::Result<void>::success();
            }
        }

        return Shared::Result<void>::error(QStringLiteral("Credential not found"));
    }

    /**
     * @brief Authenticates with password
     * @param password Password to verify
     * @return Success if password matches, error otherwise
     */
    Shared::Result<void> authenticateWithPassword(const QString &password) override
    {
        if (!m_mockRequiresPassword) {
            return Shared::Result<void>::success();
        }

        if (password == m_correctPassword) {
            return Shared::Result<void>::success();
        }

        return Shared::Result<void>::error(QStringLiteral("Invalid password"));
    }

    /**
     * @brief Changes device password
     * @param oldPassword Current password
     * @param newPassword New password to set
     * @return Success if old password correct, error otherwise
     */
    Shared::Result<void> changePassword(const QString &oldPassword, const QString &newPassword) override
    {
        if (!m_mockRequiresPassword) {
            return Shared::Result<void>::error(QStringLiteral("Device doesn't require password"));
        }

        if (oldPassword != m_correctPassword) {
            return Shared::Result<void>::error(QStringLiteral("Wrong old password"));
        }

        m_correctPassword = newPassword;
        return Shared::Result<void>::success();
    }

    /**
     * @brief Sets password for device
     * @param password Password to store
     */
    void setPassword(const QString &password) override
    {
        m_currentPassword = password;
    }

    /**
     * @brief Fetches credentials synchronously
     * @param password Optional password for authentication
     * @return List of credentials (empty on auth failure)
     */
    QList<Shared::OathCredential> fetchCredentialsSync(const QString &password = QString()) override
    {
        if (m_mockRequiresPassword && !password.isEmpty() && password != m_correctPassword) {
            return QList<Shared::OathCredential>(); // Empty list on auth failure
        }
        return m_mockCredentials;
    }

    /**
     * @brief Updates credential cache asynchronously
     * @param password Optional password for authentication
     */
    void updateCredentialCacheAsync(const QString &password = QString()) override
    {
        // Mock implementation - just update credentials synchronously
        m_mockCredentials = fetchCredentialsSync(password);
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Sets mock credentials
     */
    void setCredentials(const QList<Shared::OathCredential> &credentials)
    {
        m_mockCredentials = credentials;
    }

    /**
     * @brief Sets mock code to return
     */
    void setMockCode(const QString &code)
    {
        m_generatedCode = code;
    }

    /**
     * @brief Sets custom result for generateCode()
     * @param result Result to return (overrides default behavior)
     */
    void setMockGenerateCodeResult(const Shared::Result<QString> &result)
    {
        m_mockGenerateCodeResult = result;
    }

    /**
     * @brief Sets custom result for addCredential()
     * @param result Result to return (overrides default behavior)
     */
    void setMockAddCredentialResult(const Shared::Result<void> &result)
    {
        m_mockAddCredentialResult = result;
    }

    /**
     * @brief Sets custom result for deleteCredential()
     * @param result Result to return (overrides default behavior)
     */
    void setMockDeleteCredentialResult(const Shared::Result<void> &result)
    {
        m_mockDeleteCredentialResult = result;
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
     * @brief Sets the correct password for authentication testing
     * @param password Password that will authenticate successfully
     */
    void setCorrectPassword(const QString &password)
    {
        m_correctPassword = password;
    }

    /**
     * @brief Configures whether device requires password
     * @param required True if password required
     */
    void setRequiresPassword(bool required)
    {
        m_mockRequiresPassword = required;
    }

    /**
     * @brief Gets currently stored password
     * @return Current password (for verification in tests)
     */
    QString currentPassword() const
    {
        return m_currentPassword;
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
    // Mock device information (to override base class pure virtuals)
    QString m_mockDeviceId;
    QString m_mockReaderName;
    Shared::Version m_mockFirmwareVersion;
    Shared::DeviceModel m_mockDeviceModel;
    quint32 m_mockSerialNumber;
    quint8 m_mockFormFactor;
    bool m_mockUpdateInProgress{false};
    bool m_mockRequiresPassword{false};

    // Mock credential data
    QList<Shared::OathCredential> m_mockCredentials;
    QString m_generatedCode{QStringLiteral("123456")};
    QSet<QString> m_touchedCredentials;
    QSet<QString> m_failingCredentials;

    // Password management
    QString m_correctPassword;      // Password that will authenticate successfully
    QString m_currentPassword;      // Currently set password (via setPassword)

    // Custom mock results (overrides default behavior when set)
    std::optional<Shared::Result<QString>> m_mockGenerateCodeResult;
    std::optional<Shared::Result<void>> m_mockAddCredentialResult;
    std::optional<Shared::Result<void>> m_mockDeleteCredentialResult;
};

} // namespace Daemon
} // namespace YubiKeyOath
