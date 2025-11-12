/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef OATH_CREDENTIAL_PROXY_H
#define OATH_CREDENTIAL_PROXY_H

#include <QObject>
#include <QString>
#include "types/yubikey_value_types.h"

// Forward declarations
class QDBusInterface;

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Proxy for a single OATH credential on a YubiKey
 *
 * This class represents a D-Bus object at path:
 * /pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>
 *
 * Interface: pl.jkolo.yubikey.oath.Credential
 *
 * Single Responsibility: Proxy for credential D-Bus object
 * - Caches all credential properties (read-only, const)
 * - Provides methods for operations: GenerateCode, CopyToClipboard, TypeCode, Delete
 * - Converts to CredentialInfo value type
 *
 * Architecture:
 * ```
 * OathManagerProxy (singleton)
 *     ↓ owns
 * OathDeviceProxy (per device)
 *     ↓ owns
 * OathCredentialProxy (per credential) ← YOU ARE HERE
 * ```
 */
class OathCredentialProxy : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs credential proxy from D-Bus object path and properties
     * @param objectPath D-Bus object path (e.g. /pl/jkolo/yubikey/oath/devices/<id>/credentials/<id>)
     * @param properties Property map from GetManagedObjects() for pl.jkolo.yubikey.oath.Credential interface
     * @param parent Parent object (typically OathDeviceProxy)
     *
     * Properties are cached on construction (all credential properties are const).
     * Creates QDBusInterface for method calls.
     */
    explicit OathCredentialProxy(const QString &objectPath,
                                  const QVariantMap &properties,
                                  QObject *parent = nullptr);

    ~OathCredentialProxy() override;

    // ========== Cached Properties (read-only) ==========

    [[nodiscard]] QString objectPath() const { return m_objectPath; }
    [[nodiscard]] QString fullName() const { return m_fullName; }
    [[nodiscard]] QString issuer() const { return m_issuer; }
    [[nodiscard]] QString username() const { return m_username; }
    [[nodiscard]] bool requiresTouch() const { return m_requiresTouch; }
    [[nodiscard]] QString type() const { return m_type; }
    [[nodiscard]] QString algorithm() const { return m_algorithm; }
    [[nodiscard]] int digits() const { return m_digits; }
    [[nodiscard]] int period() const { return m_period; }
    [[nodiscard]] QString deviceId() const { return m_deviceId; }

    /**
     * @brief Returns parent device's public ID (extracted from object path)
     * @return Device ID as used in D-Bus paths (serial number or "dev_<hex>")
     *
     * Extracts device ID from object path segment:
     * /pl/jkolo/yubikey/oath/devices/<parentDeviceId>/credentials/<credId>
     *
     * @note This is different from deviceId() which returns the credential's
     * DeviceId property (internal hex hash used by daemon).
     * parentDeviceId() returns the public device identifier matching the
     * device's D-Bus "ID" property.
     */
    [[nodiscard]] QString parentDeviceId() const;

    // ========== D-Bus Methods ==========

    /**
     * @brief Generates TOTP/HOTP code
     * @return Result structure with (code, validUntil timestamp)
     *
     * Synchronous D-Bus call to GenerateCode().
     * Returns empty code and 0 timestamp on failure.
     */
    GenerateCodeResult generateCode();

    /**
     * @brief Copies code to clipboard
     * @return true on success, false on failure
     *
     * Synchronous D-Bus call to CopyToClipboard().
     * Generates code and copies to clipboard with auto-clear support.
     */
    bool copyToClipboard();

    /**
     * @brief Types code via keyboard emulation
     * @param fallbackToCopy If true, falls back to clipboard on typing failure
     * @return true on success, false on failure
     *
     * Synchronous D-Bus call to TypeCode(fallbackToCopy).
     * Generates code and types it using appropriate input method.
     */
    bool typeCode(bool fallbackToCopy = true);

    /**
     * @brief Deletes credential from YubiKey
     *
     * Synchronous D-Bus call to Delete().
     * After successful deletion, this proxy becomes invalid.
     * Parent DeviceProxy will emit credentialRemoved signal.
     */
    void deleteCredential();

    // ========== Value Type Conversion ==========

    /**
     * @brief Converts to CredentialInfo value type
     * @return CredentialInfo structure for D-Bus marshaling or display
     *
     * Used by clients that need CredentialInfo instead of proxy object.
     */
    CredentialInfo toCredentialInfo() const;

private:
    QString m_objectPath;
    QDBusInterface *m_interface{nullptr};

    // Cached properties (all const - never change after construction)
    QString m_fullName;
    QString m_issuer;
    QString m_username;
    bool m_requiresTouch{false};
    QString m_type;
    QString m_algorithm;
    int m_digits{0};
    int m_period{0};
    QString m_deviceId;

    // Code cache (mutable - updated on generateCode() calls)
    // PERFORMANCE: Caching eliminates N separate D-Bus calls when building matches
    // Cache is valid until validUntil timestamp (typically 30s for TOTP)
    mutable QString m_cachedCode;
    mutable qint64 m_cachedValidUntil{0};

    static constexpr const char *SERVICE_NAME = "pl.jkolo.yubikey.oath.daemon";
    static constexpr const char *INTERFACE_NAME = "pl.jkolo.yubikey.oath.Credential";
};

} // namespace Shared
} // namespace YubiKeyOath

#endif // OATH_CREDENTIAL_PROXY_H
