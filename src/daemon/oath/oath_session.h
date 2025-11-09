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
#include "management_protocol.h"
#include "common/result.h"
#include "shared/types/yubikey_model.h"
#include "shared/utils/version.h"

// Forward declarations for PC/SC types
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

/**
 * @brief Extended device information retrieved from YubiKey
 *
 * Contains comprehensive device data from multiple sources:
 * - Serial number (from Management or PIV interface)
 * - Firmware version (from OATH SELECT or Management)
 * - Device model (derived from firmware/form factor)
 * - Form factor (from Management interface)
 */
struct ExtendedDeviceInfo {
    quint32 serialNumber = 0;      ///< Device serial number (0 if unavailable)
    Version firmwareVersion;       ///< Firmware version (major.minor.patch)
    YubiKeyModel deviceModel;      ///< Device model (series, variant, ports, capabilities)
    quint8 formFactor = 0;         ///< Form factor (1=Keychain, 2=Nano, etc.)
};

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
     * @param outFirmwareVersion Output parameter for firmware version from YubiKey
     * @return Result with success or error message
     *
     * This is the first command sent to YubiKey to establish OATH session.
     * Returns device ID via device() and challenge for authentication.
     */
    Result<void> selectOathApplication(QByteArray &outChallenge, Version &outFirmwareVersion);

    /**
     * @brief Calculates TOTP code for single credential
     * @param name Full credential name (issuer:username)
     * @param period TOTP period in seconds (default 30)
     * @return Result with code string or error
     *
     * Uses CALCULATE command (0xA2) with current timestamp.
     * Returns 6-8 digit code string.
     * Emits touchRequired() signal if credential requires physical touch.
     *
     * For credentials with non-standard period (≠30s), pass the correct period
     * to generate the proper TOTP challenge.
     */
    Result<QString> calculateCode(const QString &name, int period = 30);

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
     * @brief Sets new password on YubiKey
     * @param newPassword New password to set
     * @param deviceId Device ID for PBKDF2 salt
     * @return Result with success or error message
     *
     * Uses SET_CODE command (0x03) to configure authentication.
     * Flow:
     * 1. Derives key from password using PBKDF2 (salt = deviceId, 1000 iterations)
     * 2. Generates challenge for mutual authentication
     * 3. Calculates HMAC-SHA1 response
     * 4. Sends SET_CODE with key, challenge, and response
     * 5. Verifies YubiKey's response
     *
     * Requires prior authentication if password already exists.
     */
    Result<void> setPassword(const QString &newPassword, const QString &deviceId);

    /**
     * @brief Removes password from YubiKey
     * @return Result with success or error message
     *
     * Uses SET_CODE command (0x03) with length 0 to remove authentication.
     * Requires prior authentication with current password.
     */
    Result<void> removePassword();

    /**
     * @brief Changes password on YubiKey
     * @param oldPassword Current password
     * @param newPassword New password (empty string removes password)
     * @param deviceId Device ID for PBKDF2 salt
     * @return Result with success or error message
     *
     * Combines authenticate() + setPassword() or removePassword().
     * If newPassword is empty, removes password instead.
     */
    Result<void> changePassword(const QString &oldPassword,
                                const QString &newPassword,
                                const QString &deviceId);

    /**
     * @brief Retrieves extended device information (serial, firmware, form factor)
     * @return Result with ExtendedDeviceInfo or error message
     *
     * Comprehensive device data retrieval strategy:
     * 1. Try Management GET DEVICE INFO (YubiKey 4.1+):
     *    - Gets serial, firmware, form factor in single call
     *    - Most efficient and comprehensive
     * 2. Fallback to PIV GET SERIAL (YubiKey NEO, 4, 5):
     *    - Gets serial number only
     *    - Firmware from previous OATH SELECT
     *    - Form factor unavailable (set to 0)
     * 3. Final fallback:
     *    - Serial = 0 (unavailable)
     *    - Firmware from OATH SELECT
     *    - Device model derived from firmware only
     *
     * IMPORTANT: Must re-select OATH application after Management/PIV!
     * This method automatically restores OATH session state.
     *
     * Usage: Call once during device connection to cache device info.
     *
     * @param readerName Optional PC/SC reader name for fallback detection (e.g., "Yubico YubiKey NEO OTP+CCID (0003507404) 00 00")
     *                   Used for parsing device model from reader name when Management interface unavailable
     */
    Result<ExtendedDeviceInfo> getExtendedDeviceInfo(const QString &readerName = QString());

    /**
     * @brief Cancels pending operation by sending SELECT
     *
     * Useful for interrupting long-running touch-required operations.
     * Sends SELECT command to reset YubiKey state.
     */
    void cancelOperation();

    /**
     * @brief Updates card handle after reconnect
     * @param newHandle New PC/SC card handle
     * @param newProtocol New PC/SC protocol
     *
     * Called by YubiKeyOathDevice after successful reconnect to update
     * the handle without destroying the OathSession object.
     * Marks session as inactive (requires SELECT after reconnect).
     */
    void updateCardHandle(SCARDHANDLE newHandle, DWORD newProtocol);

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

    /**
     * @brief Emitted when card reset is detected (SCARD_W_RESET_CARD)
     * @param command The APDU command that failed due to reset
     *
     * Triggered when external application (like ykman) resets the card.
     * This signal initiates reconnect workflow through upper layers.
     */
    void cardResetDetected(const QByteArray &command);

    /**
     * @brief Emitted when reconnect completed successfully
     *
     * This signal is emitted by upper layer (YubiKeyOathDevice) after
     * successful reconnect. OathSession waits for this in sendApdu()
     * to retry the failed command.
     */
    void reconnectReady();

    /**
     * @brief Emitted when reconnect failed
     *
     * This signal is emitted by upper layer (YubiKeyOathDevice) when
     * reconnect attempts fail. OathSession waits for this in sendApdu()
     * to abort the operation.
     */
    void reconnectFailed();

