/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "shared/types/device_brand.h"
#include "shared/utils/version.h"

using namespace YubiKeyOath::Shared;

/**
 * @brief Unit tests for DeviceBrand detection and utility functions
 *
 * Tests the brand detection logic including:
 * - Reader name pattern matching
 * - Serial number + firmware heuristics
 * - Model string detection
 * - Utility functions (brandName, brandPrefix, isBrandSupported)
 */
class TestBrandDetection : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // detectBrand() tests - Strategy #1: Reader name pattern matching
    void testDetectBrand_NitrokeyReaderName_data();
    void testDetectBrand_NitrokeyReaderName();
    void testDetectBrand_YubicoReaderName_data();
    void testDetectBrand_YubicoReaderName();
    void testDetectBrand_YubiKeyReaderName_data();
    void testDetectBrand_YubiKeyReaderName();

    // detectBrand() tests - Strategy #2: Serial + firmware heuristics
    void testDetectBrand_NitrokeySerial();
    void testDetectBrand_YubiKeyNoSerial();

    // detectBrand() tests - Strategy #3: Firmware heuristics
    void testDetectBrand_YubiKey5Firmware();
    void testDetectBrand_YubiKey4AndNeoFirmware();

    // detectBrand() tests - Fallback behavior
    void testDetectBrand_UnknownFallback();

    // detectBrandFromModelString() tests
    void testDetectBrandFromModelString_Nitrokey();
    void testDetectBrandFromModelString_YubiKey();
    void testDetectBrandFromModelString_Fallback();

    // Utility function tests
    void testBrandName();
    void testBrandPrefix();
    void testIsBrandSupported();
};

// ========== detectBrand() - Reader Name Pattern Matching ==========

void TestBrandDetection::testDetectBrand_NitrokeyReaderName_data()
{
    QTest::addColumn<QString>("readerName");

    // Various Nitrokey reader name formats
    QTest::newRow("exact") << QStringLiteral("Nitrokey 3");
    QTest::newRow("with_variant") << QStringLiteral("Nitrokey 3C NFC");
    QTest::newRow("lowercase") << QStringLiteral("nitrokey 3a mini");
    QTest::newRow("uppercase") << QStringLiteral("NITROKEY 3");
    QTest::newRow("mixed_case") << QStringLiteral("NiTrOkEy 3");
    QTest::newRow("with_extra_info") << QStringLiteral("Nitrokey 3 CCID and U2F");
}

void TestBrandDetection::testDetectBrand_NitrokeyReaderName()
{
    QFETCH(QString, readerName);

    const DeviceBrand brand = detectBrand(readerName, Version(1, 0, 0), false);

    QCOMPARE(brand, DeviceBrand::Nitrokey);
}

void TestBrandDetection::testDetectBrand_YubicoReaderName_data()
{
    QTest::addColumn<QString>("readerName");

    // Yubico-branded reader names
    QTest::newRow("yubico_exact") << QStringLiteral("Yubico YubiKey");
    QTest::newRow("yubico_with_model") << QStringLiteral("Yubico YubiKey 5C NFC");
    QTest::newRow("yubico_lowercase") << QStringLiteral("yubico yubikey");
    QTest::newRow("yubico_uppercase") << QStringLiteral("YUBICO YUBIKEY");
    QTest::newRow("yubico_with_interfaces") << QStringLiteral("Yubico YubiKey OTP+FIDO+CCID");
}

void TestBrandDetection::testDetectBrand_YubicoReaderName()
{
    QFETCH(QString, readerName);

    const DeviceBrand brand = detectBrand(readerName, Version(5, 0, 0), false);

    QCOMPARE(brand, DeviceBrand::YubiKey);
}

void TestBrandDetection::testDetectBrand_YubiKeyReaderName_data()
{
    QTest::addColumn<QString>("readerName");

    // YubiKey-only reader names (without "Yubico")
    QTest::newRow("yubikey_exact") << QStringLiteral("YubiKey 5");
    QTest::newRow("yubikey_with_variant") << QStringLiteral("YubiKey 5 NFC");
    QTest::newRow("yubikey_lowercase") << QStringLiteral("yubikey 4");
    QTest::newRow("yubikey_uppercase") << QStringLiteral("YUBIKEY NEO");
}

void TestBrandDetection::testDetectBrand_YubiKeyReaderName()
{
    QFETCH(QString, readerName);

    const DeviceBrand brand = detectBrand(readerName, Version(5, 0, 0), false);

    QCOMPARE(brand, DeviceBrand::YubiKey);
}

// ========== detectBrand() - Serial + Firmware Heuristics ==========

void TestBrandDetection::testDetectBrand_NitrokeySerial()
{
    // Nitrokey 3 has TAG_SERIAL_NUMBER (0x8F) in SELECT response
    // and firmware 4.14.0+
    const Version nk3Firmware(4, 14, 0);
    const bool hasSerial = true;

    const DeviceBrand brand = detectBrand(QStringLiteral("Generic Reader"), nk3Firmware, hasSerial);

    QCOMPARE(brand, DeviceBrand::Nitrokey);
}

