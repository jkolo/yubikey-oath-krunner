/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "formatting/credential_formatter.h"
#include "types/oath_credential.h"
#include "types/yubikey_value_types.h"

using namespace YubiKeyOath::Shared;

/**
 * @brief Unit tests for CredentialFormatter
 *
 * Tests the CredentialFormatter wrapper around FlexibleDisplayStrategy.
 * Verifies correct parameter passing and overload resolution for both
 * OathCredential and CredentialInfo types.
 */
class TestCredentialFormatter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // OathCredential overload tests
    void testFormatDisplayName_OathCredential_Basic();
    void testFormatDisplayName_OathCredential_WithUsername();
    void testFormatDisplayName_OathCredential_WithCode();
    void testFormatDisplayName_OathCredential_WithDeviceName();
    void testFormatDisplayName_OathCredential_AllOptions();

    // CredentialInfo overload tests
    void testFormatDisplayName_CredentialInfo_Basic();
    void testFormatDisplayName_CredentialInfo_WithUsername();
    void testFormatDisplayName_CredentialInfo_WithCode();
    void testFormatDisplayName_CredentialInfo_WithDeviceName();
    void testFormatDisplayName_CredentialInfo_AllOptions();

    // Edge cases
    void testFormatDisplayName_EmptyFields();
    void testFormatDisplayName_DeviceNameVisibility();

    // Real-world scenarios
    void testRealWorldCredentials();
};

// ========== OathCredential Overload Tests ==========

void TestCredentialFormatter::testFormatDisplayName_OathCredential_Basic()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.account = "user@example.com";

    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            false, // showUsername
            false, // showCode
            false, // showDeviceName
            QString(), // deviceName
            1, // connectedDeviceCount
            false // showDeviceOnlyWhenMultiple
        )
    );

    QCOMPARE(result, QString("Google"));
}

void TestCredentialFormatter::testFormatDisplayName_OathCredential_WithUsername()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.account = "user@example.com";

    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            true, // showUsername
            false, false, QString(), 1, false
        )
    );

    QCOMPARE(result, QString("Google (user@example.com)"));
}

void TestCredentialFormatter::testFormatDisplayName_OathCredential_WithCode()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.code = "123456";
    cred.requiresTouch = false;

    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            false, // showUsername
            true, // showCode
            false, QString(), 1, false
        )
    );

    QCOMPARE(result, QString("Google - 123456"));
}

void TestCredentialFormatter::testFormatDisplayName_OathCredential_WithDeviceName()
{
    OathCredential cred;
    cred.issuer = "Google";

    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            false, false,
            true, // showDeviceName
            QString("YubiKey 5"), // deviceName
            2, // connectedDeviceCount
            false // showDeviceOnlyWhenMultiple
        )
    );

    QCOMPARE(result, QString("Google @ YubiKey 5"));
}

void TestCredentialFormatter::testFormatDisplayName_OathCredential_AllOptions()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.account = "user@example.com";
    cred.code = "123456";
    cred.requiresTouch = false;

    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            true, // showUsername
            true, // showCode
            true, // showDeviceName
            QString("YubiKey 5"), // deviceName
            2, // connectedDeviceCount
            false // showDeviceOnlyWhenMultiple
        )
    );

    QCOMPARE(result, QString("Google (user@example.com) - 123456 @ YubiKey 5"));
}

// ========== CredentialInfo Overload Tests ==========

void TestCredentialFormatter::testFormatDisplayName_CredentialInfo_Basic()
{
    CredentialInfo cred;
    cred.issuer = "GitHub";
    cred.account = "developer";
    cred.name = "GitHub:developer";

    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            false, // showUsername
            false, false, QString(), 1, false
        )
    );

    QCOMPARE(result, QString("GitHub"));
}

void TestCredentialFormatter::testFormatDisplayName_CredentialInfo_WithUsername()
{
    CredentialInfo cred;
    cred.issuer = "GitHub";
    cred.account = "developer";
    cred.name = "GitHub:developer";

    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            true, // showUsername
            false, false, QString(), 1, false
        )
    );

    QCOMPARE(result, QString("GitHub (developer)"));
}

void TestCredentialFormatter::testFormatDisplayName_CredentialInfo_WithCode()
{
    CredentialInfo cred;
    cred.issuer = "GitHub";
    cred.account = "developer";
    cred.name = "GitHub:developer";
    cred.requiresTouch = false;

    // Note: CredentialInfo doesn't have a code field,
    // but showCode flag should be handled gracefully
    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            false, // showUsername
            true, // showCode (should be ignored for CredentialInfo)
            false, QString(), 1, false
        )
    );

    QCOMPARE(result, QString("GitHub"));
}

void TestCredentialFormatter::testFormatDisplayName_CredentialInfo_WithDeviceName()
{
    CredentialInfo cred;
    cred.issuer = "GitHub";
    cred.account = "developer";
    cred.name = "GitHub:developer";

    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            false, false,
            true, // showDeviceName
            QString("YubiKey 5C"), // deviceName
            2, // connectedDeviceCount
            false // showDeviceOnlyWhenMultiple
        )
    );

    QCOMPARE(result, QString("GitHub @ YubiKey 5C"));
}

