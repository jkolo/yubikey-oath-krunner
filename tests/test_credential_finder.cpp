/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "shared/utils/credential_finder.h"
#include "shared/types/oath_credential.h"

using namespace YubiKeyOath::Utils;
using namespace YubiKeyOath::Shared;

/**
 * @brief Unit tests for CredentialFinder
 *
 * Tests credential search functionality by name and device ID.
 */
class TestCredentialFinder : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Basic search tests
    void testFindCredential_ExactMatch();
    void testFindCredential_WrongName();
    void testFindCredential_WrongDeviceId();
    void testFindCredential_EmptyList();
    void testFindCredential_NoMatch();

    // Multiple credential tests
    void testFindCredential_MultipleMatches_ReturnsFirst();
    void testFindCredential_MultipleDevices_CorrectDeviceId();

    // Edge cases
    void testFindCredential_EmptyStrings();
    void testFindCredential_SpecialCharacters();

private:
    // Helper function to create test credential
    OathCredential createCredential(const QString &name, const QString &deviceId) {
        OathCredential cred;
        cred.originalName = name;
        cred.deviceId = deviceId;
        cred.issuer = "TestIssuer";
        cred.account = "test@example.com";
        cred.code = "123456";
        cred.validUntil = 0;
        cred.requiresTouch = false;
        cred.isTotp = true;
        return cred;
    }
};

// ========== Basic Search Tests ==========

void TestCredentialFinder::testFindCredential_ExactMatch()
{
    // Test finding credential with exact name and device ID match
    QList<OathCredential> credentials;
    credentials.append(createCredential("GitHub:user1", "device123"));
    credentials.append(createCredential("Google:user2", "device456"));

    auto result = findCredential(credentials, "GitHub:user1", "device123");

    QVERIFY(result.has_value());
    QCOMPARE(result->originalName, "GitHub:user1");
    QCOMPARE(result->deviceId, "device123");
}

void TestCredentialFinder::testFindCredential_WrongName()
{
    // Test that wrong credential name returns nullopt
    QList<OathCredential> credentials;
    credentials.append(createCredential("GitHub:user1", "device123"));

    auto result = findCredential(credentials, "Google:user1", "device123");

    QVERIFY(!result.has_value());
}

void TestCredentialFinder::testFindCredential_WrongDeviceId()
{
    // Test that wrong device ID returns nullopt
    QList<OathCredential> credentials;
    credentials.append(createCredential("GitHub:user1", "device123"));

    auto result = findCredential(credentials, "GitHub:user1", "device456");

    QVERIFY(!result.has_value());
}

void TestCredentialFinder::testFindCredential_EmptyList()
{
    // Test searching in empty list
    QList<OathCredential> credentials; // Empty

    auto result = findCredential(credentials, "GitHub:user1", "device123");

    QVERIFY(!result.has_value());
}

void TestCredentialFinder::testFindCredential_NoMatch()
{
    // Test when credential exists but neither name nor device ID match
    QList<OathCredential> credentials;
    credentials.append(createCredential("GitHub:user1", "device123"));
    credentials.append(createCredential("Google:user2", "device456"));

    auto result = findCredential(credentials, "Amazon:user3", "device789");

    QVERIFY(!result.has_value());
}

// ========== Multiple Credential Tests ==========

void TestCredentialFinder::testFindCredential_MultipleMatches_ReturnsFirst()
{
    // Test that when multiple credentials match, the first one is returned
    QList<OathCredential> credentials;

    auto cred1 = createCredential("GitHub:user1", "device123");
    cred1.issuer = "FirstIssuer";
    credentials.append(cred1);

    auto cred2 = createCredential("GitHub:user1", "device123");
    cred2.issuer = "SecondIssuer";
    credentials.append(cred2);

    auto result = findCredential(credentials, "GitHub:user1", "device123");

    QVERIFY(result.has_value());
    QCOMPARE(result->originalName, "GitHub:user1");
    QCOMPARE(result->deviceId, "device123");
    QCOMPARE(result->issuer, "FirstIssuer"); // Should return first match
}

void TestCredentialFinder::testFindCredential_MultipleDevices_CorrectDeviceId()
{
    // Test finding credential when same credential name exists on multiple devices
    QList<OathCredential> credentials;
    credentials.append(createCredential("GitHub:user1", "device123"));
    credentials.append(createCredential("GitHub:user1", "device456"));
    credentials.append(createCredential("GitHub:user1", "device789"));

    auto result = findCredential(credentials, "GitHub:user1", "device456");

    QVERIFY(result.has_value());
    QCOMPARE(result->originalName, "GitHub:user1");
    QCOMPARE(result->deviceId, "device456"); // Should match correct device
}

// ========== Edge Cases ==========

void TestCredentialFinder::testFindCredential_EmptyStrings()
{
    // Test with empty credential name and device ID
    QList<OathCredential> credentials;
    credentials.append(createCredential("", ""));
    credentials.append(createCredential("GitHub:user1", "device123"));

    auto result = findCredential(credentials, "", "");

    QVERIFY(result.has_value());
    QCOMPARE(result->originalName, "");
    QCOMPARE(result->deviceId, "");
}

void TestCredentialFinder::testFindCredential_SpecialCharacters()
{
    // Test with special characters in credential name
    QList<OathCredential> credentials;
    credentials.append(createCredential("GitHub:user+special@example.com", "device!@#$%"));
    credentials.append(createCredential("30/Google:user", "device123"));

    // Test with + and @ characters
    auto result1 = findCredential(credentials, "GitHub:user+special@example.com", "device!@#$%");
    QVERIFY(result1.has_value());
    QCOMPARE(result1->originalName, "GitHub:user+special@example.com");

    // Test with period prefix (non-standard TOTP period)
    auto result2 = findCredential(credentials, "30/Google:user", "device123");
    QVERIFY(result2.has_value());
    QCOMPARE(result2->originalName, "30/Google:user");
}

QTEST_MAIN(TestCredentialFinder)
#include "test_credential_finder.moc"
