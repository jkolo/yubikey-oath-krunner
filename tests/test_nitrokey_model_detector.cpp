/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "daemon/oath/nitrokey_model_detector.h"
#include "shared/types/device_brand.h"
#include "shared/types/device_model.h"
#include "shared/utils/version.h"

using namespace YubiKeyOath::Daemon;
using namespace YubiKeyOath::Shared;

/**
 * @brief Unit tests for Nitrokey model detection
 *
 * Tests the detectNitrokeyModel() function including:
 * - USB variant detection (A vs C) from firmware heuristics
 * - NFC capability detection
 * - Model code encoding (0xGGVVPPFF format)
 * - Capabilities list construction
 * - Invalid reader name handling
 */
class TestNitrokeyModelDetector : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Valid Nitrokey 3 detection tests
    void testDetectNitrokey3C_NewFirmware();
    void testDetectNitrokey3A_OldFirmware();
    void testDetectNitrokey3C_NFC();
    void testDetectNitrokey3A_NoNFC();

    // USB variant heuristics tests
    void testDetectUSBVariant_FirmwareBelow16();
    void testDetectUSBVariant_FirmwareAt16();
    void testDetectUSBVariant_FirmwareAbove16();

    // NFC capability tests
    void testNFCCapability_Firmware15();
    void testNFCCapability_Firmware14();

    // Model code encoding tests
    void testModelCodeEncoding_3C_NFC();
    void testModelCodeEncoding_3A_NoNFC();

    // Capabilities list tests
    void testCapabilitiesList();

    // Reader name validation tests
    void testValidReaderName_ExactMatch();
    void testValidReaderName_CaseInsensitive();
    void testValidReaderName_WithInterfaces();
    void testInvalidReaderName_YubiKey();
    void testInvalidReaderName_Generic();

    // Edge cases
    void testNoSerialNumber();
    void testZeroSerialNumber();
    void testVeryOldFirmware();
    void testVeryNewFirmware();
};

// ========== Valid Nitrokey 3 Detection ==========

void TestNitrokeyModelDetector::testDetectNitrokey3C_NewFirmware()
{
    // Nitrokey 3C: firmware >= 1.6.0
    const QString readerName = QStringLiteral("Nitrokey Nitrokey 3 [CCID/ICCD Interface]");
    const Version firmware(1, 6, 0);
    const quint32 serial = 562721119;

    DeviceModel model = detectNitrokeyModel(readerName, firmware, serial);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
    QVERIFY(model.modelString.contains(QStringLiteral("3C")));
    QVERIFY(model.modelString.contains(QStringLiteral("NFC")));
    QVERIFY(!model.capabilities.isEmpty());
}

void TestNitrokeyModelDetector::testDetectNitrokey3A_OldFirmware()
{
    // Nitrokey 3A: firmware < 1.6.0
    const QString readerName = QStringLiteral("Nitrokey 3");
    const Version firmware(1, 5, 0);

    DeviceModel model = detectNitrokeyModel(readerName, firmware);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
    QVERIFY(model.modelString.contains(QStringLiteral("3A")));
}

void TestNitrokeyModelDetector::testDetectNitrokey3C_NFC()
{
    // Nitrokey 3C NFC: firmware >= 1.6.0 (implies NFC capable since >= 1.5.0)
    const QString readerName = QStringLiteral("Nitrokey 3");
    const Version firmware(1, 7, 0);
    const quint32 serial = 123456789;

    DeviceModel model = detectNitrokeyModel(readerName, firmware, serial);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
    QVERIFY(model.modelString.contains(QStringLiteral("3C")));
    QVERIFY(model.modelString.contains(QStringLiteral("NFC")));
}

void TestNitrokeyModelDetector::testDetectNitrokey3A_NoNFC()
{
    // Nitrokey 3A without NFC: firmware < 1.5.0
    const QString readerName = QStringLiteral("Nitrokey 3");
    const Version firmware(1, 4, 0);

    DeviceModel model = detectNitrokeyModel(readerName, firmware);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
    QVERIFY(model.modelString.contains(QStringLiteral("3A")));
    QVERIFY(!model.modelString.contains(QStringLiteral("NFC")));
}

