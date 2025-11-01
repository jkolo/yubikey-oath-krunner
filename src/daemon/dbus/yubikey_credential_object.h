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
class YubiKeyService;

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
class YubiKeyCredentialObject : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "pl.jkolo.yubikey.oath.Credential")

    // Properties exposed via D-Bus Properties interface (all const - credentials don't change)
    Q_PROPERTY(QString Name READ name CONSTANT)
    Q_PROPERTY(QString Issuer READ issuer CONSTANT)
    Q_PROPERTY(QString Account READ account CONSTANT)
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
     * @param service Pointer to YubiKeyService
     * @param connection D-Bus connection
     * @param parent Parent QObject
     */
    explicit YubiKeyCredentialObject(const Shared::OathCredential &credential,
                                     const QString &deviceId,
                                     YubiKeyService *service,
                                     const QDBusConnection &connection,
                                     QObject *parent = nullptr);
    ~YubiKeyCredentialObject() override;

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
    QString name() const;
    QString issuer() const;
    QString account() const;
    bool requiresTouch() const;
    QString type() const;
    QString algorithm() const;
    int digits() const;
    int period() const;
    QString deviceId() const;

public Q_SLOTS:
    /**
     * @brief Generates TOTP/HOTP code
     * @return (code, validUntil)
     *
     * Handles touch requirement automatically - shows notification if needed.
     */
    Shared::GenerateCodeResult GenerateCode();

    /**
     * @brief Copies code to clipboard
     * @return true on success
     *
     * Generates code and copies to clipboard with auto-clear support.
     */
    bool CopyToClipboard();

    /**
     * @brief Types code via keyboard emulation
     * @param fallbackToCopy If typing fails, copy to clipboard instead
     * @return true on success
     */
    bool TypeCode(bool fallbackToCopy);

    /**
     * @brief Deletes credential from YubiKey
     *
     * Removes credential from device and triggers object removal.
     */
    void Delete();

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
    YubiKeyService *m_service;               ///< Business logic service (not owned)
    QDBusConnection m_connection;            ///< D-Bus connection
    QString m_objectPath;                    ///< Our object path
    bool m_registered;                       ///< Registration state
};

} // namespace Daemon
} // namespace YubiKeyOath