void TestCredentialFormatter::testFormatDisplayName_CredentialInfo_AllOptions()
{
    CredentialInfo cred;
    cred.issuer = "GitHub";
    cred.account = "developer";
    cred.name = "GitHub:developer";
    cred.requiresTouch = false;

    QString result = CredentialFormatter::formatDisplayName(
        cred,
        FormatOptions(
            true, // showUsername
            true, // showCode (ignored for CredentialInfo)
            true, // showDeviceName
            QString("YubiKey 5C"), // deviceName
            2, // connectedDeviceCount
            false // showDeviceOnlyWhenMultiple
        )
    );

    QCOMPARE(result, QString("GitHub (developer) @ YubiKey 5C"));
}

// ========== Edge Cases ==========

void TestCredentialFormatter::testFormatDisplayName_EmptyFields()
{
    // Test with empty issuer - should fall back to account
    {
        OathCredential cred;
        cred.originalName = "MyAccount";
        cred.issuer = "";
        cred.account = "user";

        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(false, false, false, QString(), 1, false)
        );

        QCOMPARE(result, QString("user")); // Uses account when issuer is empty
    }

    // Test with empty username - should not add parentheses
    {
        OathCredential cred;
        cred.issuer = "Amazon";
        cred.account = "";

        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(true, false, false, QString(), 1, false)
        );

        QCOMPARE(result, QString("Amazon"));
    }

    // Test with empty device name - should not add @ section
    {
        OathCredential cred;
        cred.issuer = "Amazon";

        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(false, false, true, QString(), 2, false)
        );

        QCOMPARE(result, QString("Amazon"));
    }
}

void TestCredentialFormatter::testFormatDisplayName_DeviceNameVisibility()
{
    OathCredential cred;
    cred.issuer = "Microsoft";

    // Test: showDeviceName=true, onlyWhenMultiple=true, single device
    {
        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(
                false, false,
                true, // showDeviceName
                QString("YubiKey 5"), // deviceName
                1, // single device
                true // onlyWhenMultiple
            )
        );

        // Should NOT show device name with single device
        QCOMPARE(result, QString("Microsoft"));
    }

    // Test: showDeviceName=true, onlyWhenMultiple=true, multiple devices
    {
        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(
                false, false,
                true, // showDeviceName
                QString("YubiKey 5"), // deviceName
                2, // multiple devices
                true // onlyWhenMultiple
            )
        );

        // Should show device name with multiple devices
        QCOMPARE(result, QString("Microsoft @ YubiKey 5"));
    }

    // Test: showDeviceName=true, onlyWhenMultiple=false, single device
    {
        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(
                false, false,
                true, // showDeviceName
                QString("YubiKey 5"), // deviceName
                1, // single device
                false // onlyWhenMultiple
            )
        );

        // Should show device name even with single device
        QCOMPARE(result, QString("Microsoft @ YubiKey 5"));
    }
}

// ========== Real-World Scenarios ==========

void TestCredentialFormatter::testRealWorldCredentials()
{
    // Scenario 1: Google account with username and code
    {
        OathCredential cred;
        cred.issuer = "Google";
        cred.account = "user@gmail.com";
        cred.code = "123456";
        cred.requiresTouch = false;

        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(true, true, false, QString(), 1, false)
        );

        QCOMPARE(result, QString("Google (user@gmail.com) - 123456"));
    }

    // Scenario 2: GitHub with touch required (code should not display)
    {
        OathCredential cred;
        cred.issuer = "GitHub";
        cred.account = "developer";
        cred.code = "789012";
        cred.requiresTouch = true;

        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(true, true, false, QString(), 1, false)
        );

        // Code should not be shown due to touch requirement
        QCOMPARE(result, QString("GitHub (developer)"));
    }

    // Scenario 3: AWS with device name in multi-device setup
    {
        OathCredential cred;
        cred.issuer = "AWS";
        cred.account = "admin@company.com";

        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(
                true, false,
                true, // showDeviceName
                QString("YubiKey 5C NFC"), // deviceName
                3, // multiple devices
                true // onlyWhenMultiple
            )
        );

        QCOMPARE(result, QString("AWS (admin@company.com) @ YubiKey 5C NFC"));
    }

    // Scenario 4: Corporate VPN with all options
    {
        OathCredential cred;
        cred.issuer = "Corporate VPN";
        cred.account = "employee.name@corporation.example.com";
        cred.code = "567890";
        cred.requiresTouch = false;

        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(
                true, true, true,
                QString("YubiKey 5 Nano"), 2, false
            )
        );

        QCOMPARE(result,
                 QString("Corporate VPN (employee.name@corporation.example.com) - 567890 @ YubiKey 5 Nano"));
    }

    // Scenario 5: CredentialInfo from D-Bus
    {
        CredentialInfo cred;
        cred.name = "Slack:workspace";
        cred.issuer = "Slack";
        cred.account = "workspace";
        cred.deviceId = "abc123";
        cred.requiresTouch = false;

        QString result = CredentialFormatter::formatDisplayName(
            cred, FormatOptions(
                true, false, true,
                QString("YubiKey Bio"), 1, false
            )
        );

        QCOMPARE(result, QString("Slack (workspace) @ YubiKey Bio"));
    }
}

QTEST_MAIN(TestCredentialFormatter)
#include "test_credential_formatter.moc"
