/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "oath_protocol.h"

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief YubiKey-specific OATH protocol implementation
 *
 * This class extends OathProtocol base class with YubiKey-specific behavior:
 *
 * **Touch Detection:**
 * - Uses status word 0x6985 (YubiKey) instead of 0x6982 (Nitrokey)
 * - YubiKey firmware returns 0x6985 when credential requires physical touch
 *
 * **CALCULATE_ALL Strategy:**
 * - YubiKey LIST command has spurious 0x6985 errors (firmware bug)
 * - CALCULATE_ALL is preferred workaround - gets both metadata AND codes in single APDU
 * - Response format: NAME (0x71) + RESPONSE (0x76) or TOUCH (0x7c) or HOTP (0x77)
 *
 * **Serial Number Retrieval:**
 * - YubiKey does NOT include TAG_SERIAL_NUMBER (0x8F) in SELECT response
 * - Must use Management Application interface (YubiKey 4/5)
 * - Fallback: OTP Application GET_SERIAL (YubiKey NEO 3.4.0)
 * - Fallback: PIV Application GET SERIAL
 * - Last resort: Parse PC/SC reader name for NEO devices
 *
 * **Supported Models:**
 * - YubiKey NEO (firmware 3.x) - OTP/PIV serial retrieval
 * - YubiKey 4 (firmware 4.x) - Management API + PIV fallback
 * - YubiKey 5 (firmware 5.x) - Full Management API support
 * - YubiKey 5 FIPS - Same as YubiKey 5
 * - YubiKey Bio (firmware 5.x+) - Same as YubiKey 5
 */
class YKOathProtocol : public OathProtocol
{
public:
    YKOathProtocol() = default;
    ~YKOathProtocol() override = default;

    // Brand-specific overrides
    /**
     * @brief Parses CALCULATE response (YubiKey touch: 0x6985)
     */
    QString parseCode(const QByteArray &response) const override;

    /**
     * @brief Parses CALCULATE ALL response (YubiKey NAME+RESPONSE format)
     */
    QList<OathCredential> parseCalculateAllResponse(const QByteArray &response) const override;

    /**
     * @brief Parses SELECT response (YubiKey - no TAG_SERIAL_NUMBER)
     */
    bool parseSelectResponse(const QByteArray &response,
                           QString &outDeviceId,
                           QByteArray &outChallenge,
                           Version &outFirmwareVersion,
                           bool &outRequiresPassword,
                           quint32 &outSerialNumber) const override;
};

} // namespace Daemon
} // namespace YubiKeyOath
