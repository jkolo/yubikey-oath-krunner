/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "oath_protocol.h"

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Nitrokey-specific OATH protocol implementation (trussed-secrets-app)
 *
 * This class extends OathProtocol base class with Nitrokey 3-specific behavior:
 *
 * **Touch Detection:**
 * - Uses status word 0x6982 (Nitrokey) instead of 0x6985 (YubiKey)
 * - Nitrokey firmware returns 0x6982 when credential requires physical touch
 *
 * **LIST v1 Strategy:**
 * - Nitrokey LIST command works correctly (no spurious errors like YubiKey)
 * - Supports LIST version 1 with properties byte:
 *   - Send: `00 A1 00 00 01 01` (data byte 0x01 requests version 1)
 *   - Response: `72 [len] [type+algo] [label...] [properties_byte]`
 *   - Properties byte (Bit 0=touch_required, Bit 1=encrypted, Bit 2=pws_data_exist)
 * - Single APDU gets all metadata including touch flag
 * - Then use individual CALCULATE for codes only when needed
 *
 * **CALCULATE_ALL Limitation:**
 * - Nitrokey 3 may not have `calculate-all` feature enabled (feature-gated)
 * - Returns 0x6D00 (INS not supported) when disabled
 * - LIST v1 is preferred strategy - faster and more reliable
 *
 * **Serial Number in SELECT:**
 * - Nitrokey includes TAG_SERIAL_NUMBER (0x8F, 4 bytes) in SELECT response
 * - No need for separate Management API or OTP/PIV calls
 * - Simplifies device identification
 *
 * **Supported Models:**
 * - Nitrokey 3A Mini (USB-A keychain)
 * - Nitrokey 3C Mini (USB-C keychain)
 * - Nitrokey 3A NFC (USB-A with NFC)
 * - Nitrokey 3C NFC (USB-C with NFC)
 *
 * Based on: github.com/Nitrokey/trussed-secrets-app (OATH authenticator implementation)
 */
class NitrokeySecretsOathProtocol : public OathProtocol
{
public:
    NitrokeySecretsOathProtocol() = default;
    ~NitrokeySecretsOathProtocol() override = default;

    // Brand-specific overrides
    /**
     * @brief Creates CALCULATE command with Le byte for CCID compatibility
     * @param name Credential name
     * @param challenge TOTP challenge (8 bytes)
     * @return APDU command bytes with Le byte
     *
     * Format: 00 A2 00 01 [Lc] [NAME tag+data] [CHALLENGE tag+data] 00
     * Le byte (0x00) required for CCID Case 4.
     */
    static QByteArray createCalculateCommand(const QString &name, const QByteArray &challenge);

    /**
     * @brief Parses CALCULATE response (Nitrokey touch: 0x6982)
     */
    QString parseCode(const QByteArray &response) const override;

    /**
     * @brief Parses SELECT response (Nitrokey includes TAG_SERIAL_NUMBER)
     */
    bool parseSelectResponse(const QByteArray &response,
                           QString &outDeviceId,
                           QByteArray &outChallenge,
                           Version &outFirmwareVersion,
                           bool &outRequiresPassword,
                           quint32 &outSerialNumber) const override;

    /**
     * @brief Parses CALCULATE ALL response (Nitrokey uses LIST v1 format)
     *
     * Note: This may not be called if CALCULATE_ALL is not supported (0x6D00).
     * NitrokeyOathSession should use LIST v1 strategy instead.
     */
    QList<OathCredential> parseCalculateAllResponse(const QByteArray &response) const override;

    // Nitrokey-specific extensions
    /**
     * @brief Creates standard LIST command (Nitrokey CCID requires Le byte)
     * @return APDU command bytes with Le byte for CCID compatibility
     *
     * Format: 00 A1 00 00 00 (Case 2: no data, expects response)
     * - CLA: 0x00
     * - INS: 0xA1 (LIST)
     * - P1: 0x00
     * - P2: 0x00
     * - Le: 0x00 (expect maximum response)
     *
     * Note: CCID transport requires Le byte, unlike CTAPHID which doesn't.
     */
    static QByteArray createListCommand();

    /**
     * @brief Creates LIST command with version 1 request
     * @return APDU command bytes (includes data byte 0x01 and Le byte)
     *
     * Format: 00 A1 00 00 01 01 00 (Case 4: data with expected response)
     * - CLA: 0x00
     * - INS: 0xA1 (LIST)
     * - P1: 0x00
     * - P2: 0x00
     * - Lc: 0x01 (1 data byte)
     * - Data: 0x01 (version 1 request)
     * - Le: 0x00 (expect maximum response - CCID requirement)
     *
     * Response includes properties byte at end of each credential:
     * 72 [len] [type+algo] [label...] [properties]
     */
    static QByteArray createListCommandV1();

    /**
     * @brief Parses LIST v1 response with properties byte
     * @param response Response data from LIST command (version 1)
     * @return List of credentials with metadata including touch_required flag
     *
     * Response format: 72 [len] [type+algo] [label...] [properties_byte]
     *
     * Properties byte (last byte of each credential):
     * - Bit 0 (0x01): touch_required
     * - Bit 1 (0x02): encrypted
     * - Bit 2 (0x04): pws_data_exist
     *
     * This method correctly extracts requiresTouch from properties byte,
     * unlike base parseCredentialList() which doesn't parse this field.
     */
    static QList<OathCredential> parseCredentialListV1(const QByteArray &response);
};

} // namespace Daemon
} // namespace YubiKeyOath
