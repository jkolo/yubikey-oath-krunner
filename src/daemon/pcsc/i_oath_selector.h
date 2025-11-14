/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "common/result.h"
#include "shared/utils/version.h"
#include <QByteArray>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

/**
 * @brief Interface for OATH applet selection
 *
 * Abstracts the SELECT OATH operation, allowing CardTransaction
 * to work with any OATH session implementation without depending
 * on concrete classes (Dependency Inversion Principle).
 *
 * This interface breaks the circular dependency between pcsc/ and oath/ layers:
 * - BEFORE: CardTransaction (pcsc/) → YkOathSession (oath/) ❌ WRONG DIRECTION
 * - AFTER:  CardTransaction (pcsc/) → IOathSelector (pcsc/) ✅ CORRECT
 *           YkOathSession (oath/) implements IOathSelector ✅ CORRECT
 *
 * Benefits:
 * - Clean separation between PC/SC infrastructure and OATH protocol
 * - Enables testing with mock implementations
 * - Allows reuse of CardTransaction with other smart card protocols
 *
 * @see CardTransaction
 * @see YkOathSession
 */
class IOathSelector
{
public:
    virtual ~IOathSelector() = default;

    /**
     * @brief Selects OATH applet on smart card
     *
     * This method sends the SELECT command (ISO 7816-4) to activate
     * the OATH applet on the card. The response contains:
     * - Challenge bytes (for HMAC authentication)
     * - Firmware version (from TAG_VERSION)
     * - Device capabilities (from TAG_ALGORITHM, TAG_NAME, etc.)
     *
     * @param outChallenge Output: Challenge bytes from SELECT response (for authentication)
     * @param outFirmwareVersion Output: Firmware version from TAG_VERSION
     * @return Result<void>::success() if SELECT succeeded, error() with description otherwise
     *
     * Error cases:
     * - Card not connected: "Device not connected"
     * - OATH applet not found: "OATH applet not found" (SW=0x6A82)
     * - Communication error: "Failed to communicate with device"
     *
     * Thread Safety:
     * - NOT thread-safe - caller must serialize access with mutex
     *
     * @note This method is called automatically by CardTransaction constructor
     *       (unless skipOathSelect=true). Do not call manually unless you know
     *       what you're doing.
     */
    virtual Result<void> selectOathApplication(QByteArray &outChallenge,
                                               Version &outFirmwareVersion) = 0;
};

} // namespace Daemon
} // namespace YubiKeyOath
