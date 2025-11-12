/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "shared/types/device_capabilities.h"
#include "shared/types/device_brand.h"
#include "shared/utils/version.h"

using namespace YubiKeyOath::Shared;

/**
 * @brief Unit tests for DeviceCapabilities struct
 *
 * Tests brand-specific capability detection including:
 * - YubiKey protocol characteristics
 * - Nitrokey protocol characteristics
 * - Unknown device fallback behavior
 * - Touch requirement status word detection
 */
class TestDeviceCapabilities : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // YubiKey capabilities tests
    void testDetectCapabilities_YubiKey();
    void testYubiKey_SupportsCalculateAll();
    void testYubiKey_NoSelectSerial();
    void testYubiKey_DoesNotPreferList();
    void testYubiKey_TouchStatusWord();

    // Nitrokey capabilities tests
    void testDetectCapabilities_Nitrokey();
    void testNitrokey_NoCalculateAll();
    void testNitrokey_HasSelectSerial();
    void testNitrokey_PrefersList();
    void testNitrokey_TouchStatusWord();

    // Unknown device fallback tests
    void testDetectCapabilities_Unknown();
    void testUnknown_YubiKeyCompatibleDefaults();

    // Touch requirement detection tests
    void testIsTouchRequired_YubiKeyStatusWord();
    void testIsTouchRequired_NitrokeyStatusWord();
    void testIsTouchRequired_CrossBrandCompatibility();
    void testIsTouchRequired_OtherStatusWords();

    // Firmware version independence tests
    void testFirmwareVersionIndependence();
};

// ========== YubiKey Capabilities ==========

void TestDeviceCapabilities::testDetectCapabilities_YubiKey()
{
    const Version firmware(5, 4, 3);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::YubiKey, firmware);

    // YubiKey-specific defaults
    QVERIFY(caps.supportsCalculateAll);
    QVERIFY(!caps.hasSelectSerial);
    QVERIFY(!caps.preferList);
    QCOMPARE(caps.touchRequiredStatusWord, static_cast<quint16>(0x6985));
}

void TestDeviceCapabilities::testYubiKey_SupportsCalculateAll()
{
    // All YubiKeys support CALCULATE_ALL (INS=0xA4)
    const Version firmware(5, 0, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::YubiKey, firmware);

    QVERIFY(caps.supportsCalculateAll);
}

void TestDeviceCapabilities::testYubiKey_NoSelectSerial()
{
    // YubiKey uses Management/PIV APIs for serial, not SELECT response
    const Version firmware(5, 0, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::YubiKey, firmware);

    QVERIFY(!caps.hasSelectSerial);
}

void TestDeviceCapabilities::testYubiKey_DoesNotPreferList()
{
    // YubiKey uses CALCULATE_ALL to avoid LIST spurious touch errors
    const Version firmware(5, 0, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::YubiKey, firmware);

    QVERIFY(!caps.preferList);
}

void TestDeviceCapabilities::testYubiKey_TouchStatusWord()
{
    // YubiKey uses 0x6985 (ConditionsNotSatisfied) for touch requirement
    const Version firmware(5, 0, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::YubiKey, firmware);

    QCOMPARE(caps.touchRequiredStatusWord, static_cast<quint16>(0x6985));
}

// ========== Nitrokey Capabilities ==========

void TestDeviceCapabilities::testDetectCapabilities_Nitrokey()
{
    const Version firmware(1, 6, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::Nitrokey, firmware);

    // Nitrokey-specific defaults
    QVERIFY(!caps.supportsCalculateAll);  // Feature-gated, test at runtime
    QVERIFY(caps.hasSelectSerial);
    QVERIFY(caps.preferList);
    QCOMPARE(caps.touchRequiredStatusWord, static_cast<quint16>(0x6982));
}

void TestDeviceCapabilities::testNitrokey_NoCalculateAll()
{
    // Nitrokey CALCULATE_ALL is feature-gated, must be tested at runtime
    const Version firmware(1, 6, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::Nitrokey, firmware);

    QVERIFY(!caps.supportsCalculateAll);
}

void TestDeviceCapabilities::testNitrokey_HasSelectSerial()
{
    // Nitrokey includes TAG_SERIAL_NUMBER (0x8F) in SELECT response
    const Version firmware(1, 6, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::Nitrokey, firmware);

    QVERIFY(caps.hasSelectSerial);
}

void TestDeviceCapabilities::testNitrokey_PrefersList()
{
    // Nitrokey LIST works reliably, CALCULATE_ALL may be unavailable
    const Version firmware(1, 6, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::Nitrokey, firmware);

    QVERIFY(caps.preferList);
}

void TestDeviceCapabilities::testNitrokey_TouchStatusWord()
{
    // Nitrokey uses 0x6982 (SecurityStatusNotSatisfied) for touch requirement
    const Version firmware(1, 6, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::Nitrokey, firmware);

    QCOMPARE(caps.touchRequiredStatusWord, static_cast<quint16>(0x6982));
}

// ========== Unknown Device Fallback ==========

void TestDeviceCapabilities::testDetectCapabilities_Unknown()
{
    const Version firmware(1, 0, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::Unknown, firmware);

    // Conservative defaults for unknown devices
    QVERIFY(caps.supportsCalculateAll);
    QVERIFY(!caps.hasSelectSerial);
    QVERIFY(!caps.preferList);
    QCOMPARE(caps.touchRequiredStatusWord, static_cast<quint16>(0x6985));
}

