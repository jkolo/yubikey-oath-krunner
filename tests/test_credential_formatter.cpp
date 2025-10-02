/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "krunner/formatting/credential_formatter.h"
#include "shared/types/oath_credential.h"

using namespace KRunner::YubiKey;

/**
 * @brief Unit tests for CredentialFormatter
 *
 * Tests all display strategies and formatting logic for OATH credentials.
 */
class TestCredentialFormatter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Strategy-based API tests (recommended)
    void testFormatDisplayName_NameStrategy();
    void testFormatDisplayName_NameUserStrategy();
    void testFormatDisplayName_FullStrategy();
    void testFormatDisplayName_UnknownStrategy();

    // Edge cases for strategies
    void testNameStrategy_EmptyIssuer();
    void testNameStrategy_EmptyUsername();
    void testNameStrategy_BothEmpty();

    void testNameUserStrategy_EmptyIssuer();
    void testNameUserStrategy_EmptyUsername();
    void testNameUserStrategy_BothEmpty();
    void testNameUserStrategy_BothPresent();

    void testFullStrategy_WithCode();
    void testFullStrategy_WithoutCode();
    void testFullStrategy_EmptyCode();

    // Deprecated enum API tests (backward compatibility)
    void testFormatDisplayName_Enum_Name();
    void testFormatDisplayName_Enum_NameUser();
    void testFormatDisplayName_Enum_Full();

    // formatFromString tests (deprecated)
    void testFormatFromString();

    // defaultFormat test
    void testDefaultFormat();

    // Real-world scenarios
    void testRealWorldCredentials();
};

// ========== Strategy-Based API Tests ==========

void TestCredentialFormatter::testFormatDisplayName_NameStrategy()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "123456";

    const QString result =CredentialFormatter::formatDisplayName(cred, "name");

    // NameOnlyStrategy: returns issuer if present
    QCOMPARE(result, QString("Google"));
}

void TestCredentialFormatter::testFormatDisplayName_NameUserStrategy()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "123456";

    const QString result =CredentialFormatter::formatDisplayName(cred, "name_user");

    // NameUserStrategy: "issuer (username)"
    QCOMPARE(result, QString("Google (user@example.com)"));
}

void TestCredentialFormatter::testFormatDisplayName_FullStrategy()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "123456";

    const QString result =CredentialFormatter::formatDisplayName(cred, "full");

    // FullStrategy: "issuer (username) - code"
    QCOMPARE(result, QString("Google (user@example.com) - 123456"));
}

void TestCredentialFormatter::testFormatDisplayName_UnknownStrategy()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";

    // Unknown identifier should default to name_user
    const QString result =CredentialFormatter::formatDisplayName(cred, "invalid_strategy");

    QCOMPARE(result, QString("Google (user@example.com)"));
}

// ========== NameOnlyStrategy Edge Cases ==========

void TestCredentialFormatter::testNameStrategy_EmptyIssuer()
{
    OathCredential cred;
    cred.issuer = "";
    cred.username = "user@example.com";

    const QString result =CredentialFormatter::formatDisplayName(cred, "name");

    // Should return username when issuer is empty
    QCOMPARE(result, QString("user@example.com"));
}

void TestCredentialFormatter::testNameStrategy_EmptyUsername()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "";

    const QString result =CredentialFormatter::formatDisplayName(cred, "name");

    // Should return issuer
    QCOMPARE(result, QString("Google"));
}

void TestCredentialFormatter::testNameStrategy_BothEmpty()
{
    OathCredential cred;
    cred.issuer = "";
    cred.username = "";

    const QString result =CredentialFormatter::formatDisplayName(cred, "name");

    // Should return empty string when both are empty
    QCOMPARE(result, QString(""));
}

// ========== NameUserStrategy Edge Cases ==========

void TestCredentialFormatter::testNameUserStrategy_EmptyIssuer()
{
    OathCredential cred;
    cred.issuer = "";
    cred.username = "user@example.com";

    const QString result =CredentialFormatter::formatDisplayName(cred, "name_user");

    // Should return username only
    QCOMPARE(result, QString("user@example.com"));
}

void TestCredentialFormatter::testNameUserStrategy_EmptyUsername()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "";

    const QString result =CredentialFormatter::formatDisplayName(cred, "name_user");

    // Should return issuer only
    QCOMPARE(result, QString("Google"));
}

void TestCredentialFormatter::testNameUserStrategy_BothEmpty()
{
    OathCredential cred;
    cred.issuer = "";
    cred.username = "";

    const QString result =CredentialFormatter::formatDisplayName(cred, "name_user");

    // Should return empty string
    QCOMPARE(result, QString(""));
}

void TestCredentialFormatter::testNameUserStrategy_BothPresent()
{
    OathCredential cred;
    cred.issuer = "GitHub";
    cred.username = "developer";

    const QString result =CredentialFormatter::formatDisplayName(cred, "name_user");

    // Should return "issuer (username)"
    QCOMPARE(result, QString("GitHub (developer)"));
}

