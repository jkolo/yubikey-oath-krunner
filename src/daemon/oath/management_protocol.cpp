/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "management_protocol.h"
#include "../logging_categories.h"

#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

// Management Application Identifier
// A0 00 00 05 27 20 01 01
const QByteArray ManagementProtocol::MANAGEMENT_AID = QByteArray::fromHex("a000000527200101");

// =============================================================================
// Command Creation
// =============================================================================

QByteArray ManagementProtocol::createSelectCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));                         // CLA
    command.append((char)0xA4);                                     // INS = SELECT
    command.append((char)0x04);                                     // P1 = Select by name
    command.append((char)0x00);                                     // P2
    command.append(static_cast<char>(MANAGEMENT_AID.length()));     // Lc
    command.append(MANAGEMENT_AID);                                 // Data = AID

    return command;
}

QByteArray ManagementProtocol::createGetDeviceInfoCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));                 // CLA = 0x00
    command.append(static_cast<char>(INS_GET_DEVICE_INFO)); // INS = 0x01
    command.append(static_cast<char>(P1_GET_DEVICE_INFO));  // P1 = 0x13
    command.append((char)0x00);                             // P2 = 0x00
    // No Lc, no data, no Le

    return command;
}

// =============================================================================
// Response Parsing
// =============================================================================

bool ManagementProtocol::parseDeviceInfoResponse(const QByteArray &response,
                                                ManagementDeviceInfo &outInfo)
{
    // Response format: TLV data + status word (90 00)
    if (response.length() < 2) {
        qCWarning(YubiKeyOathDeviceLog) << "Device info response too short:" << response.length();
        return false;
    }

    // Check status word
    const quint16 sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        qCWarning(YubiKeyOathDeviceLog) << "Device info failed, status word:"
                                        << Qt::hex << Qt::showbase << sw;
        return false;
    }

    // Parse TLV data
    // Format: [LENGTH byte][TLV data][SW1 SW2]
    // Skip first byte (length header) and last 2 bytes (status word)
    const QByteArray tlvData = response.mid(1, response.length() - 3);
    QMap<quint8, QByteArray> tlvMap = parseTlv(tlvData);

    if (tlvMap.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "No TLV data in device info response";
        return false;
    }

    // Extract serial number (TAG_SERIAL = 0x02, 4 bytes big-endian)
    if (tlvMap.contains(TAG_SERIAL)) {
        QByteArray serialBytes = tlvMap[TAG_SERIAL];
        if (serialBytes.length() == 4) {
            outInfo.serialNumber = (static_cast<quint8>(serialBytes[0]) << 24) |
                                  (static_cast<quint8>(serialBytes[1]) << 16) |
                                  (static_cast<quint8>(serialBytes[2]) << 8) |
                                  (static_cast<quint8>(serialBytes[3]));
        } else {
            qCWarning(YubiKeyOathDeviceLog) << "Serial number has invalid length:"
                                            << serialBytes.length() << "(expected 4)";
        }
    }

    // Extract firmware version (TAG_FIRMWARE_VERSION = 0x05, 3+ bytes: major.minor.patch[.build])
    if (tlvMap.contains(TAG_FIRMWARE_VERSION)) {
        QByteArray fwBytes = tlvMap[TAG_FIRMWARE_VERSION];
        if (fwBytes.length() >= 3) {
            outInfo.firmwareVersion = Version(
                static_cast<quint8>(fwBytes[0]),  // major
                static_cast<quint8>(fwBytes[1]),  // minor
                static_cast<quint8>(fwBytes[2])   // patch
            );
            // Note: 4th byte (build/qualifier) is ignored if present
        } else {
            qCWarning(YubiKeyOathDeviceLog) << "Firmware version has invalid length:"
                                            << fwBytes.length() << "(expected at least 3)";
        }
    }

    // Extract form factor (TAG_FORM_FACTOR = 0x04, 1 byte)
    if (tlvMap.contains(TAG_FORM_FACTOR)) {
        QByteArray formBytes = tlvMap[TAG_FORM_FACTOR];
        if (formBytes.length() == 1) {
            outInfo.formFactor = static_cast<quint8>(formBytes[0]);
        }
    }

    // Extract USB capabilities (optional)
    if (tlvMap.contains(TAG_USB_SUPPORTED)) {
        QByteArray usbSupportedBytes = tlvMap[TAG_USB_SUPPORTED];
        if (usbSupportedBytes.length() == 1) {
            outInfo.usbSupported = static_cast<quint8>(usbSupportedBytes[0]);
        }
    }

    if (tlvMap.contains(TAG_USB_ENABLED)) {
        QByteArray usbEnabledBytes = tlvMap[TAG_USB_ENABLED];
        if (usbEnabledBytes.length() == 1) {
            outInfo.usbEnabled = static_cast<quint8>(usbEnabledBytes[0]);
        }
    }

    // Extract NFC capabilities (optional)
    // YubiKey 5 series uses 2-byte bitfield, older devices may use 1 byte
    if (tlvMap.contains(TAG_NFC_SUPPORTED)) {
        QByteArray nfcSupportedBytes = tlvMap[TAG_NFC_SUPPORTED];
        if (nfcSupportedBytes.length() == 2) {
            // YubiKey 5 series: 2 bytes big-endian
            outInfo.nfcSupported = (static_cast<quint8>(nfcSupportedBytes[0]) << 8) |
                                    static_cast<quint8>(nfcSupportedBytes[1]);
        } else if (nfcSupportedBytes.length() == 1) {
            // Older YubiKeys: 1 byte (fallback)
            outInfo.nfcSupported = static_cast<quint8>(nfcSupportedBytes[0]);
        }
    }

    if (tlvMap.contains(TAG_NFC_ENABLED)) {
        QByteArray nfcEnabledBytes = tlvMap[TAG_NFC_ENABLED];
        if (nfcEnabledBytes.length() == 2) {
            // YubiKey 5 series: 2 bytes big-endian
            outInfo.nfcEnabled = (static_cast<quint8>(nfcEnabledBytes[0]) << 8) |
                                  static_cast<quint8>(nfcEnabledBytes[1]);
        } else if (nfcEnabledBytes.length() == 1) {
            // Older YubiKeys: 1 byte (fallback)
            outInfo.nfcEnabled = static_cast<quint8>(nfcEnabledBytes[0]);
        }
    }

    // Extract config locked (optional)
    if (tlvMap.contains(TAG_CONFIG_LOCKED)) {
        QByteArray configLockedBytes = tlvMap[TAG_CONFIG_LOCKED];
        if (configLockedBytes.length() == 1) {
            outInfo.configLocked = (static_cast<quint8>(configLockedBytes[0]) != 0);
        }
    }

    // Extract device flags (optional, 2 bytes)
    if (tlvMap.contains(TAG_DEVICE_FLAGS)) {
        QByteArray flagsBytes = tlvMap[TAG_DEVICE_FLAGS];
        if (flagsBytes.length() == 2) {
            outInfo.deviceFlags = (static_cast<quint8>(flagsBytes[0]) << 8) |
                                 (static_cast<quint8>(flagsBytes[1]));
        }
    }

    // Extract auto-eject timeout (optional)
    if (tlvMap.contains(TAG_AUTO_EJECT_TIMEOUT)) {
        QByteArray timeoutBytes = tlvMap[TAG_AUTO_EJECT_TIMEOUT];
        if (timeoutBytes.length() == 1) {
            outInfo.autoEjectTimeout = static_cast<quint8>(timeoutBytes[0]);
        }
    }

    // Extract challenge-response timeout (optional)
    if (tlvMap.contains(TAG_CHALLENGE_RESPONSE_TIMEOUT)) {
        QByteArray timeoutBytes = tlvMap[TAG_CHALLENGE_RESPONSE_TIMEOUT];
        if (timeoutBytes.length() == 1) {
            outInfo.challengeResponseTimeout = static_cast<quint8>(timeoutBytes[0]);
        }
    }

    qCInfo(YubiKeyOathDeviceLog) << "Parsed device info:"
                                 << "serial=" << outInfo.serialNumber
                                 << "firmware=" << outInfo.firmwareVersion.toString()
                                 << "formFactor=" << outInfo.formFactor
                                 << "(" << formFactorToString(outInfo.formFactor) << ")";

    return true;
}

