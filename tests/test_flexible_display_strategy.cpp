/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "krunner/formatting/display_strategies/flexible_display_strategy.h"
#include "shared/types/oath_credential.h"

using namespace KRunner::YubiKey;

/**
 * @brief Unit tests for FlexibleDisplayStrategy
 *
 * Tests all combinations of display flags for flexible credential formatting.
 */
class TestFlexibleDisplayStrategy : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Basic formatting tests
    void testFormat_OnlyIssuer();
    void testFormat_IssuerWithUsername();
    void testFormat_IssuerWithCode();
    void testFormat_IssuerWithDeviceName();
    void testFormat_AllOptions();

    // Username flag tests
    void testFormat_Username_Enabled();
    void testFormat_Username_Disabled();
    void testFormat_Username_EmptyUsername();

    // Code flag tests
    void testFormat_Code_Enabled_NoTouch();
    void testFormat_Code_Enabled_RequiresTouch();
    void testFormat_Code_Disabled();
    void testFormat_Code_EmptyCode();

    // Device name flag tests
    void testFormat_DeviceName_Enabled_SingleDevice();
    void testFormat_DeviceName_Enabled_MultipleDevices();
    void testFormat_DeviceName_Disabled();
    void testFormat_DeviceName_OnlyWhenMultiple_SingleDevice();
    void testFormat_DeviceName_OnlyWhenMultiple_MultipleDevices();
    void testFormat_DeviceName_EmptyDeviceName();

    // formatWithCode tests
    void testFormatWithCode_WithCode();
    void testFormatWithCode_RequiresTouch();
    void testFormatWithCode_AllOptions();

    // Edge cases
    void testFormat_EmptyIssuer_UsesName();
    void testFormat_EmptyIssuerAndName();
    void testFormat_AllEmpty();

    // Real-world scenarios
    void testRealWorldScenarios();
};

// ========== Basic Formatting Tests ==========

void TestFlexibleDisplayStrategy::testFormat_OnlyIssuer()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "123456";
    cred.requiresTouch = false;

    // All flags disabled - should show only issuer
    QString result = FlexibleDisplayStrategy::format(
        cred,
        false, // showUsername
        false, // showCode
        false, // showDeviceName
        QString(), // deviceName
        1, // connectedDeviceCount
        false // showDeviceOnlyWhenMultiple
    );

    QCOMPARE(result, QString("Google"));
}

void TestFlexibleDisplayStrategy::testFormat_IssuerWithUsername()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";

    QString result = FlexibleDisplayStrategy::format(
        cred,
        true, // showUsername
        false, false, QString(), 1, false
    );

    QCOMPARE(result, QString("Google (user@example.com)"));
}

void TestFlexibleDisplayStrategy::testFormat_IssuerWithCode()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "123456";
    cred.requiresTouch = false;

    QString result = FlexibleDisplayStrategy::format(
        cred,
        false, // showUsername
        true, // showCode
        false, QString(), 1, false
    );

    QCOMPARE(result, QString("Google - 123456"));
}

void TestFlexibleDisplayStrategy::testFormat_IssuerWithDeviceName()
{
    OathCredential cred;
    cred.issuer = "Google";

    QString result = FlexibleDisplayStrategy::format(
        cred,
        false, false,
        true, // showDeviceName
        QString("YubiKey 5"), // deviceName
        2, // connectedDeviceCount
        false // showDeviceOnlyWhenMultiple
    );

    QCOMPARE(result, QString("Google @ YubiKey 5"));
}

void TestFlexibleDisplayStrategy::testFormat_AllOptions()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "123456";
    cred.requiresTouch = false;

    QString result = FlexibleDisplayStrategy::format(
        cred,
        true, // showUsername
        true, // showCode
        true, // showDeviceName
        QString("YubiKey 5"), // deviceName
        2, // connectedDeviceCount
        false // showDeviceOnlyWhenMultiple
    );

    QCOMPARE(result, QString("Google (user@example.com) - 123456 @ YubiKey 5"));
}

// ========== Username Flag Tests ==========

void TestFlexibleDisplayStrategy::testFormat_Username_Enabled()
{
    OathCredential cred;
    cred.issuer = "GitHub";
    cred.username = "developer";

    QString result = FlexibleDisplayStrategy::format(
        cred, true, false, false, QString(), 1, false
    );

    QCOMPARE(result, QString("GitHub (developer)"));
}

void TestFlexibleDisplayStrategy::testFormat_Username_Disabled()
{
    OathCredential cred;
    cred.issuer = "GitHub";
    cred.username = "developer";

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false, false, QString(), 1, false
    );

    QCOMPARE(result, QString("GitHub"));
}

