/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_model.h"
#include "device_brand.h"
#include "yubikey_model.h"
#include <QString>

namespace YubiKeyOath {
namespace Shared {

QString deviceModelToString(quint32 modelCode)
{
    // Handle zero/unknown model code
    if (modelCode == 0x00000000) {
        return QStringLiteral("Unknown Device");
    }

    // Detect brand from model code
    // YubiKey: first byte 0x01-0x0F (series encoding)
    // Nitrokey: first byte 0x02 in high generation bits (collision with YubiKey FIPS)
    //           but distinguishable by full code pattern

    const quint8 generationByte = (modelCode >> 24) & 0xFF;

    // Nitrokey detection: generation byte 0x02 with specific patterns
    // Nitrokey 3C: 0x02000A0F (gen=0x02, ver=0x00, patch=0x0A, fw=0x0F)
    // YubiKey 5 FIPS: 0x02XXXXXX but different version patterns
    if (generationByte == 0x02) {
        const quint8 versionByte = (modelCode >> 16) & 0xFF;

        // Nitrokey 3 has version byte 0x00 in most cases
        // YubiKey 5 FIPS has version byte in range 0x04-0x07
        if (versionByte == 0x00) {
            // This is Nitrokey 3
            // Decode generation to model name
            QString base = QStringLiteral("Nitrokey 3C");

            // Check for NFC capability (encoded in patch/fw bytes)
            const quint8 patchByte = (modelCode >> 8) & 0xFF;
            if (patchByte >= 0x0A) {  // 0x0A and above indicate NFC
                base += QStringLiteral(" NFC");
            }

            return base;
        }
    }

    // For all other cases (YubiKey or unknown), use existing YubiKey converter
    return modelToString(modelCode);
}

} // namespace Shared
} // namespace YubiKeyOath
