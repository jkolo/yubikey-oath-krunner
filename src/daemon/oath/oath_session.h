/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <QList>
#include <QObject>
#include "types/oath_credential.h"
#include "oath_protocol.h"
#include "common/result.h"

// Forward declarations for PC/SC types
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Manages OATH session with YubiKey device
 *
 * This class handles full OATH protocol communication with a single YubiKey device:
 * - PC/SC I/O operations (sendApdu with chained response handling)
 * - High-level OATH operations (select, list, calculate, authenticate)
 * - Business logic (PBKDF2 key derivation, HMAC authentication)
 *
 * Uses OathProtocol for command building and response parsing.
 *
 * Ownership:
 * - Does NOT own SCARDHANDLE or SCARDCONTEXT (passed in constructor)
 * - Caller is responsible for card handle lifecycle
 *
 * Thread Safety:
 * - NOT thread-safe - caller must serialize access with mutex
 * - All PC/SC operations are synchronous blocking calls
 *
 * Signals:
 * - touchRequired() - emitted when YubiKey requires physical touch (SW=0x6985)
 * - errorOccurred() - emitted when PC/SC communication fails
 */
class OathSession : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs OATH session for YubiKey device
     * @param cardHandle PC/SC card handle (non-owning reference)
     * @param protocol PC/SC protocol (T=0 or T=1)
     * @param deviceId Device ID (hex string) for logging/debugging
     * @param parent Parent QObject
     *
     * IMPORTANT: Caller retains ownership of cardHandle.
     * OathSession will NOT disconnect or release the handle.
     */
    explicit OathSession(SCARDHANDLE cardHandle,
                        DWORD protocol,
                        const QString &deviceId,
                        QObject *parent = nullptr);

    /**
     * @brief Destructor - does NOT disconnect card handle
     */
    ~OathSession();

    // High-level OATH operations
    /**
     * @brief Selects OATH application on YubiKey
     * @param outChallenge Output parameter for challenge from YubiKey
     * @return Result with success or error message
     *
     * This is the first command sent to YubiKey to establish OATH session.
     * Returns device ID via device() and challenge for authentication.
     */
    Result<void> selectOathApplication(QByteArray &outChallenge);

    /**
     * @brief Calculates TOTP code for single credential
     * @param name Full credential name (issuer:username)
     * @return Result with code string or error
     *
     * Uses CALCULATE command (0xA2) with current timestamp.
     * Returns 6-8 digit code string.
     * Emits touchRequired() signal if credential requires physical touch.
     */
    Result<QString> calculateCode(const QString &name);

    /**
     * @brief Calculates TOTP codes for all credentials
     * @return Result with list of credentials with codes or error
     *
     * Uses CALCULATE ALL command (0xA4) with current timestamp.
     * More efficient than multiple CALCULATE commands.
     * Returns credentials with codes and validity timestamps.
     */
    Result<QList<OathCredential>> calculateAll();

    /**
     * @brief Authenticates session with password
     * @param password YubiKey OATH password
     * @param deviceId Device ID for PBKDF2 salt
     * @return Result with success or error message
     *
     * Full authentication flow:
     * 1. Executes SELECT to get fresh challenge from YubiKey
     * 2. Derives key from password using PBKDF2 (salt = deviceId, 1000 iterations)
     * 3. Calculates HMAC-SHA1 response to challenge
     * 4. Sends VALIDATE command with response and our challenge
     * 5. Verifies YubiKey's response to our challenge (mutual auth)
     *
     * After successful authentication, subsequent commands will work without password.
     * Each authentication requires fresh SELECT to get new challenge.
     */
    Result<void> authenticate(const QString &password, const QString &deviceId);

    /**
     * @brief Adds or updates credential on YubiKey
     * @param data Credential data (name, secret, algorithm, etc.)
     * @return Result with success or error message
     *
     * Uses PUT command (0x01) to add new credential or overwrite existing.
     * Requires authentication if validation is configured on YubiKey.
     *
     * Possible errors:
     * - Invalid Base32 secret
     * - Insufficient space (0x6a84)
     * - Authentication required (0x6982)
     * - Wrong data format (0x6a80)
     */
    Result<void> putCredential(const OathCredentialData &data);

    /**
     * @brief Deletes credential from YubiKey
     * @param name Full credential name (issuer:username)
     * @return Result with success or error message
     *
     * Uses DELETE command (0x02) to remove credential.
     * Requires authentication if validation is configured on YubiKey.
     *
     * Possible errors:
     * - No such object (0x6984) - credential not found
     * - Authentication required (0x6982)
     * - Wrong data format (0x6a80)
     */
    Result<void> deleteCredential(const QString &name);

    /**
     * @brief Cancels pending operation by sending SELECT
     *
     * Useful for interrupting long-running touch-required operations.
     * Sends SELECT command to reset YubiKey state.
     */
    void cancelOperation();

    /**
     * @brief Gets device ID from last SELECT response
     * @return Device ID (hex string) or empty if SELECT not yet executed
     */
    QString deviceId() const { return m_deviceId; }

Q_SIGNALS:
    /**
     * @brief Emitted when YubiKey requires physical touch
     *
     * Triggered when CALCULATE returns status word 0x6985.
     * Client should show touch prompt to user.
     */
    void touchRequired();

    /**
     * @brief Emitted when PC/SC communication error occurs
     * @param error Error description
     */
    void errorOccurred(const QString &error);

private:
    /**
     * @brief Sends APDU command to YubiKey with chained response handling
     * @param command APDU command bytes
     * @return Response data including status word, or empty on error
     *
     * Handles chained responses:
     * - If SW=0x61XX (more data available), sends SEND REMAINING (0xA5)
     * - Accumulates all data parts into single response
     * - Returns full data with final status word
     */
    QByteArray sendApdu(const QByteArray &command);

    /**
     * @brief Derives PBKDF2 key from password
     * @param password Password string
     * @param salt Salt bytes (typically device ID)
     * @param iterations Number of iterations (typically 1000)
     * @param keyLength Desired key length in bytes (typically 16)
     * @return Derived key bytes
     */
    static QByteArray deriveKeyPbkdf2(const QByteArray &password,
                                     const QByteArray &salt,
                                     int iterations,
                                     int keyLength);

    SCARDHANDLE m_cardHandle;  ///< PC/SC card handle (non-owning)
    DWORD m_protocol;          ///< PC/SC protocol (T=0 or T=1)
    QString m_deviceId;        ///< Device ID from SELECT response
    bool m_sessionActive = false;  ///< Session state tracking - true if OATH applet is selected
};

} // namespace Daemon
} // namespace YubiKeyOath