void TestFlexibleDisplayStrategy::testFormat_Username_EmptyUsername()
{
    OathCredential cred;
    cred.issuer = "GitHub";
    cred.username = "";

    QString result = FlexibleDisplayStrategy::format(
        cred, true, false, false, QString(), 1, false
    );

    // Should not append parentheses if username is empty
    QCOMPARE(result, QString("GitHub"));
}

// ========== Code Flag Tests ==========

void TestFlexibleDisplayStrategy::testFormat_Code_Enabled_NoTouch()
{
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.code = "654321";
    cred.requiresTouch = false;

    QString result = FlexibleDisplayStrategy::format(
        cred, false, true, false, QString(), 1, false
    );

    QCOMPARE(result, QString("Amazon - 654321"));
}

void TestFlexibleDisplayStrategy::testFormat_Code_Enabled_RequiresTouch()
{
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.code = "654321";
    cred.requiresTouch = true;

    QString result = FlexibleDisplayStrategy::format(
        cred, false, true, false, QString(), 1, false
    );

    // Should not show code if touch is required
    QCOMPARE(result, QString("Amazon"));
}

void TestFlexibleDisplayStrategy::testFormat_Code_Disabled()
{
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.code = "654321";
    cred.requiresTouch = false;

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false, false, QString(), 1, false
    );

    // Should not show code if flag disabled
    QCOMPARE(result, QString("Amazon"));
}

void TestFlexibleDisplayStrategy::testFormat_Code_EmptyCode()
{
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.code = "";
    cred.requiresTouch = false;

    QString result = FlexibleDisplayStrategy::format(
        cred, false, true, false, QString(), 1, false
    );

    // Should not append code if it's empty
    QCOMPARE(result, QString("Amazon"));
}

// ========== Device Name Flag Tests ==========

void TestFlexibleDisplayStrategy::testFormat_DeviceName_Enabled_SingleDevice()
{
    OathCredential cred;
    cred.issuer = "Microsoft";

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false,
        true, // showDeviceName
        QString("YubiKey 5C"), // deviceName
        1, // connectedDeviceCount
        false // showDeviceOnlyWhenMultiple
    );

    // Should show device name even with single device
    QCOMPARE(result, QString("Microsoft @ YubiKey 5C"));
}

void TestFlexibleDisplayStrategy::testFormat_DeviceName_Enabled_MultipleDevices()
{
    OathCredential cred;
    cred.issuer = "Microsoft";

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false,
        true, // showDeviceName
        QString("YubiKey 5C"), // deviceName
        3, // connectedDeviceCount
        false // showDeviceOnlyWhenMultiple
    );

    QCOMPARE(result, QString("Microsoft @ YubiKey 5C"));
}

void TestFlexibleDisplayStrategy::testFormat_DeviceName_Disabled()
{
    OathCredential cred;
    cred.issuer = "Microsoft";

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false,
        false, // showDeviceName
        QString("YubiKey 5C"), // deviceName
        2, // connectedDeviceCount
        false // showDeviceOnlyWhenMultiple
    );

    // Should not show device name if flag disabled
    QCOMPARE(result, QString("Microsoft"));
}

void TestFlexibleDisplayStrategy::testFormat_DeviceName_OnlyWhenMultiple_SingleDevice()
{
    OathCredential cred;
    cred.issuer = "Microsoft";

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false,
        true, // showDeviceName
        QString("YubiKey 5C"), // deviceName
        1, // connectedDeviceCount
        true // showDeviceOnlyWhenMultiple
    );

    // Should NOT show device name with single device
    QCOMPARE(result, QString("Microsoft"));
}

void TestFlexibleDisplayStrategy::testFormat_DeviceName_OnlyWhenMultiple_MultipleDevices()
{
    OathCredential cred;
    cred.issuer = "Microsoft";

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false,
        true, // showDeviceName
        QString("YubiKey 5C"), // deviceName
        2, // connectedDeviceCount
        true // showDeviceOnlyWhenMultiple
    );

    // Should show device name with multiple devices
    QCOMPARE(result, QString("Microsoft @ YubiKey 5C"));
}

void TestFlexibleDisplayStrategy::testFormat_DeviceName_EmptyDeviceName()
{
    OathCredential cred;
    cred.issuer = "Microsoft";

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false,
        true, // showDeviceName
        QString(), // empty deviceName
        2, // connectedDeviceCount
        false // showDeviceOnlyWhenMultiple
    );

    // Should not append device section if name is empty
    QCOMPARE(result, QString("Microsoft"));
}

// ========== formatWithCode Tests ==========

void TestFlexibleDisplayStrategy::testFormatWithCode_WithCode()
{
    OathCredential cred;
    cred.issuer = "Dropbox";
    cred.username = "user";
    cred.requiresTouch = false;

    QString result = FlexibleDisplayStrategy::formatWithCode(
        cred,
        QString("789012"), // code
        false, // requiresTouch
        true, // showUsername
        true, // showCode
        false, false, 1, false
    );

    QCOMPARE(result, QString("Dropbox (user) - 789012"));
}

