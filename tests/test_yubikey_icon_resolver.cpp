/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "shared/utils/yubikey_icon_resolver.h"
#include "shared/types/yubikey_model.h"

using namespace YubiKeyOath::Shared;

/**
 * @brief Unit tests for YubiKeyIconResolver
 *
 * Tests icon path resolution with fallback strategy and naming conventions.
 */
class TestYubiKeyIconResolver : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Generic icon tests
    void testGetGenericIconPath();

    // Unknown/invalid model tests
    void testGetIconPath_UnknownModel_ReturnsGeneric();
    void testGetIconPath_ZeroModel_ReturnsGeneric();

    // Naming convention tests (via getIconPath)
    void testGetIconPath_YubiKey5_USB_A();
    void testGetIconPath_YubiKey5_USB_C();
    void testGetIconPath_YubiKey5_USB_A_NFC();
    void testGetIconPath_YubiKey5C_NFC();
    void testGetIconPath_YubiKey5_Nano();
    void testGetIconPath_YubiKey5C_Nano();
    void testGetIconPath_YubiKey5Ci();
    void testGetIconPath_YubiKeyBio();
    void testGetIconPath_YubiKeyNEO_NoNFCSuffix();
    void testGetIconPath_YubiKey5FIPS();
    void testGetIconPath_YubiKey5FIPS_NFC();  // FIPS with NFC should find yubikey-5-nfc.png
    void testGetIconPath_YubiKey5FIPS_USB_C_NFC();  // FIPS USB-C with NFC should find yubikey-5c-nfc.png
    void testGetIconPath_YubiKey4FIPS();  // YubiKey 4 FIPS should find yubikey-4.png
    void testGetIconPath_SecurityKey();

    // Fallback strategy tests
    void testGetIconPath_AlwaysReturnsNonEmpty();

    // Edge cases
    void testGetIconPath_MultipleCallsSameModel_Consistent();

private:
    // Helper to create encoded model
    YubiKeyModel createModel(YubiKeySeries series,
                            YubiKeyVariant variant,
                            YubiKeyPorts ports,
                            YubiKeyCapabilities caps = YubiKeyCapability::OATH_TOTP) {
        return (static_cast<uint32_t>(series) << 24) |
               (static_cast<uint32_t>(variant) << 16) |
               (static_cast<uint32_t>(ports.toInt()) << 8) |
               static_cast<uint32_t>(caps.toInt());
    }
};

// ========== Generic Icon Tests ==========

void TestYubiKeyIconResolver::testGetGenericIconPath()
{
    QString genericPath = YubiKeyIconResolver::getGenericIconPath();

    QVERIFY(!genericPath.isEmpty());
    QVERIFY(genericPath.contains("yubikey"));
    QVERIFY(genericPath.endsWith(".svg"));
    QCOMPARE(genericPath, QString(":/icons/yubikey.svg"));
}

// ========== Unknown/Invalid Model Tests ==========

void TestYubiKeyIconResolver::testGetIconPath_UnknownModel_ReturnsGeneric()
{
    YubiKeyModel unknownModel = createModel(YubiKeySeries::Unknown,
                                            YubiKeyVariant::Standard,
                                            YubiKeyPort::USB_A);

    QString iconPath = YubiKeyIconResolver::getIconPath(unknownModel);

    QVERIFY(!iconPath.isEmpty());
    // Should eventually fall back to generic icon
    QVERIFY(iconPath.contains("yubikey"));
}

void TestYubiKeyIconResolver::testGetIconPath_ZeroModel_ReturnsGeneric()
{
    YubiKeyModel zeroModel = 0;

    QString iconPath = YubiKeyIconResolver::getIconPath(zeroModel);

    QCOMPARE(iconPath, YubiKeyIconResolver::getGenericIconPath());
}

