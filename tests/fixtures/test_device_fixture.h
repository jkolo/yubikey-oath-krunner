/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../../src/daemon/storage/oath_database.h"
#include "../../src/shared/utils/version.h"
#include "../../src/shared/types/device_model.h"
#include "../../src/shared/types/device_brand.h"
#include <QString>
#include <QDateTime>

using namespace YubiKeyOath::Daemon;
using namespace YubiKeyOath::Shared;

/**
 * @brief Factory for creating test OathDatabase::DeviceRecord objects
 *
 * Provides pre-configured device records for consistent testing.
 * Covers various YubiKey models and configurations.
 *
 * Usage:
 * @code
 * auto device = TestDeviceFixture::createYubiKey5C();
 * auto nitrokey = TestDeviceFixture::createNitrokey3C();
 * @endcode
 */
class TestDeviceFixture {
public:
    /**
     * @brief Creates YubiKey 5C NFC device record
     * @param deviceId Device identifier (default: "test-yubikey-5c")
     * @param deviceName Friendly name (default: "YubiKey 5C NFC - Test")
     * @param requiresPassword Password protection flag (default: false)
     * @return Configured YubiKey 5C NFC record
     */
    static OathDatabase::DeviceRecord createYubiKey5C(
        const QString &deviceId = QStringLiteral("test-yubikey-5c"),
        const QString &deviceName = QStringLiteral("YubiKey 5C NFC - Test"),
        bool requiresPassword = false
    ) {
        OathDatabase::DeviceRecord record;
        record.deviceId = deviceId;
        record.deviceName = deviceName;
        record.requiresPassword = requiresPassword;
        record.lastSeen = QDateTime::currentDateTime();
        record.createdAt = QDateTime::currentDateTime().addDays(-30);
        record.firmwareVersion = Version(5, 4, 3);
        record.serialNumber = 12345678;
        record.formFactor = 1; // Keychain

        // YubiKey 5C NFC model code (0xSSVVPPFF)
        // SS=5 (Series 5), VV=01 (Standard), PP=08 (USB-C+NFC), FF=03 (basic capabilities)
        record.deviceModel = 0x05010803;

        return record;
    }

    /**
     * @brief Creates YubiKey 5 NFC (USB-A) device record
     * @param deviceId Device identifier
     * @param requiresPassword Password protection flag
     * @return Configured YubiKey 5 NFC record
     */
    static OathDatabase::DeviceRecord createYubiKey5NFC(
        const QString &deviceId = QStringLiteral("test-yubikey-5-nfc"),
        bool requiresPassword = false
    ) {
        auto record = createYubiKey5C(deviceId, QStringLiteral("YubiKey 5 NFC - Test"), requiresPassword);
        // YubiKey 5 NFC (USB-A) model code
        // SS=5, VV=01 (Standard), PP=09 (USB-A+NFC), FF=03
        record.deviceModel = 0x05010903;
        return record;
    }

    /**
     * @brief Creates YubiKey 5 Nano device record
     * @param deviceId Device identifier
     * @return Configured YubiKey 5 Nano record (no NFC)
     */
    static OathDatabase::DeviceRecord createYubiKey5Nano(
        const QString &deviceId = QStringLiteral("test-yubikey-nano")
    ) {
        auto record = createYubiKey5C(deviceId, QStringLiteral("YubiKey 5 Nano - Test"), false);
        // YubiKey 5 Nano model code
        // SS=5, VV=02 (Nano), PP=01 (USB-A only), FF=03
        record.deviceModel = 0x05020103;
        record.formFactor = 2; // Nano
        return record;
    }

    /**
     * @brief Creates Nitrokey 3C NFC device record
     * @param deviceId Device identifier (default: "test-nitrokey-3c")
     * @param deviceName Friendly name
     * @param requiresPassword Password protection flag
     * @return Configured Nitrokey 3C record
     */
    static OathDatabase::DeviceRecord createNitrokey3C(
        const QString &deviceId = QStringLiteral("test-nitrokey-3c"),
        const QString &deviceName = QStringLiteral("Nitrokey 3C NFC - Test"),
        bool requiresPassword = false
    ) {
        OathDatabase::DeviceRecord record;
        record.deviceId = deviceId;
        record.deviceName = deviceName;
        record.requiresPassword = requiresPassword;
        record.lastSeen = QDateTime::currentDateTime();
        record.createdAt = QDateTime::currentDateTime().addDays(-15);
        record.firmwareVersion = Version(1, 6, 0);
        record.serialNumber = 87654321;
        record.formFactor = 1; // Keychain

        // Nitrokey 3C NFC model code (0xGGVVPPFF)
        // GG=02 (NK3C gen), VV=00 (Standard), PP=0A (USB-C+NFC), FF=02
        record.deviceModel = 0x02000A02;

        return record;
    }