private:
    /**
     * @brief Sends APDU command to YubiKey with chained response handling
     * @param command APDU command bytes
     * @param retryCount Internal parameter for retry recursion guard (default 0)
     * @return Response data including status word, or empty on error
     *
     * Handles chained responses:
     * - If SW=0x61XX (more data available), sends SEND REMAINING (0xA5)
     * - Accumulates all data parts into single response
     * - Returns full data with final status word
     *
     * Handles card reset (SCARD_W_RESET_CARD):
     * - Attempts SCardReconnect() to refresh handle
     * - Retries command once after successful reconnect
     * - Prevents infinite recursion with retryCount guard
     */
    QByteArray sendApdu(const QByteArray &command, int retryCount = 0);

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

    /**
     * @brief Ensures OATH session is active, reactivates if needed
     * @return Result with success or error message
     *
     * Checks m_sessionActive flag. If session is inactive, executes SELECT
     * to reactivate OATH applet. This is needed after external apps (like ykman)
     * interact with YubiKey and may leave it in a different state.
     */
    Result<void> ensureSessionActive();

    /**
     * @brief Attempts to reconnect to card after reset
     * @return true if reconnect successful, false otherwise
     *
     * Uses SCardReconnect() to refresh the card handle after external
     * apps (like ykman) reset the card. This preserves the connection
     * without requiring full disconnect/connect cycle.
     *
     * After successful reconnect, m_sessionActive is set to false
     * (OATH applet needs to be selected again).
     */
    bool reconnectCard();

    SCARDHANDLE m_cardHandle;  ///< PC/SC card handle (non-owning)
    DWORD m_protocol;          ///< PC/SC protocol (T=0 or T=1)
    QString m_deviceId;        ///< Device ID from SELECT response
    bool m_sessionActive = false;  ///< Session state tracking - true if OATH applet is selected
};

} // namespace Daemon
} // namespace YubiKeyOath
