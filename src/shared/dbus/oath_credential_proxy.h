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

    // ========== D-Bus Methods (all async) ==========

    /**
     * @brief Generates TOTP/HOTP code asynchronously
     *
     * Asynchronous D-Bus call to GenerateCode().
     * Checks cache first - returns cached if valid.
     * Result delivered via codeGenerated() signal.
     */
    void generateCode();

    /**
     * @brief Copies code to clipboard asynchronously
     *
     * Asynchronous D-Bus call to CopyToClipboard().
     * Generates code and copies to clipboard with auto-clear support.
     * Result delivered via clipboardCopied() signal.
     */
    void copyToClipboard();

    /**
     * @brief Types code via keyboard emulation asynchronously
     * @param fallbackToCopy If true, falls back to clipboard on typing failure
     *
     * Asynchronous D-Bus call to TypeCode(fallbackToCopy).
     * Generates code and types it using appropriate input method.
     * Result delivered via codeTyped() signal.
     */
    void typeCode(bool fallbackToCopy = true);

    /**
     * @brief Deletes credential from YubiKey asynchronously
     *
     * Asynchronous D-Bus call to Delete().
     * After successful deletion, this proxy becomes invalid.
     * Parent DeviceProxy will emit credentialRemoved signal.
     * Result delivered via deleted() signal.
     */
    void deleteCredential();

    // ========== Cache Getters ==========

    /**
     * @brief Gets cached code if still valid
     * @return Cached GenerateCodeResult (empty if no cache or expired)
     *
     * Used by KRunner to show placeholder while waiting for async result.
     */
    [[nodiscard]] GenerateCodeResult getCachedCode() const;

    /**
     * @brief Checks if code cache is still valid
     * @return true if cache exists and not expired
     */
    [[nodiscard]] bool isCacheValid() const;

    // ========== Value Type Conversion ==========

    /**
     * @brief Converts to CredentialInfo value type
     * @return CredentialInfo structure for D-Bus marshaling or display
     *
     * Used by clients that need CredentialInfo instead of proxy object.
     */
    CredentialInfo toCredentialInfo() const;

Q_SIGNALS:
    // === Result Signals ===
    /**
     * @brief Emitted when async code generation completes
     * @param code Generated TOTP/HOTP code (empty on error)
     * @param validUntil Timestamp when code expires (0 on error)
     * @param error Error message (empty on success)
     */
    void codeGenerated(const QString &code, qint64 validUntil, const QString &error);

    /**
     * @brief Emitted when async clipboard copy completes
     * @param success true if copied successfully
     * @param error Error message (empty on success)
     */
    void clipboardCopied(bool success, const QString &error);

    /**
     * @brief Emitted when async code typing completes
     * @param success true if typed successfully
     * @param error Error message (empty on success)
     */
    void codeTyped(bool success, const QString &error);

    /**
     * @brief Emitted when async deletion completes
     * @param success true if deletion succeeded
     * @param error Error message (empty on success)
     */
    void deleted(bool success, const QString &error);

    // === Workflow Status Signals ===
    /**
     * @brief Emitted when user needs to touch the device
     * @param timeoutSeconds Number of seconds before timeout
     * @param deviceModel Device model string for icon/description
     */
    void touchRequired(int timeoutSeconds, const QString &deviceModel);

    /**
     * @brief Emitted when touch workflow completes
     * @param success true if touch detected and operation continuing, false if cancelled/timeout
     */
    void touchCompleted(bool success);

    /**
     * @brief Emitted when device needs to be reconnected
     * @param deviceModel Device model string for icon/description
     */
    void reconnectRequired(const QString &deviceModel);

    /**
     * @brief Emitted when reconnect workflow completes
     * @param success true if device reconnected and operation continuing, false if cancelled
     */
    void reconnectCompleted(bool success);

private Q_SLOTS:
    void onCodeGenerated(const QString &code, qint64 validUntil, const QString &error);
    void onClipboardCopied(bool success, const QString &error);
    void onCodeTyped(bool success, const QString &error);
    void onDeleted(bool success, const QString &error);
    void onTouchRequired(int timeoutSeconds, const QString &deviceModel);
    void onTouchCompleted(bool success);
    void onReconnectRequired(const QString &deviceModel);
    void onReconnectCompleted(bool success);

private:  // NOLINT(readability-redundant-access-specifiers) - Required to close Q_SLOTS section for moc
    void connectToSignals();

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
