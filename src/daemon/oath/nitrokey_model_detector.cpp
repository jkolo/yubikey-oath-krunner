/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "nitrokey_model_detector.h"
#include "../logging_categories.h"
#include <QStringLiteral>

namespace YubiKeyOath {
namespace Daemon {

using Shared::DeviceModel;
using Shared::DeviceBrand;
using Shared::Version;

// Nitrokey 3 generation codes (GG byte in 0xGGVVPPFF)
enum class NitrokeyGeneration : quint8 {
    NK3A = 0x01,        // Nitrokey 3A (USB-A variants)
    NK3C = 0x02,        // Nitrokey 3C (USB-C variants)
    NK3A_Mini = 0x04,   // Nitrokey 3A Mini
    NK3C_Mini = 0x05,   // Nitrokey 3C Mini (future)
};

// Port flags (PP byte) - same as YubiKey
constexpr quint8 PORT_USB_A = 0x01;
constexpr quint8 PORT_USB_C = 0x02;
constexpr quint8 PORT_NFC = 0x08;

// Capability flags (FF byte)
constexpr quint8 CAP_FIDO2 = 0x01;
constexpr quint8 CAP_OATH = 0x02;
constexpr quint8 CAP_OPENPGP = 0x04;
constexpr quint8 CAP_PIV = 0x08;

/**
 * @brief Detects if reader name contains Nitrokey 3
 */
static bool isNitrokey3Reader(const QString &readerName)
{
    return readerName.contains(QStringLiteral("Nitrokey"), Qt::CaseInsensitive) &&
           readerName.contains(QStringLiteral("3"));
}

/**
 * @brief Detects USB variant (A vs C) from firmware and serial
 *
 * Heuristics:
 * - Nitrokey 3C: typically firmware 1.6.0+, serial number present
 * - Nitrokey 3A: older firmware versions, may lack serial
 * - For now: assume 3C if firmware >= 1.6.0
 *
 * @param serialNumber Reserved for future use (may help distinguish variants)
 */
static NitrokeyGeneration detectUSBVariant(const Version &firmware, [[maybe_unused]] quint32 serialNumber)
{
    // Nitrokey 3C typically has firmware 1.6.0+
    // This is a heuristic - may need adjustment based on real data
    if (firmware.major() >= 1 && firmware.minor() >= 6) {
        qCDebug(OathDeviceManagerLog) << "Nitrokey variant detection: firmware" << firmware.toString()
                                         << "-> assuming 3C (heuristic: >=1.6.0)";
        return NitrokeyGeneration::NK3C;
    }

    // Fallback to 3A for older firmware
    qCDebug(OathDeviceManagerLog) << "Nitrokey variant detection: firmware" << firmware.toString()
                                     << "-> assuming 3A (heuristic: <1.6.0)";
    if (serialNumber == 0) {
        qCWarning(OathDeviceManagerLog) << "Nitrokey variant detection uncertain: no serial number available";
    }
    return NitrokeyGeneration::NK3A;
}

/**
 * @brief Detects NFC capability
 *
 * For Nitrokey 3, NFC is typically available on:
 * - Nitrokey 3A NFC
 * - Nitrokey 3C NFC
 *
 * Heuristic: If firmware >= 1.5.0 and not Mini, assume NFC capable
 * (Nitrokey 3 Mini variants lack NFC)
 */
static bool hasNFC(const Version &firmware, NitrokeyGeneration generation)
{
    // Mini variants don't have NFC
    if (generation == NitrokeyGeneration::NK3A_Mini || generation == NitrokeyGeneration::NK3C_Mini) {
        return false;
    }

    // NFC introduced in firmware 1.5.0+
    return firmware.major() >= 1 && firmware.minor() >= 5;
}

/**
 * @brief Constructs capabilities list for Nitrokey 3
 *
 * Nitrokey 3 supports:
 * - FIDO2 (WebAuthn/CTAP2)
 * - OATH (HOTP/TOTP)
 * - OpenPGP
 * - PIV (via PKCS#11)
 */
static QStringList getCapabilities()
{
    return {
        QStringLiteral("FIDO2"),
        QStringLiteral("OATH-HOTP"),
        QStringLiteral("OATH-TOTP"),
        QStringLiteral("OpenPGP"),
        QStringLiteral("PIV"),
    };
}

/**
 * @brief Generates model string from generation and NFC flag
 */
static QString generateModelString(NitrokeyGeneration generation, bool nfcCapable)
{
    QString base;

    switch (generation) {
    case NitrokeyGeneration::NK3A:
        base = QStringLiteral("Nitrokey 3A");
        break;
    case NitrokeyGeneration::NK3C:
        base = QStringLiteral("Nitrokey 3C");
        break;
    case NitrokeyGeneration::NK3A_Mini:
        base = QStringLiteral("Nitrokey 3A Mini");
        break;
    case NitrokeyGeneration::NK3C_Mini:
        base = QStringLiteral("Nitrokey 3C Mini");
        break;
    default:
        base = QStringLiteral("Nitrokey 3");
        break;
    }

    if (nfcCapable) {
        base += QStringLiteral(" NFC");
    }

    return base;
}

DeviceModel detectNitrokeyModel(const QString &readerName,
                                const Version &firmware,
                                quint32 serialNumber)
{
    qCInfo(OathDeviceManagerLog) << "Detecting Nitrokey model - Reader:" << readerName
                                    << "Firmware:" << firmware.toString()
                                    << "Serial:" << (serialNumber > 0 ? QString::number(serialNumber) : QStringLiteral("N/A"));

    DeviceModel model;

    // Verify this is a Nitrokey 3 device
    if (!isNitrokey3Reader(readerName)) {
        qCWarning(OathDeviceManagerLog) << "Reader name does not match Nitrokey 3 pattern:" << readerName;
        model.brand = DeviceBrand::Unknown;
        model.modelString = QStringLiteral("Unknown Device");
        return model;
    }

    // Detect USB variant (A vs C)
    const NitrokeyGeneration generation = detectUSBVariant(firmware, serialNumber);

    // Detect NFC capability
    const bool nfcCapable = hasNFC(firmware, generation);

    // Build ports byte (PP in 0xGGVVPPFF)
    quint8 ports = 0;
    if (generation == NitrokeyGeneration::NK3A || generation == NitrokeyGeneration::NK3A_Mini) {
        ports |= PORT_USB_A;
    } else {
        ports |= PORT_USB_C;
    }
    if (nfcCapable) {
        ports |= PORT_NFC;
    }

    // Build capabilities byte (FF in 0xGGVVPPFF)
    const quint8 capabilities = CAP_FIDO2 | CAP_OATH | CAP_OPENPGP | CAP_PIV;

    // Construct model code: 0xGGVVPPFF
    const quint8 variant = 0x00;  // Standard variant (no special editions yet)
    model.modelCode = (static_cast<quint32>(generation) << 24) |
                      (static_cast<quint32>(variant) << 16) |
                      (static_cast<quint32>(ports) << 8) |
                      capabilities;

    // Set brand and model string
    model.brand = DeviceBrand::Nitrokey;
    model.modelString = generateModelString(generation, nfcCapable);
    model.formFactor = 0;  // Not detected via reader name
    model.capabilities = getCapabilities();

    qCInfo(OathDeviceManagerLog) << "Nitrokey model detected:" << model.modelString
                                    << "Code:" << QString(QStringLiteral("0x%1")).arg(model.modelCode, 8, 16, QLatin1Char('0'))
                                    << "NFC:" << (nfcCapable ? "Yes" : "No");

    return model;
}

} // namespace Daemon
} // namespace YubiKeyOath