void TestBrandDetection::testDetectBrand_YubiKeyNoSerial()
{
    // YubiKey does NOT have TAG_SERIAL_NUMBER in SELECT response
    // (uses Management/PIV APIs instead)
    const Version yk5Firmware(5, 4, 3);
    const bool hasSerial = false;

    const DeviceBrand brand = detectBrand(QStringLiteral("Generic Reader"), yk5Firmware, hasSerial);

    QCOMPARE(brand, DeviceBrand::YubiKey);
}

// ========== detectBrand() - Firmware Heuristics ==========

void TestBrandDetection::testDetectBrand_YubiKey5Firmware()
{
    // YubiKey 5: firmware 5.x.x without TAG_SERIAL_NUMBER
    const Version yk5Firmware(5, 0, 0);
    const bool hasSerial = false;

    const DeviceBrand brand = detectBrand(QStringLiteral("Generic Reader"), yk5Firmware, hasSerial);

    QCOMPARE(brand, DeviceBrand::YubiKey);
}

void TestBrandDetection::testDetectBrand_YubiKey4AndNeoFirmware()
{
    // YubiKey 4/NEO: firmware < 5 without TAG_SERIAL_NUMBER
    const Version yk4Firmware(4, 3, 7);
    const Version ykNeoFirmware(3, 5, 0);
    const bool hasSerial = false;

    DeviceBrand brand4 = detectBrand(QStringLiteral("Generic Reader"), yk4Firmware, hasSerial);
    DeviceBrand brandNeo = detectBrand(QStringLiteral("Generic Reader"), ykNeoFirmware, hasSerial);

    QCOMPARE(brand4, DeviceBrand::YubiKey);
    QCOMPARE(brandNeo, DeviceBrand::YubiKey);
}

// ========== detectBrand() - Fallback Behavior ==========

void TestBrandDetection::testDetectBrand_UnknownFallback()
{
    // Unknown device: no reader name match, but firmware heuristics detect Nitrokey
    // Firmware 6.0.0 + hasSerial=true matches Nitrokey pattern (firmware >= 4.14.0)
    const Version unknownFirmware(6, 0, 0);
    const bool hasSerial = true;

    const DeviceBrand brand = detectBrand(QStringLiteral("Generic CCID Reader"), unknownFirmware, hasSerial);

    // Firmware + serial heuristics detect this as Nitrokey (strategy #2)
    QCOMPARE(brand, DeviceBrand::Nitrokey);
}

// ========== detectBrandFromModelString() ==========

void TestBrandDetection::testDetectBrandFromModelString_Nitrokey()
{
    // Nitrokey model strings
    QCOMPARE(detectBrandFromModelString(QStringLiteral("Nitrokey 3C NFC")), DeviceBrand::Nitrokey);
    QCOMPARE(detectBrandFromModelString(QStringLiteral("Nitrokey 3A")), DeviceBrand::Nitrokey);
    QCOMPARE(detectBrandFromModelString(QStringLiteral("nitrokey 3 mini")), DeviceBrand::Nitrokey);
    QCOMPARE(detectBrandFromModelString(QStringLiteral("NITROKEY 3")), DeviceBrand::Nitrokey);
}

void TestBrandDetection::testDetectBrandFromModelString_YubiKey()
{
    // YubiKey model strings
    QCOMPARE(detectBrandFromModelString(QStringLiteral("YubiKey 5C NFC")), DeviceBrand::YubiKey);
    QCOMPARE(detectBrandFromModelString(QStringLiteral("YubiKey 5 Nano")), DeviceBrand::YubiKey);
    QCOMPARE(detectBrandFromModelString(QStringLiteral("yubikey 4")), DeviceBrand::YubiKey);
    QCOMPARE(detectBrandFromModelString(QStringLiteral("YUBIKEY BIO")), DeviceBrand::YubiKey);
}

void TestBrandDetection::testDetectBrandFromModelString_Fallback()
{
    // Unknown/generic model strings - default to YubiKey
    QCOMPARE(detectBrandFromModelString(QStringLiteral("Generic OATH Device")), DeviceBrand::YubiKey);
    QCOMPARE(detectBrandFromModelString(QStringLiteral("Unknown Device")), DeviceBrand::YubiKey);
    QCOMPARE(detectBrandFromModelString(QString()), DeviceBrand::YubiKey);
}

// ========== Utility Functions ==========

void TestBrandDetection::testBrandName()
{
    // Note: brandName() returns i18n translated strings
    // We test that it returns non-empty strings
    QVERIFY(!brandName(DeviceBrand::YubiKey).isEmpty());
    QVERIFY(!brandName(DeviceBrand::Nitrokey).isEmpty());
    QVERIFY(!brandName(DeviceBrand::Unknown).isEmpty());
}

void TestBrandDetection::testBrandPrefix()
{
    QCOMPARE(brandPrefix(DeviceBrand::YubiKey), QStringLiteral("yubikey"));
    QCOMPARE(brandPrefix(DeviceBrand::Nitrokey), QStringLiteral("nitrokey"));
    QCOMPARE(brandPrefix(DeviceBrand::Unknown), QStringLiteral("oath-device"));
}

void TestBrandDetection::testIsBrandSupported()
{
    QVERIFY(isBrandSupported(DeviceBrand::YubiKey));
    QVERIFY(isBrandSupported(DeviceBrand::Nitrokey));
    QVERIFY(!isBrandSupported(DeviceBrand::Unknown));
}

QTEST_MAIN(TestBrandDetection)
#include "test_brand_detection.moc"