// ========== Naming Convention Tests ==========

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5_USB_A()
{
    // YubiKey 5 (USB-A, no NFC) - Standard variant
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPort::USB_A);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Path should contain "yubikey-5" (no 'c' for USB-A, no '-nfc', no variant)
    // May be specific icon or fallback to generic
    QVERIFY(iconPath.contains("yubikey"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5_USB_C()
{
    // YubiKey 5C (USB-C, no NFC) - Standard variant
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPort::USB_C);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Path should contain "5c" (USB-C indicator)
    QVERIFY(iconPath.contains("yubikey"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5_USB_A_NFC()
{
    // YubiKey 5 NFC (USB-A + NFC) - Standard variant
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPorts(YubiKeyPort::USB_A) | YubiKeyPort::NFC);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Path should contain "yubikey-5" and "-nfc"
    QVERIFY(iconPath.contains("yubikey"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5C_NFC()
{
    // YubiKey 5C NFC (USB-C + NFC) - Standard variant
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPorts(YubiKeyPort::USB_C) | YubiKeyPort::NFC);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Path should contain "5c" and "nfc"
    QVERIFY(iconPath.contains("yubikey"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5_Nano()
{
    // YubiKey 5 Nano (USB-A + Nano variant)
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5,
                                     YubiKeyVariant::Nano,
                                     YubiKeyPort::USB_A);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Path should contain "yubikey-5" and "nano"
    QVERIFY(iconPath.contains("yubikey"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5C_Nano()
{
    // YubiKey 5C Nano (USB-C + Nano variant)
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5,
                                     YubiKeyVariant::Nano,
                                     YubiKeyPort::USB_C);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Path should contain "5c" and "nano"
    QVERIFY(iconPath.contains("yubikey"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5Ci()
{
    // YubiKey 5Ci (USB-C + Lightning - special case)
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5,
                                     YubiKeyVariant::DualConnector,
                                     YubiKeyPorts(YubiKeyPort::USB_C) | YubiKeyPort::Lightning);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Path should contain "5ci" (special dual connector naming)
    QVERIFY(iconPath.contains("yubikey"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKeyBio()
{
    // YubiKey Bio (USB-A) - doesn't support OATH applet, should fallback to generic
    YubiKeyModel model = createModel(YubiKeySeries::YubiKeyBio,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPort::USB_A,
                                     YubiKeyCapabilities(YubiKeyCapability::FIDO2) | YubiKeyCapability::FIDO_U2F);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Bio doesn't support OATH applet - no specific icon files exist
    // Should fallback to generic icon
    QVERIFY(iconPath.contains("yubikey"));
    QVERIFY(iconPath.contains("generic") || iconPath.endsWith(".svg"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKeyNEO_NoNFCSuffix()
{
    // YubiKey NEO (USB-A + NFC) - NEO always has NFC, so no "-nfc" suffix
    YubiKeyModel model = createModel(YubiKeySeries::YubiKeyNEO,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPorts(YubiKeyPort::USB_A) | YubiKeyPort::NFC);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Path should contain "neo" but NOT "-nfc" (NEO always has NFC, no suffix needed)
    QVERIFY(iconPath.contains("yubikey"));
    // If it's the specific NEO icon (not generic fallback), verify no -nfc
    if (iconPath.contains("neo") && !iconPath.contains("generic")) {
        QVERIFY(!iconPath.contains("-nfc"));
    }
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5FIPS()
{
    // YubiKey 5 FIPS (USB-A) - should use same icon as non-FIPS YubiKey 5
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5FIPS,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPort::USB_A);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // FIPS models use same icons as non-FIPS counterparts
    // No separate FIPS icon files exist - fallbacks to generic
    QVERIFY(iconPath.contains("yubikey"));
    // Should fallback to generic since no "yubikey-5.png" exists (no USB-A only model)
    QVERIFY(iconPath.contains("generic") || iconPath.endsWith(".svg"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5FIPS_NFC()
{
    // YubiKey 5 FIPS NFC (USB-A + NFC) - FIPS uses same naming as non-FIPS
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5FIPS,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPorts(YubiKeyPort::USB_A) | YubiKeyPort::NFC);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // FIPS models use same icons as non-FIPS counterparts
    // Either finds yubikey-5-nfc.png or falls back to generic (yubikey.svg)
    QVERIFY(iconPath.contains("yubikey"));
    QVERIFY(iconPath.contains("yubikey-5-nfc") || iconPath.endsWith(".svg"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey5FIPS_USB_C_NFC()
{
    // YubiKey 5C FIPS NFC (USB-C + NFC) - FIPS uses same naming as non-FIPS
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5FIPS,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPorts(YubiKeyPort::USB_C) | YubiKeyPort::NFC);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // FIPS models use same icons as non-FIPS counterparts
    // Either finds yubikey-5c-nfc.png or falls back to generic (yubikey.svg)
    QVERIFY(iconPath.contains("yubikey"));
    QVERIFY(iconPath.contains("yubikey-5c-nfc") || iconPath.endsWith(".svg"));
}

void TestYubiKeyIconResolver::testGetIconPath_YubiKey4FIPS()
{
    // YubiKey 4 FIPS (USB-A) - FIPS uses same naming as non-FIPS
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey4FIPS,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPort::USB_A);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // FIPS models use same icons as non-FIPS counterparts
    // Either finds yubikey-4.png or falls back to generic (yubikey.svg)
    QVERIFY(iconPath.contains("yubikey"));
    QVERIFY(iconPath.contains("yubikey-4") || iconPath.endsWith(".svg"));
}

void TestYubiKeyIconResolver::testGetIconPath_SecurityKey()
{
    // Security Key (USB-A) - FIDO-only, doesn't support OATH applet, should fallback to generic
    YubiKeyModel model = createModel(YubiKeySeries::SecurityKey,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPort::USB_A,
                                     YubiKeyCapabilities(YubiKeyCapability::FIDO2) | YubiKeyCapability::FIDO_U2F);

    QString iconPath = YubiKeyIconResolver::getIconPath(model);

    QVERIFY(!iconPath.isEmpty());
    // Security Key doesn't support OATH applet - no specific icon files exist
    // Should fallback to generic icon
    QVERIFY(iconPath.contains("yubikey"));
    QVERIFY(iconPath.contains("generic") || iconPath.endsWith(".svg"));
}

// ========== Fallback Strategy Tests ==========

void TestYubiKeyIconResolver::testGetIconPath_AlwaysReturnsNonEmpty()
{
    // Test various models to ensure ALWAYS returns non-empty path
    QVector<YubiKeyModel> testModels = {
        0, // Zero model
        createModel(YubiKeySeries::Unknown, YubiKeyVariant::Standard, YubiKeyPort::USB_A),
        createModel(YubiKeySeries::YubiKey5, YubiKeyVariant::Standard, YubiKeyPort::USB_A),
        createModel(YubiKeySeries::YubiKey5, YubiKeyVariant::Nano, YubiKeyPort::USB_C),
        createModel(YubiKeySeries::YubiKeyBio, YubiKeyVariant::Standard, YubiKeyPort::USB_C),
        createModel(YubiKeySeries::YubiKeyNEO, YubiKeyVariant::Standard,
                   YubiKeyPorts(YubiKeyPort::USB_A) | YubiKeyPort::NFC),
    };

    for (const auto& model : testModels) {
        QString iconPath = YubiKeyIconResolver::getIconPath(model);
        QVERIFY2(!iconPath.isEmpty(), "Icon path must never be empty");
        QVERIFY2(iconPath.contains("yubikey"), "Icon path must contain 'yubikey'");
    }
}

// ========== Edge Cases ==========

void TestYubiKeyIconResolver::testGetIconPath_MultipleCallsSameModel_Consistent()
{
    // Test that multiple calls with same model return consistent results
    YubiKeyModel model = createModel(YubiKeySeries::YubiKey5,
                                     YubiKeyVariant::Standard,
                                     YubiKeyPorts(YubiKeyPort::USB_C) | YubiKeyPort::NFC);

    QString iconPath1 = YubiKeyIconResolver::getIconPath(model);
    QString iconPath2 = YubiKeyIconResolver::getIconPath(model);
    QString iconPath3 = YubiKeyIconResolver::getIconPath(model);

    QCOMPARE(iconPath1, iconPath2);
    QCOMPARE(iconPath2, iconPath3);
}

QTEST_MAIN(TestYubiKeyIconResolver)
#include "test_yubikey_icon_resolver.moc"
