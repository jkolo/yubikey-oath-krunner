/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <QMap>
#include "shared/utils/version.h"
#include "shared/types/yubikey_model.h"

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

/**
 * @brief Extended device information from Management interface
 *
 * Contains data retrieved via GET DEVICE INFO command.
 * Available on YubiKey 4.1+ firmware.
 */
struct ManagementDeviceInfo {
    quint32 serialNumber = 0;      ///< Device serial number (4 bytes, big-endian)
    Version firmwareVersion;       ///< Firmware version (major.minor.patch)
    quint8 formFactor = 0;         ///< Form factor (1=Keychain, 2=Nano, 3=Nano-C, 4=USB-C, 5=USB-C Nano, etc.)
    quint8 usbSupported = 0;       ///< USB interfaces supported (bitfield)
    quint8 usbEnabled = 0;         ///< USB interfaces enabled (bitfield)
    quint16 nfcSupported = 0;      ///< NFC interfaces supported (2-byte bitfield)
    quint16 nfcEnabled = 0;        ///< NFC interfaces enabled (2-byte bitfield)
    bool configLocked = false;     ///< Configuration locked flag
    bool fips = false;             ///< FIPS compliant device
    bool sky = false;              ///< Security Key series (no serial number)
    quint8 autoEjectTimeout = 0;   ///< Auto-eject timeout in seconds (0 = disabled)
    quint8 challengeResponseTimeout = 0; ///< Challenge-response timeout in seconds
    quint16 deviceFlags = 0;       ///< Device-specific flags
};

/**
 * @brief Stateless utility class for YubiKey Management protocol
 *
 * This class provides pure functions for Management interface operations:
 * - Protocol constants (instruction codes, TLV tags)
 * - APDU command creation
 * - Response parsing (TLV format)
 * - Device info retrieval
 *
 * No state, no I/O - all functions are static.
 * Used by OathSession to get extended device information.
 *
 * Management interface is available on YubiKey 4.1+ firmware.
 * Provides serial number, form factor, capabilities, and more.
 */
class ManagementProtocol
{
public:
    // Class byte for Management commands
    static constexpr quint8 CLA = 0x00;

    // Instruction codes
    static constexpr quint8 INS_GET_DEVICE_INFO = 0x01;

    // P1 parameter for GET DEVICE INFO
    static constexpr quint8 P1_GET_DEVICE_INFO = 0x13;

    // Status words
    static constexpr quint16 SW_SUCCESS = 0x9000;
    static constexpr quint16 SW_INS_NOT_SUPPORTED = 0x6D00;

    // TLV tags for GET DEVICE INFO response
    static constexpr quint8 TAG_USB_SUPPORTED = 0x01;     ///< USB interfaces supported
    static constexpr quint8 TAG_SERIAL = 0x02;            ///< Serial number (4 bytes)
    static constexpr quint8 TAG_USB_ENABLED = 0x03;       ///< USB interfaces enabled
    static constexpr quint8 TAG_FORM_FACTOR = 0x04;       ///< Form factor (1 byte)
    static constexpr quint8 TAG_FIRMWARE_VERSION = 0x05;  ///< Firmware version (3 bytes)
    static constexpr quint8 TAG_AUTO_EJECT_TIMEOUT = 0x06; ///< Auto-eject timeout
    static constexpr quint8 TAG_CHALLENGE_RESPONSE_TIMEOUT = 0x07; ///< Challenge-response timeout
    static constexpr quint8 TAG_DEVICE_FLAGS = 0x08;      ///< Device flags (2 bytes)
    static constexpr quint8 TAG_CONFIG_LOCKED = 0x0A;     ///< Config locked flag
    static constexpr quint8 TAG_NFC_SUPPORTED = 0x0D;     ///< NFC interfaces supported
    static constexpr quint8 TAG_NFC_ENABLED = 0x0E;       ///< NFC interfaces enabled

    // Form factor values
    static constexpr quint8 FORM_FACTOR_USB_A_KEYCHAIN = 0x01;
    static constexpr quint8 FORM_FACTOR_USB_A_NANO = 0x02;
    static constexpr quint8 FORM_FACTOR_USB_C_KEYCHAIN = 0x03;
    static constexpr quint8 FORM_FACTOR_USB_C_NANO = 0x04;
    static constexpr quint8 FORM_FACTOR_USB_C_LIGHTNING = 0x05;
    static constexpr quint8 FORM_FACTOR_USB_A_BIO_KEYCHAIN = 0x06;
    static constexpr quint8 FORM_FACTOR_USB_C_BIO_KEYCHAIN = 0x07;

    // Management Application Identifier (AID)
    static const QByteArray MANAGEMENT_AID;

    // Command creation
    /**
     * @brief Creates SELECT Management application command
     * @return APDU command bytes
     */
    static QByteArray createSelectCommand();

    /**
     * @brief Creates GET DEVICE INFO command
     * @return APDU command bytes
     *
     * APDU format: 00 01 13 00
     * - CLA: 0x00
     * - INS: 0x01 (GET DEVICE INFO)
     * - P1:  0x13 (device info subcommand)
     * - P2:  0x00
     * - No data
     */
    static QByteArray createGetDeviceInfoCommand();

    // Response parsing
    /**
     * @brief Parses GET DEVICE INFO response
     * @param response Response data from GET DEVICE INFO command (TLV format)
     * @param outInfo Output parameter for parsed device information
     * @return true on success, false on parse error
     *
     * Response format: TLV-encoded data followed by status word (90 00)
     * Each TLV: [TAG (1 byte)][LENGTH (1 byte)][VALUE (LENGTH bytes)]
     *
     * Common tags:
     * - 0x02: Serial number (4 bytes, big-endian)
     * - 0x04: Form factor (1 byte)
     * - 0x05: Firmware version (3 bytes: major, minor, patch)
     * - 0x01/0x03: USB capabilities
     * - 0x0D/0x0E: NFC capabilities
     */
    static bool parseDeviceInfoResponse(const QByteArray &response,
                                       ManagementDeviceInfo &outInfo);

    // Helper functions
    /**
     * @brief Parses TLV data into tag-value map
     * @param data TLV-encoded data
     * @return Map of tag to value bytes
     *
     * Parses simple TLV format: [TAG][LENGTH][VALUE]...
     * Stops at status word (0x90 0x00) or end of data.
     */
    static QMap<quint8, QByteArray> parseTlv(const QByteArray &data);

    /**
     * @brief Extracts status word from response
     * @param response Response bytes
     * @return Status word (SW1 << 8 | SW2)
     */
    static quint16 getStatusWord(const QByteArray &response);

    /**
     * @brief Checks if status word indicates success
     * @param sw Status word
     * @return true if success (0x9000)
     */
    static bool isSuccess(quint16 sw);

    /**
     * @brief Converts form factor byte to human-readable name
     * @param formFactor Form factor byte from device info
     * @return Human-readable form factor name
     */
    static QString formFactorToString(quint8 formFactor);

private:
    // Private constructor - utility class only
    ManagementProtocol() = delete;
};

} // namespace Daemon
} // namespace YubiKeyOath
