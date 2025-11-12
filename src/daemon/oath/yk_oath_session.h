/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <memory>
#include <QByteArray>
#include <QString>
#include <QList>
#include <QObject>
#include "types/oath_credential.h"
#include "types/oath_credential_data.h"
#include "oath_protocol.h"
#include "yk_oath_protocol.h"
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
 * @brief Extended device information from Management/PIV APIs
 */
struct ExtendedDeviceInfo {
    Version firmwareVersion;    ///< Firmware version
    YubiKeyModel model{0};      ///< Device model (0xSSVVPPFF)
    YubiKeyModel deviceModel{0}; ///< Alias for model (for compatibility)
    quint32 serialNumber{0};    ///< Device serial number
    quint8 formFactor{0};       ///< Form factor code
};

/**
 * @brief YubiKey-specific OATH session implementation (base class)
 *
 * This class handles full OATH protocol communication with YubiKey devices:
 * - PC/SC I/O operations (sendApdu with chained response handling)
 * - High-level OATH operations (select, list, calculate, authenticate)
 * - Business logic (PBKDF2 key derivation, HMAC authentication)
 *
 * YubiKey-specific behavior:
 * - Uses CALCULATE_ALL (0xA4) command without fallback
 * - Touch required status word: 0x6985
 * - Serial number via Management/PIV APIs (not in SELECT response)
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
 * - cardResetDetected() - emitted when SCARD_W_RESET_CARD detected
 * - reconnectReady() - emitted by upper layer after successful reconnect
 * - reconnectFailed() - emitted by upper layer when reconnect fails
 */
class YkOathSession : public QObject
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
     * YkOathSession will NOT disconnect or release the handle.
     */
    explicit YkOathSession(SCARDHANDLE cardHandle,
                          DWORD protocol,
                          const QString &deviceId,
                          QObject *parent = nullptr);

    /**
     * @brief Destructor - does NOT disconnect card handle
     */
    virtual ~YkOathSession();

    // OATH protocol operations (virtual methods can be overridden in subclasses)
    virtual Result<void> selectOathApplication(QByteArray &outChallenge, Version &outFirmwareVersion);
    virtual Result<QString> calculateCode(const QString &name, int period = 30);
    virtual Result<QList<OathCredential>> calculateAll();
    virtual Result<QList<OathCredential>> listCredentials();
    virtual Result<void> authenticate(const QString &password, const QString &deviceId);
    virtual Result<void> putCredential(const OathCredentialData &data);
    virtual Result<void> deleteCredential(const QString &name);
    virtual Result<void> setPassword(const QString &newPassword, const QString &deviceId);
    virtual Result<void> removePassword();
    virtual Result<void> changePassword(const QString &oldPassword,
                                const QString &newPassword,
                                const QString &deviceId);
    virtual Result<ExtendedDeviceInfo> getExtendedDeviceInfo(const QString &readerName = QString());
    virtual void cancelOperation();
    virtual void updateCardHandle(SCARDHANDLE newHandle, DWORD newProtocol);
    [[nodiscard]] virtual QString deviceId() const { return m_deviceId; }
    [[nodiscard]] virtual bool requiresPassword() const { return m_requiresPassword; }
    [[nodiscard]] virtual quint32 selectSerialNumber() const { return m_selectSerialNumber; }

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
     * successful reconnect. YkOathSession waits for this in sendApdu()
     * to retry the failed command.
     */
    void reconnectReady();

    /**
     * @brief Emitted when reconnect failed
     *
     * This signal is emitted by upper layer (YubiKeyOathDevice) when
     * reconnect attempts fail. YkOathSession waits for this in sendApdu()
     * to abort the operation.
     */
    void reconnectFailed();

protected:
    /**
     * @brief Sends APDU command to device with chained response handling
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
     * - Emits cardResetDetected() signal to trigger reconnect workflow
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
     * interact with device and may leave it in a different state.
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

    // Protected member variables (accessible to derived classes)
    SCARDHANDLE m_cardHandle;  ///< PC/SC card handle (non-owning)
    DWORD m_protocol;          ///< PC/SC protocol (T=0 or T=1)
    QString m_deviceId;        ///< Device ID from SELECT response
    Version m_firmwareVersion;  ///< Firmware version from SELECT TAG_VERSION
    quint32 m_selectSerialNumber = 0;  ///< Serial number from SELECT TAG_SERIAL_NUMBER (0x8F), strategy #0
    bool m_requiresPassword = false;  ///< Password requirement from SELECT TAG_CHALLENGE presence
    bool m_sessionActive = false;  ///< Session state tracking - true if OATH applet is selected
    std::unique_ptr<OathProtocol> m_oathProtocol;  ///< Brand-specific OATH protocol implementation
    qint64 m_lastPcscOperationTime = 0;  ///< Timestamp (ms since epoch) of last PC/SC operation for rate limiting
};

} // namespace Daemon
} // namespace YubiKeyOath