// =============================================================================
// Helper Functions
// =============================================================================

QMap<quint8, QByteArray> ManagementProtocol::parseTlv(const QByteArray &data)
{
    QMap<quint8, QByteArray> result;

    int pos = 0;
    while (pos < data.length()) {
        // Check if we've reached status word (0x90 0x00)
        if (pos + 1 < data.length() &&
            static_cast<quint8>(data[pos]) == 0x90 &&
            static_cast<quint8>(data[pos + 1]) == 0x00) {
            break; // Stop at status word
        }

        // Need at least tag + length
        if (pos + 2 > data.length()) {
            qCWarning(YubiKeyOathDeviceLog) << "Incomplete TLV at position" << pos;
            break;
        }

        const auto tag = static_cast<quint8>(data[pos]);
        const auto length = static_cast<quint8>(data[pos + 1]);

        // Check if we have enough data for value
        if (pos + 2 + length > data.length()) {
            qCWarning(YubiKeyOathDeviceLog) << "TLV value extends beyond data:"
                                            << "tag=" << Qt::hex << Qt::showbase << tag
                                            << "length=" << length
                                            << "pos=" << pos
                                            << "dataLength=" << data.length();
            break;
        }

        const QByteArray value = data.mid(pos + 2, length);
        result[tag] = value;

        pos += 2 + length;
    }

    return result;
}

quint16 ManagementProtocol::getStatusWord(const QByteArray &response)
{
    if (response.length() < 2) {
        return 0;
    }

    // Status word is last 2 bytes: SW1 << 8 | SW2
    const auto sw1 = static_cast<quint8>(response[response.length() - 2]);
    const auto sw2 = static_cast<quint8>(response[response.length() - 1]);

    return static_cast<quint16>((sw1 << 8) | sw2);
}

bool ManagementProtocol::isSuccess(quint16 sw)
{
    return sw == SW_SUCCESS;
}

QString ManagementProtocol::formFactorToString(quint8 formFactor)
{
    switch (formFactor) {
    case FORM_FACTOR_USB_A_KEYCHAIN:
        return QStringLiteral("USB-A Keychain");
    case FORM_FACTOR_USB_A_NANO:
        return QStringLiteral("USB-A Nano");
    case FORM_FACTOR_USB_C_KEYCHAIN:
        return QStringLiteral("USB-C Keychain");
    case FORM_FACTOR_USB_C_NANO:
        return QStringLiteral("USB-C Nano");
    case FORM_FACTOR_USB_C_LIGHTNING:
        return QStringLiteral("USB-C Lightning");
    case FORM_FACTOR_USB_A_BIO_KEYCHAIN:
        return QStringLiteral("USB-A Bio Keychain");
    case FORM_FACTOR_USB_C_BIO_KEYCHAIN:
        return QStringLiteral("USB-C Bio Keychain");
    default:
        return QStringLiteral("Unknown (0x%1)").arg(formFactor, 2, 16, QLatin1Char('0'));
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
