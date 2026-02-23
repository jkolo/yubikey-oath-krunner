/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/daemon/utils/credential_id_encoder.h"

#include <QtTest>

using namespace YubiKeyOath::Daemon;

/**
 * @brief Tests for CredentialIdEncoder
 *
 * Verifies D-Bus object path encoding for credential names.
 */
class TestCredentialIdEncoder : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Basic encoding
    void testAsciiLetters();
    void testUppercaseToLowercase();
    void testDigitsPreserved();
    void testUnderscorePreserved();

    // Special character mappings
    void testAtSign();
    void testDot();
    void testColon();
    void testSpace();
    void testSlash();
    void testCommonSpecialChars();

    // Polish transliteration
    void testPolishLowercase();
    void testPolishUppercase();

    // Unicode fallback
    void testUnknownUnicodeEncoding();

    // Leading digit handling
    void testLeadingDigitPrepended();
    void testNoLeadingDigitNoPrepend();

    // Typical credential names
    void testTypicalCredentialName();
    void testCredentialWithEmail();
    void testCredentialNoIssuer();

    // Edge cases
    void testEmptyString();
    void testVeryLongName();
    void testDeterministic();
    void testOnlySpecialChars();
};

void TestCredentialIdEncoder::testAsciiLetters()
{
    QCOMPARE(CredentialIdEncoder::encode("abcxyz"), "abcxyz");
}

void TestCredentialIdEncoder::testUppercaseToLowercase()
{
    QCOMPARE(CredentialIdEncoder::encode("GitHub"), "github");
}

void TestCredentialIdEncoder::testDigitsPreserved()
{
    QCOMPARE(CredentialIdEncoder::encode("test123"), "test123");
}

void TestCredentialIdEncoder::testUnderscorePreserved()
{
    QCOMPARE(CredentialIdEncoder::encode("my_cred"), "my_cred");
}

void TestCredentialIdEncoder::testAtSign()
{
    const QString result = CredentialIdEncoder::encode("user@example");
    QVERIFY(result.contains("_at_"));
}

void TestCredentialIdEncoder::testDot()
{
    const QString result = CredentialIdEncoder::encode("example.com");
    QVERIFY(result.contains("_dot_"));
}

void TestCredentialIdEncoder::testColon()
{
    const QString result = CredentialIdEncoder::encode("issuer:account");
    QVERIFY(result.contains("_colon_"));
}

void TestCredentialIdEncoder::testSpace()
{
    const QString result = CredentialIdEncoder::encode("my account");
    QVERIFY(result.contains("_"));
    QVERIFY(!result.contains(" "));
}

void TestCredentialIdEncoder::testSlash()
{
    const QString result = CredentialIdEncoder::encode("path/name");
    QVERIFY(result.contains("_slash_"));
}

void TestCredentialIdEncoder::testCommonSpecialChars()
{
    // Test that all mapped special chars produce valid D-Bus path chars
    const QString input = "+=-&#%!?*<>|~";
    const QString result = CredentialIdEncoder::encode(input);

    // Result should only contain [a-z0-9_] (or start with 'c' if digit)
    static const QRegularExpression validChars("^[a-z0-9_]+$");
    QVERIFY2(validChars.match(result).hasMatch(),
             qPrintable("Invalid chars in: " + result));
}

void TestCredentialIdEncoder::testPolishLowercase()
{
    // ąćęłńóśźż → acelnoszz
    const QString input = QString::fromUtf8("ąćęłńóśźż");
    const QString result = CredentialIdEncoder::encode(input);
    QCOMPARE(result, "acelnoszz");
}

void TestCredentialIdEncoder::testPolishUppercase()
{
    // ĄĆĘŁŃÓŚŹŻ → acelnoszz
    const QString input = QString::fromUtf8("ĄĆĘŁŃÓŚŹŻ");
    const QString result = CredentialIdEncoder::encode(input);
    QCOMPARE(result, "acelnoszz");
}

void TestCredentialIdEncoder::testUnknownUnicodeEncoding()
{
    // Japanese character should be encoded as _uXXXX
    const QString input = QString(QChar(0x3042)); // hiragana 'a'
    const QString result = CredentialIdEncoder::encode(input);
    QVERIFY(result.contains("_u3042"));
}

void TestCredentialIdEncoder::testLeadingDigitPrepended()
{
    const QString result = CredentialIdEncoder::encode("123service");
    QVERIFY(result.startsWith("c"));
    QCOMPARE(result, "c123service");
}

void TestCredentialIdEncoder::testNoLeadingDigitNoPrepend()
{
    const QString result = CredentialIdEncoder::encode("service123");
    QVERIFY(!result.startsWith("c") || result == "c"); // 'c' is valid ASCII
    QCOMPARE(result, "service123");
}

void TestCredentialIdEncoder::testTypicalCredentialName()
{
    const QString result = CredentialIdEncoder::encode("GitHub:user");
    QCOMPARE(result, "github_colon_user");
}

void TestCredentialIdEncoder::testCredentialWithEmail()
{
    const QString result = CredentialIdEncoder::encode("Google:user@example.com");
    QCOMPARE(result, "google_colon_user_at_example_dot_com");
}

void TestCredentialIdEncoder::testCredentialNoIssuer()
{
    const QString result = CredentialIdEncoder::encode("myaccount");
    QCOMPARE(result, "myaccount");
}

void TestCredentialIdEncoder::testEmptyString()
{
    const QString result = CredentialIdEncoder::encode("");
    QVERIFY(result.isEmpty());
}

void TestCredentialIdEncoder::testVeryLongName()
{
    // Create name > 200 chars
    QString longName;
    for (int i = 0; i < 250; ++i) {
        longName += 'a';
    }
    const QString result = CredentialIdEncoder::encode(longName);

    // Should be truncated and hashed
    QVERIFY(result.length() <= 200);
    QVERIFY(result.startsWith("cred_"));
}

void TestCredentialIdEncoder::testDeterministic()
{
    // Same input always produces same output
    const QString input = "GitHub:test@example.com";
    const QString result1 = CredentialIdEncoder::encode(input);
    const QString result2 = CredentialIdEncoder::encode(input);
    QCOMPARE(result1, result2);
}

void TestCredentialIdEncoder::testOnlySpecialChars()
{
    const QString result = CredentialIdEncoder::encode("@.:");
    // Should produce valid D-Bus path chars
    static const QRegularExpression validChars("^[a-z0-9_]+$");
    QVERIFY2(validChars.match(result).hasMatch(),
             qPrintable("Invalid chars in: " + result));
}

QTEST_MAIN(TestCredentialIdEncoder)
#include "test_credential_id_encoder.moc"
