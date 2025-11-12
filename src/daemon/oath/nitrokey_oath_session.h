/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "yk_oath_session.h"
#include "nitrokey_secrets_oath_protocol.h"

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Nitrokey-specific OATH session implementation
 *
 * This class extends YkOathSession with Nitrokey-specific protocol variations:
 * - CALCULATE_ALL (0xA4) may not be supported (feature-gated in firmware)
 * - Automatic fallback to LIST + multiple CALCULATE when 0x6d00 returned
 * - Touch required status word: 0x6982 (instead of 0x6985)
 * - Serial number available in SELECT response via TAG_SERIAL_NUMBER (0x8F)
 *
 * Protocol differences from YubiKey:
 * 1. CALCULATE_ALL: May return 0x6D00 (INS_NOT_SUPPORTED) on some firmware versions
 *    → Fallback: LIST (0xA1) + multiple CALCULATE (0xA2) commands
 * 2. Touch requirement: Returns 0x6982 (SecurityStatusNotSatisfied) instead of 0x6985
 * 3. LIST command: Works reliably (no spurious touch errors like on YubiKey)
 * 4. Management interface: Not supported (0x6A82)
 *
 * Inherits all other behavior from YkOathSession:
 * - PC/SC I/O operations
 * - PBKDF2 key derivation
 * - HMAC authentication
 * - Session management
 *
 * Thread Safety:
 * - NOT thread-safe - caller must serialize access with mutex
 * - All PC/SC operations are synchronous blocking calls
 */
class NitrokeyOathSession : public YkOathSession
{
    Q_OBJECT

public:
    /**
     * @brief Constructs OATH session for Nitrokey device
     * @param cardHandle PC/SC card handle (non-owning reference)
     * @param protocol PC/SC protocol (T=0 or T=1)
     * @param deviceId Device ID (hex string) for logging/debugging
     * @param parent Parent QObject
     *
     * IMPORTANT: Caller retains ownership of cardHandle.
     * NitrokeyOathSession will NOT disconnect or release the handle.
     */
    explicit NitrokeyOathSession(SCARDHANDLE cardHandle,
                                 DWORD protocol,
                                 const QString &deviceId,
                                 QObject *parent = nullptr);

    /**
     * @brief Destructor
     */
    ~NitrokeyOathSession() override;

    // IOathSession interface overrides (Nitrokey-specific behavior)
    /**
     * @brief Calculates TOTP code for single credential (Nitrokey-specific)
     * @param name Full credential name (issuer:username)
     * @param period TOTP period in seconds (default 30)
     * @return Result with code string or error
     *
     * Nitrokey-specific: Checks for touch required status word 0x6982
     * (instead of 0x6985 used by YubiKey).
     */
    Result<QString> calculateCode(const QString &name, int period = 30) override;

    /**
     * @brief Calculates TOTP codes for all credentials (Nitrokey-specific)
     * @return Result with list of credentials with codes or error
     *
     * Nitrokey-specific behavior:
     * 1. Attempts CALCULATE_ALL (0xA4) command first
     * 2. If 0x6D00 (INS_NOT_SUPPORTED) returned, falls back to:
     *    - LIST (0xA1) to enumerate credentials
     *    - Multiple CALCULATE (0xA2) commands for each credential
     * 3. Returns aggregated results
     *
     * This fallback strategy handles Nitrokey 3 firmware versions where
     * CALCULATE_ALL is feature-gated and may not be available.
     */
    Result<QList<OathCredential>> calculateAll() override;
};

} // namespace Daemon
} // namespace YubiKeyOath