    /**
     * @brief Creates Nitrokey 3A Mini device record
     * @param deviceId Device identifier
     * @return Configured Nitrokey 3A Mini record (no NFC)
     */
    static OathDatabase::DeviceRecord createNitrokey3AMini(
        const QString &deviceId = QStringLiteral("test-nitrokey-3a-mini")
    ) {
        auto record = createNitrokey3C(deviceId, QStringLiteral("Nitrokey 3A Mini - Test"), false);
        // Nitrokey 3A Mini model code (0xGGVVPPFF)
        // GG=04 (NK3AM gen), VV=00 (Standard), PP=01 (USB-A), FF=02
        record.deviceModel = 0x04000102;
        record.firmwareVersion = Version(1, 5, 0);
        record.formFactor = 2; // Nano/Mini
        return record;
    }

    /**
     * @brief Creates password-protected device record
     * @param deviceId Device identifier
     * @return Device with requiresPassword = true
     */
    static OathDatabase::DeviceRecord createPasswordProtectedDevice(
        const QString &deviceId = QStringLiteral("test-password-device")
    ) {
        return createYubiKey5C(deviceId, QStringLiteral("Password Protected - Test"), true);
    }

    /**
     * @brief Creates device record with old firmware
     * @param deviceId Device identifier
     * @return YubiKey 4 with older firmware
     */
    static OathDatabase::DeviceRecord createLegacyDevice(
        const QString &deviceId = QStringLiteral("test-yubikey-4")
    ) {
        auto record = createYubiKey5C(deviceId, QStringLiteral("YubiKey 4 - Legacy"), false);
        record.firmwareVersion = Version(4, 3, 7);
        // YubiKey 4 model code (0xSSVVPPFF)
        // SS=4, VV=01 (Standard), PP=01 (USB-A), FF=03
        record.deviceModel = 0x04010103;
        record.createdAt = QDateTime::currentDateTime().addYears(-2);
        return record;
    }

    /**
     * @brief Creates list of diverse device records
     * @return List with YubiKey and Nitrokey devices, various models
     */
    static QList<OathDatabase::DeviceRecord> createDiverseDeviceSet() {
        return {
            createYubiKey5C(QStringLiteral("device1")),
            createYubiKey5NFC(QStringLiteral("device2")),
            createNitrokey3C(QStringLiteral("device3")),
            createYubiKey5Nano(QStringLiteral("device4")),
            createPasswordProtectedDevice(QStringLiteral("device5"))
        };
    }

    /**
     * @brief Creates device with specific serial number
     * @param serialNumber Serial number
     * @return Device with specified serial
     */
    static OathDatabase::DeviceRecord createDeviceWithSerial(
        quint32 serialNumber
    ) {
        auto record = createYubiKey5C();
        record.serialNumber = serialNumber;
        record.deviceId = QString::number(serialNumber);
        return record;
    }

    /**
     * @brief Creates recently seen device (last hour)
     * @param deviceId Device identifier
     * @return Device with recent lastSeen timestamp
     */
    static OathDatabase::DeviceRecord createRecentlySeenDevice(
        const QString &deviceId = QStringLiteral("test-recent")
    ) {
        auto record = createYubiKey5C(deviceId);
        record.lastSeen = QDateTime::currentDateTime().addSecs(-3600);
        return record;
    }

    /**
     * @brief Creates stale device (not seen in 30 days)
     * @param deviceId Device identifier
     * @return Device with old lastSeen timestamp
     */
    static OathDatabase::DeviceRecord createStaleDevice(
        const QString &deviceId = QStringLiteral("test-stale")
    ) {
        auto record = createYubiKey5C(deviceId);
        record.lastSeen = QDateTime::currentDateTime().addDays(-30);
        return record;
    }
};