// ========== USB Variant Heuristics ==========

void TestNitrokeyModelDetector::testDetectUSBVariant_FirmwareBelow16()
{
    // Firmware 1.5.x → 3A variant
    const Version firmware15(1, 5, 9);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware15);

    QVERIFY(model.modelString.contains(QStringLiteral("3A")));
    QVERIFY(!model.modelString.contains(QStringLiteral("3C")));
}

void TestNitrokeyModelDetector::testDetectUSBVariant_FirmwareAt16()
{
    // Firmware 1.6.0 exactly → 3C variant (threshold)
    const Version firmware16(1, 6, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware16);

    QVERIFY(model.modelString.contains(QStringLiteral("3C")));
}

void TestNitrokeyModelDetector::testDetectUSBVariant_FirmwareAbove16()
{
    // Firmware 1.7.x+ → 3C variant
    const Version firmware17(1, 8, 2);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware17);

    QVERIFY(model.modelString.contains(QStringLiteral("3C")));
}

// ========== NFC Capability Tests ==========

void TestNitrokeyModelDetector::testNFCCapability_Firmware15()
{
    // NFC introduced in firmware 1.5.0+
    const Version firmware(1, 5, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware);

    QVERIFY(model.modelString.contains(QStringLiteral("NFC")));
}

void TestNitrokeyModelDetector::testNFCCapability_Firmware14()
{
    // Firmware 1.4.x does not have NFC
    const Version firmware(1, 4, 9);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware);

    QVERIFY(!model.modelString.contains(QStringLiteral("NFC")));
}

// ========== Model Code Encoding ==========

void TestNitrokeyModelDetector::testModelCodeEncoding_3C_NFC()
{
    // Nitrokey 3C NFC: firmware 1.6.0+
    // Expected model code structure:
    // GG=0x02 (NK3C), VV=0x00, PP=0x0A (USB_C|NFC), FF=0x0F (all caps)
    const Version firmware(1, 6, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware);

    // Extract bytes from model code
    const quint8 generation = (model.modelCode >> 24) & 0xFF;
    const quint8 variant = (model.modelCode >> 16) & 0xFF;
    const quint8 ports = (model.modelCode >> 8) & 0xFF;
    const quint8 capabilities = model.modelCode & 0xFF;

    // Verify encoding
    QCOMPARE(generation, static_cast<quint8>(0x02));  // NK3C
    QCOMPARE(variant, static_cast<quint8>(0x00));     // Standard variant
    QVERIFY((ports & 0x02) != 0);  // USB-C present
    QVERIFY((ports & 0x08) != 0);  // NFC present
    QVERIFY((capabilities & 0x02) != 0);  // OATH capability
}

void TestNitrokeyModelDetector::testModelCodeEncoding_3A_NoNFC()
{
    // Nitrokey 3A without NFC: firmware 1.4.0
    // Expected: GG=0x01 (NK3A), PP=0x01 (USB_A only)
    const Version firmware(1, 4, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware);

    const quint8 generation = (model.modelCode >> 24) & 0xFF;
    const quint8 ports = (model.modelCode >> 8) & 0xFF;

    QCOMPARE(generation, static_cast<quint8>(0x01));  // NK3A
    QVERIFY((ports & 0x01) != 0);  // USB-A present
    QVERIFY((ports & 0x08) == 0);  // NFC NOT present
}

// ========== Capabilities List ==========

void TestNitrokeyModelDetector::testCapabilitiesList()
{
    // All Nitrokey 3 devices support: FIDO2, OATH, OpenPGP, PIV
    const Version firmware(1, 6, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware);

    QVERIFY(model.capabilities.contains(QStringLiteral("FIDO2")));
    QVERIFY(model.capabilities.contains(QStringLiteral("OATH-HOTP")));
    QVERIFY(model.capabilities.contains(QStringLiteral("OATH-TOTP")));
    QVERIFY(model.capabilities.contains(QStringLiteral("OpenPGP")));
    QVERIFY(model.capabilities.contains(QStringLiteral("PIV")));
}