void TestFlexibleDisplayStrategy::testFormatWithCode_RequiresTouch()
{
    OathCredential cred;
    cred.issuer = "Dropbox";
    cred.username = "user";
    cred.requiresTouch = true;

    QString result = FlexibleDisplayStrategy::formatWithCode(
        cred,
        QString("789012"), // code
        true, // requiresTouch
        true, // showUsername
        true, // showCode
        false, false, 1, false
    );

    // Should show touch indicator instead of code
    QCOMPARE(result, QString("Dropbox (user) - [Touch Required]"));
}

void TestFlexibleDisplayStrategy::testFormatWithCode_AllOptions()
{
    OathCredential cred;
    cred.issuer = "Dropbox";
    cred.username = "user";
    cred.requiresTouch = false;

    QString result = FlexibleDisplayStrategy::formatWithCode(
        cred,
        QString("789012"), // code
        false, // requiresTouch
        true, // showUsername
        true, // showCode
        true, // showDeviceName
        QString("YubiKey 5"), // deviceName
        2, // connectedDeviceCount
        false // showDeviceOnlyWhenMultiple
    );

    QCOMPARE(result, QString("Dropbox (user) - 789012 @ YubiKey 5"));
}

// ========== Edge Cases ==========

void TestFlexibleDisplayStrategy::testFormat_EmptyIssuer_UsesName()
{
    OathCredential cred;
    cred.name = "MyAccount";
    cred.issuer = "";
    cred.username = "user";

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false, false, QString(), 1, false
    );

    // Should fall back to name when issuer is empty
    QCOMPARE(result, QString("MyAccount"));
}

void TestFlexibleDisplayStrategy::testFormat_EmptyIssuerAndName()
{
    OathCredential cred;
    cred.name = "";
    cred.issuer = "";
    cred.username = "user";

    QString result = FlexibleDisplayStrategy::format(
        cred, false, false, false, QString(), 1, false
    );

    // Should return empty string
    QCOMPARE(result, QString(""));
}

void TestFlexibleDisplayStrategy::testFormat_AllEmpty()
{
    OathCredential cred;
    cred.name = "";
    cred.issuer = "";
    cred.username = "";

    QString result = FlexibleDisplayStrategy::format(
        cred, true, false, false, QString(), 1, false
    );

    // Should return empty string
    QCOMPARE(result, QString(""));
}

// ========== Real-World Scenarios ==========

void TestFlexibleDisplayStrategy::testRealWorldScenarios()
{
    // Scenario 1: Google account with all options
    {
        OathCredential cred;
        cred.issuer = "Google";
        cred.username = "user@gmail.com";
        cred.code = "123456";
        cred.requiresTouch = false;

        QString result = FlexibleDisplayStrategy::format(
            cred, true, true, true,
            QString("YubiKey 5"), 2, false
        );

        QCOMPARE(result, QString("Google (user@gmail.com) - 123456 @ YubiKey 5"));
    }

    // Scenario 2: GitHub with touch required
    {
        OathCredential cred;
        cred.issuer = "GitHub";
        cred.username = "developer";
        cred.requiresTouch = true;

        QString result = FlexibleDisplayStrategy::format(
            cred, true, true, false,
            QString(), 1, false
        );

        // Should not show code due to touch requirement
        QCOMPARE(result, QString("GitHub (developer)"));
    }

    // Scenario 3: AWS with minimal display
    {
        OathCredential cred;
        cred.issuer = "AWS";
        cred.username = "admin";

        QString result = FlexibleDisplayStrategy::format(
            cred, false, false, false,
            QString(), 1, false
        );

        QCOMPARE(result, QString("AWS"));
    }

    // Scenario 4: Multiple devices with selective display
    {
        OathCredential cred;
        cred.issuer = "Slack";
        cred.username = "team@company.com";

        QString result = FlexibleDisplayStrategy::format(
            cred, true, false, true,
            QString("YubiKey 5C NFC"), 3, true
        );

        QCOMPARE(result, QString("Slack (team@company.com) @ YubiKey 5C NFC"));
    }

    // Scenario 5: Single device with onlyWhenMultiple flag
    {
        OathCredential cred;
        cred.issuer = "Slack";
        cred.username = "team@company.com";

        QString result = FlexibleDisplayStrategy::format(
            cred, true, false, true,
            QString("YubiKey 5C NFC"), 1, true
        );

        // Device name should be hidden with single device
        QCOMPARE(result, QString("Slack (team@company.com)"));
    }
}

QTEST_MAIN(TestFlexibleDisplayStrategy)
#include "test_flexible_display_strategy.moc"