// ========== FullStrategy Tests ==========

void TestCredentialFormatter::testFullStrategy_WithCode()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "654321";

    const QString result =CredentialFormatter::formatDisplayName(cred, "full");

    QCOMPARE(result, QString("Google (user@example.com) - 654321"));
}

void TestCredentialFormatter::testFullStrategy_WithoutCode()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    // code not set (defaults to empty)

    const QString result =CredentialFormatter::formatDisplayName(cred, "full");

    // Should return base format without code
    QCOMPARE(result, QString("Google (user@example.com)"));
}

void TestCredentialFormatter::testFullStrategy_EmptyCode()
{
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "";

    const QString result =CredentialFormatter::formatDisplayName(cred, "full");

    // Empty code should be treated as no code
    QCOMPARE(result, QString("Google (user@example.com)"));
}

// ========== Deprecated Enum API Tests ==========

void TestCredentialFormatter::testFormatDisplayName_Enum_Name()
{
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.username = "shopper";

    const QString result =CredentialFormatter::formatDisplayName(cred, CredentialFormatter::Name);

    QCOMPARE(result, QString("Amazon"));
}

void TestCredentialFormatter::testFormatDisplayName_Enum_NameUser()
{
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.username = "shopper";

    const QString result =CredentialFormatter::formatDisplayName(cred, CredentialFormatter::NameUser);

    QCOMPARE(result, QString("Amazon (shopper)"));
}

void TestCredentialFormatter::testFormatDisplayName_Enum_Full()
{
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.username = "shopper";
    cred.code = "999888";

    const QString result =CredentialFormatter::formatDisplayName(cred, CredentialFormatter::Full);

    QCOMPARE(result, QString("Amazon (shopper) - 999888"));
}

// ========== formatFromString Tests ==========

void TestCredentialFormatter::testFormatFromString()
{
    QCOMPARE(CredentialFormatter::formatFromString("name"), CredentialFormatter::Name);
    QCOMPARE(CredentialFormatter::formatFromString("name_user"), CredentialFormatter::NameUser);
    QCOMPARE(CredentialFormatter::formatFromString("full"), CredentialFormatter::Full);

    // Unknown string should default to NameUser
    QCOMPARE(CredentialFormatter::formatFromString("invalid"), CredentialFormatter::NameUser);
    QCOMPARE(CredentialFormatter::formatFromString(""), CredentialFormatter::NameUser);
}

// ========== defaultFormat Test ==========

void TestCredentialFormatter::testDefaultFormat()
{
    const QString defaultFmt = CredentialFormatter::defaultFormat();

    // Default should be "name_user"
    QCOMPARE(defaultFmt, QString("name_user"));
}

// ========== Real-World Scenarios ==========

void TestCredentialFormatter::testRealWorldCredentials()
{
    // Test with various real-world credential formats

    // Scenario 1: Typical Google account
    {
        OathCredential cred;
        cred.issuer = "Google";
        cred.username = "user@gmail.com";
        cred.code = "123456";

        QCOMPARE(CredentialFormatter::formatDisplayName(cred, "name"), QString("Google"));
        QCOMPARE(CredentialFormatter::formatDisplayName(cred, "name_user"), QString("Google (user@gmail.com)"));
        QCOMPARE(CredentialFormatter::formatDisplayName(cred, "full"), QString("Google (user@gmail.com) - 123456"));
    }

    // Scenario 2: GitHub with username only
    {
        OathCredential cred;
        cred.issuer = "GitHub";
        cred.username = "developer123";
        cred.code = "789012";

        QCOMPARE(CredentialFormatter::formatDisplayName(cred, "name_user"), QString("GitHub (developer123)"));
    }

    // Scenario 3: Service without issuer (username only)
    {
        OathCredential cred;
        cred.issuer = "";
        cred.username = "admin@company.com";
        cred.code = "345678";

        QCOMPARE(CredentialFormatter::formatDisplayName(cred, "name"), QString("admin@company.com"));
        QCOMPARE(CredentialFormatter::formatDisplayName(cred, "name_user"), QString("admin@company.com"));
    }

    // Scenario 4: Service with issuer only (no username)
    {
        OathCredential cred;
        cred.issuer = "AWS Root Account";
        cred.username = "";
        cred.code = "901234";

        QCOMPARE(CredentialFormatter::formatDisplayName(cred, "name"), QString("AWS Root Account"));
        QCOMPARE(CredentialFormatter::formatDisplayName(cred, "name_user"), QString("AWS Root Account"));
    }

    // Scenario 5: Corporate VPN with long names
    {
        OathCredential cred;
        cred.issuer = "Corporate VPN";
        cred.username = "employee.name@corporation.example.com";
        cred.code = "567890";

        QCOMPARE(CredentialFormatter::formatDisplayName(cred, "name_user"),
                 QString("Corporate VPN (employee.name@corporation.example.com)"));
    }
}

QTEST_MAIN(TestCredentialFormatter)
#include "test_credential_formatter.moc"