void TestDeviceCapabilities::testUnknown_YubiKeyCompatibleDefaults()
{
    // Unknown devices should assume YubiKey-compatible behavior
    const Version firmware(1, 0, 0);

    DeviceCapabilities unknownCaps = DeviceCapabilities::detect(DeviceBrand::Unknown, firmware);
    DeviceCapabilities yubikeyCaps = DeviceCapabilities::detect(DeviceBrand::YubiKey, firmware);

    QCOMPARE(unknownCaps.supportsCalculateAll, yubikeyCaps.supportsCalculateAll);
    QCOMPARE(unknownCaps.hasSelectSerial, yubikeyCaps.hasSelectSerial);
    QCOMPARE(unknownCaps.preferList, yubikeyCaps.preferList);
    QCOMPARE(unknownCaps.touchRequiredStatusWord, yubikeyCaps.touchRequiredStatusWord);
}

// ========== Touch Requirement Detection ==========

void TestDeviceCapabilities::testIsTouchRequired_YubiKeyStatusWord()
{
    const Version firmware(5, 0, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::YubiKey, firmware);

    // Test YubiKey's 0x6985 status word
    QVERIFY(caps.isTouchRequired(0x6985));
    QVERIFY(!caps.isTouchRequired(0x9000));  // Success
    QVERIFY(!caps.isTouchRequired(0x6A80));  // Incorrect parameters
}

void TestDeviceCapabilities::testIsTouchRequired_NitrokeyStatusWord()
{
    const Version firmware(1, 6, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::Nitrokey, firmware);

    // Test Nitrokey's 0x6982 status word
    QVERIFY(caps.isTouchRequired(0x6982));
    QVERIFY(!caps.isTouchRequired(0x9000));  // Success
    QVERIFY(!caps.isTouchRequired(0x6A86));  // Incorrect P1/P2
}

void TestDeviceCapabilities::testIsTouchRequired_CrossBrandCompatibility()
{
    // isTouchRequired() should recognize BOTH brand status words
    // This allows client code to check touch requirement without brand awareness
    const Version firmware(1, 0, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::YubiKey, firmware);

    // YubiKey capabilities should recognize both status words
    QVERIFY(caps.isTouchRequired(0x6985));  // YubiKey's touch code
    QVERIFY(caps.isTouchRequired(0x6982));  // Nitrokey's touch code (cross-compatible)
}

void TestDeviceCapabilities::testIsTouchRequired_OtherStatusWords()
{
    const Version firmware(1, 0, 0);
    DeviceCapabilities caps = DeviceCapabilities::detect(DeviceBrand::YubiKey, firmware);

    // Common APDU status words that are NOT touch requirements
    QVERIFY(!caps.isTouchRequired(0x9000));  // Success
    QVERIFY(!caps.isTouchRequired(0x6300));  // Verification failed
    QVERIFY(!caps.isTouchRequired(0x6700));  // Wrong length
    QVERIFY(!caps.isTouchRequired(0x6A80));  // Incorrect parameters
    QVERIFY(!caps.isTouchRequired(0x6A86));  // Incorrect P1/P2
    QVERIFY(!caps.isTouchRequired(0x6D00));  // INS not supported
    QVERIFY(!caps.isTouchRequired(0x6E00));  // CLA not supported
}

// ========== Firmware Version Independence ==========

void TestDeviceCapabilities::testFirmwareVersionIndependence()
{
    // Capabilities should be determined by brand, not firmware version
    // (firmware parameter is currently unused but reserved for future use)

    const Version oldFirmware(1, 0, 0);
    const Version newFirmware(10, 0, 0);

    // YubiKey capabilities should be same regardless of firmware
    DeviceCapabilities yubikeyOld = DeviceCapabilities::detect(DeviceBrand::YubiKey, oldFirmware);
    DeviceCapabilities yubikeyNew = DeviceCapabilities::detect(DeviceBrand::YubiKey, newFirmware);

    QCOMPARE(yubikeyOld.supportsCalculateAll, yubikeyNew.supportsCalculateAll);
    QCOMPARE(yubikeyOld.hasSelectSerial, yubikeyNew.hasSelectSerial);
    QCOMPARE(yubikeyOld.preferList, yubikeyNew.preferList);
    QCOMPARE(yubikeyOld.touchRequiredStatusWord, yubikeyNew.touchRequiredStatusWord);

    // Nitrokey capabilities should be same regardless of firmware
    DeviceCapabilities nitrokeyOld = DeviceCapabilities::detect(DeviceBrand::Nitrokey, oldFirmware);
    DeviceCapabilities nitrokeyNew = DeviceCapabilities::detect(DeviceBrand::Nitrokey, newFirmware);

    QCOMPARE(nitrokeyOld.supportsCalculateAll, nitrokeyNew.supportsCalculateAll);
    QCOMPARE(nitrokeyOld.hasSelectSerial, nitrokeyNew.hasSelectSerial);
    QCOMPARE(nitrokeyOld.preferList, nitrokeyNew.preferList);
    QCOMPARE(nitrokeyOld.touchRequiredStatusWord, nitrokeyNew.touchRequiredStatusWord);
}

QTEST_MAIN(TestDeviceCapabilities)
#include "test_device_capabilities.moc"
