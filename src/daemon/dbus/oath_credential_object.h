/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QDBusConnection>
#include <QVariant>
#include "types/oath_credential.h"
#include "types/yubikey_value_types.h"

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class OathService;

/**
 * @brief Credential D-Bus object for individual OATH credential
 *
 * D-Bus path: /pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>
 * Interfaces: pl.jkolo.yubikey.oath.Credential, Properties, Introspectable
 *
 * Represents a single OATH credential (TOTP/HOTP) on a YubiKey.
 * Provides methods to generate codes, copy to clipboard, type code.
 *
 * @par Lifetime
 * Created when credential is discovered on YubiKey, destroyed when removed.
 * Owned by YubiKeyDeviceObject.
 */
class OathCredentialObject : public QObject
{
    Q_OBJECT
    // Note: D-Bus interface is handled by CredentialAdaptor (auto-generated from XML)

    // Properties exposed via D-Bus Properties interface (all const - credentials don't change)
    Q_PROPERTY(QString FullName READ fullName CONSTANT)
    Q_PROPERTY(QString Issuer READ issuer CONSTANT)
    Q_PROPERTY(QString Username READ username CONSTANT)
    Q_PROPERTY(bool RequiresTouch READ requiresTouch CONSTANT)
    Q_PROPERTY(QString Type READ type CONSTANT)
    Q_PROPERTY(QString Algorithm READ algorithm CONSTANT)
    Q_PROPERTY(int Digits READ digits CONSTANT)
    Q_PROPERTY(int Period READ period CONSTANT)
    Q_PROPERTY(QString DeviceId READ deviceId CONSTANT)

public:
    /**
     * @brief Constructs Credential object
     * @param credential OathCredential data
     * @param deviceId Parent device ID
     * @param service Pointer to OathService
     * @param connection D-Bus connection
     * @param parent Parent QObject
     */
    explicit OathCredentialObject(Shared::OathCredential credential,
                                   QString deviceId,
                                   OathService *service,
                                   QDBusConnection connection,
                                   QObject *parent = nullptr);
    ~OathCredentialObject() override;

    /**
     * @brief Sets the D-Bus object path
     * @param path Full object path
     *
     * Must be called before registerObject()
     */
    void setObjectPath(const QString &path);

    /**
     * @brief Registers this object on D-Bus
     * @return true on success
     */
    bool registerObject();

    /**
     * @brief Unregisters this object from D-Bus
     */
    void unregisterObject();

    /**
     * @brief Gets D-Bus object path
     * @return Full object path
     */
    QString objectPath() const;

    // Property getters
    QString fullName() const;
    QString issuer() const;
    QString username() const;
    bool requiresTouch() const;
    QString type() const;
    QString algorithm() const;
    int digits() const;
    int period() const;
    QString deviceId() const;

public Q_SLOTS:
    // === ASYNC API (all methods fire-and-forget with signal results) ===

    /**
     * @brief Generates TOTP/HOTP code asynchronously
     *
     * Returns immediately. Result emitted via CodeGenerated signal.
     * Non-blocking - uses worker pool for PC/SC operations.
     */
    void GenerateCode();

    /**
     * @brief Copies code to clipboard asynchronously
     *
     * Returns immediately. Result emitted via ClipboardCopied signal.
     * Generates code and copies to clipboard with auto-clear support.
     */
    void CopyToClipboard();

    /**
     * @brief Types code via keyboard emulation asynchronously
     * @param fallbackToCopy If typing fails, copy to clipboard instead
     *
     * Returns immediately. Result emitted via CodeTyped signal.
     * Generates code and types it via input system.
     */
    void TypeCode(bool fallbackToCopy);

    /**
     * @brief Deletes credential from YubiKey asynchronously
     *
     * Returns immediately. Result emitted via Deleted signal.
     * Removes credential from device and triggers object removal.
     */
    void Delete();

Q_SIGNALS:
    // === Result Signals ===
    /**
     * @brief Emitted when async code generation completes
     * @param code Generated code (empty if error)
     * @param validUntil Unix timestamp when code expires (0 if error)
     * @param error Error message (empty if success)
     */
    void CodeGenerated(const QString &code, qint64 validUntil, const QString &error);

    /**
     * @brief Emitted when async clipboard copy completes
     * @param success true if copied successfully
     * @param error Error message (empty if success)
     */
    void ClipboardCopied(bool success, const QString &error);

    /**
     * @brief Emitted when async code typing completes
     * @param success true if typed successfully
     * @param error Error message (empty if success)
     */
    void CodeTyped(bool success, const QString &error);

    /**
     * @brief Emitted when async deletion completes
     * @param success true if deleted successfully
     * @param error Error message (empty if success)
     */
    void Deleted(bool success, const QString &error);

    // === Workflow Status Signals ===
    /**
     * @brief Emitted when user needs to touch the device
     * @param timeoutSeconds Number of seconds before timeout
     * @param deviceModel Device model string for icon/description
     */
    void TouchRequired(int timeoutSeconds, const QString &deviceModel);

    /**
     * @brief Emitted when touch workflow completes
     * @param success true if touch detected and operation continuing, false if cancelled/timeout
     */
    void TouchCompleted(bool success);

    /**
     * @brief Emitted when device needs to be reconnected
     * @param deviceModel Device model string for icon/description
     */
    void ReconnectRequired(const QString &deviceModel);

    /**
     * @brief Emitted when reconnect workflow completes
     * @param success true if device reconnected and operation continuing, false if cancelled
     */
    void ReconnectCompleted(bool success);

public:
    /**
     * @brief Gets ObjectManager data for this credential
     * @return Map of interface → properties
     *
     * Used by Device's getManagedCredentialObjects()
     */
    QVariantMap getManagedObjectData() const;

private:
    Shared::OathCredential m_credential;     ///< Credential data
    QString m_deviceId;                      ///< Parent device ID
    OathService *m_service;               ///< Business logic service (not owned)
    QDBusConnection m_connection;            ///< D-Bus connection
    QString m_objectPath;                    ///< Our object path
    bool m_registered;                       ///< Registration state
};

} // namespace Daemon
} // namespace YubiKeyOath