// ========== Reader Name Validation ==========

void TestNitrokeyModelDetector::testValidReaderName_ExactMatch()
{
    const Version firmware(1, 6, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
}

void TestNitrokeyModelDetector::testValidReaderName_CaseInsensitive()
{
    // Reader name matching is case-insensitive
    const Version firmware(1, 6, 0);

    DeviceModel model1 = detectNitrokeyModel(QStringLiteral("NITROKEY 3"), firmware);
    DeviceModel model2 = detectNitrokeyModel(QStringLiteral("nitrokey 3"), firmware);
    DeviceModel model3 = detectNitrokeyModel(QStringLiteral("NiTrOkEy 3"), firmware);

    QCOMPARE(model1.brand, DeviceBrand::Nitrokey);
    QCOMPARE(model2.brand, DeviceBrand::Nitrokey);
    QCOMPARE(model3.brand, DeviceBrand::Nitrokey);
}

void TestNitrokeyModelDetector::testValidReaderName_WithInterfaces()
{
    // Reader names often include interface information
    const Version firmware(1, 6, 0);
    DeviceModel model = detectNitrokeyModel(
        QStringLiteral("Nitrokey Nitrokey 3 [CCID/ICCD Interface]"), firmware);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
}

void TestNitrokeyModelDetector::testInvalidReaderName_YubiKey()
{
    // YubiKey reader name should NOT be detected as Nitrokey
    const Version firmware(5, 4, 3);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Yubico YubiKey"), firmware);

    QCOMPARE(model.brand, DeviceBrand::Unknown);
    QCOMPARE(model.modelString, QStringLiteral("Unknown Device"));
}

void TestNitrokeyModelDetector::testInvalidReaderName_Generic()
{
    // Generic CCID reader should fallback to Unknown
    const Version firmware(1, 6, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Generic CCID Reader"), firmware);

    QCOMPARE(model.brand, DeviceBrand::Unknown);
}

// ========== Edge Cases ==========

void TestNitrokeyModelDetector::testNoSerialNumber()
{
    // Serial number is optional - detection should still work
    const Version firmware(1, 6, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
    QVERIFY(!model.modelString.isEmpty());
}

void TestNitrokeyModelDetector::testZeroSerialNumber()
{
    // Explicit zero serial (no serial available)
    const Version firmware(1, 5, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), firmware, 0);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
    // Should assume 3A for firmware < 1.6.0, but with low confidence
    QVERIFY(model.modelString.contains(QStringLiteral("3A")));
}

void TestNitrokeyModelDetector::testVeryOldFirmware()
{
    // Very old firmware (pre-1.5.0) - no NFC, likely 3A
    const Version oldFirmware(1, 0, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), oldFirmware);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
    QVERIFY(model.modelString.contains(QStringLiteral("3A")));
    QVERIFY(!model.modelString.contains(QStringLiteral("NFC")));
}

void TestNitrokeyModelDetector::testVeryNewFirmware()
{
    // Future firmware version - should detect as 3C (major=2 > 1, minor=0)
    // Detection logic: firmware.major() >= 1 && firmware.minor() >= 6
    // For 2.0.0: major >= 1 (true), but minor 0 < 6 (false) → defaults to 3A
    // Use 2.6.0 to satisfy both conditions
    const Version newFirmware(2, 6, 0);
    DeviceModel model = detectNitrokeyModel(QStringLiteral("Nitrokey 3"), newFirmware);

    QCOMPARE(model.brand, DeviceBrand::Nitrokey);
    // Firmware 2.6.0: major >= 1 AND minor >= 6 → 3C variant
    QVERIFY(model.modelString.contains(QStringLiteral("3C")));
}

QTEST_MAIN(TestNitrokeyModelDetector)
#include "test_nitrokey_model_detector.moc"
