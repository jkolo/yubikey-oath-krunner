/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/daemon/utils/password_derivation.h"

#include <QtTest>

using namespace YubiKeyOath::Daemon;

/**
 * @brief Tests for PasswordDerivation PBKDF2 implementation
 *
 * Verifies PBKDF2-HMAC-SHA1 key derivation against known test vectors.
 * RFC 8018 Section 6 does not provide HMAC-SHA1 vectors directly,
 * but RFC 6070 provides PBKDF2-HMAC-SHA1 test vectors.
 */
class TestPasswordDerivation : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Constants
    void testOathConstants();

    // RFC 6070 test vectors (PBKDF2-HMAC-SHA1)
    void testRfc6070Vector1();
    void testRfc6070Vector2();

    // OATH-specific usage
    void testOathDerivation();
    void testDeterministic();

    // Edge cases
    void testDifferentKeyLengths();
    void testMultiBlockKey();
    void testEmptyPassword();
    void testEmptySalt();
};

void TestPasswordDerivation::testOathConstants()
{
    QCOMPARE(PasswordDerivation::OATH_PBKDF2_ITERATIONS, 1000);
    QCOMPARE(PasswordDerivation::OATH_DERIVED_KEY_LENGTH, 16);
}

void TestPasswordDerivation::testRfc6070Vector1()
{
    // RFC 6070 Test Vector 1: P="password", S="salt", c=1, dkLen=20
    const QByteArray password = "password";
    const QByteArray salt = "salt";
    const QByteArray expected = QByteArray::fromHex("0c60c80f961f0e71f3a9b524af6012062fe037a6");

    const QByteArray result = PasswordDerivation::deriveKeyPbkdf2(password, salt, 1, 20);
    QCOMPARE(result.toHex(), expected.toHex());
}

void TestPasswordDerivation::testRfc6070Vector2()
{
    // RFC 6070 Test Vector 2: P="password", S="salt", c=2, dkLen=20
    const QByteArray password = "password";
    const QByteArray salt = "salt";
    const QByteArray expected = QByteArray::fromHex("ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957");

    const QByteArray result = PasswordDerivation::deriveKeyPbkdf2(password, salt, 2, 20);
    QCOMPARE(result.toHex(), expected.toHex());
}

void TestPasswordDerivation::testOathDerivation()
{
    // Test OATH standard derivation (1000 iterations, 16 bytes)
    const QByteArray password = "testpassword";
    const QByteArray salt = QByteArray::fromHex("0102030405060708");

    const QByteArray result = PasswordDerivation::deriveKeyPbkdf2(
        password, salt,
        PasswordDerivation::OATH_PBKDF2_ITERATIONS,
        PasswordDerivation::OATH_DERIVED_KEY_LENGTH);

    QCOMPARE(result.length(), 16);
    // Key should be non-zero (probabilistically impossible)
    QVERIFY(result != QByteArray(16, '\0'));
}

void TestPasswordDerivation::testDeterministic()
{
    const QByteArray password = "mypassword";
    const QByteArray salt = "mysalt";

    const QByteArray result1 = PasswordDerivation::deriveKeyPbkdf2(password, salt, 100, 16);
    const QByteArray result2 = PasswordDerivation::deriveKeyPbkdf2(password, salt, 100, 16);
    QCOMPARE(result1, result2);
}

void TestPasswordDerivation::testDifferentKeyLengths()
{
    const QByteArray password = "password";
    const QByteArray salt = "salt";

    // 16-byte key
    const QByteArray key16 = PasswordDerivation::deriveKeyPbkdf2(password, salt, 1, 16);
    QCOMPARE(key16.length(), 16);

    // 20-byte key (one full SHA1 block)
    const QByteArray key20 = PasswordDerivation::deriveKeyPbkdf2(password, salt, 1, 20);
    QCOMPARE(key20.length(), 20);

    // 16-byte key should be prefix of 20-byte key (same first block)
    QCOMPARE(key16, key20.left(16));
}

void TestPasswordDerivation::testMultiBlockKey()
{
    // Request > 20 bytes (needs multiple HMAC-SHA1 blocks)
    const QByteArray password = "password";
    const QByteArray salt = "salt";

    const QByteArray key32 = PasswordDerivation::deriveKeyPbkdf2(password, salt, 1, 32);
    QCOMPARE(key32.length(), 32);

    // First 20 bytes should match single-block derivation
    const QByteArray key20 = PasswordDerivation::deriveKeyPbkdf2(password, salt, 1, 20);
    QCOMPARE(key32.left(20), key20);
}

void TestPasswordDerivation::testEmptyPassword()
{
    const QByteArray result = PasswordDerivation::deriveKeyPbkdf2(
        QByteArray(), "salt", 1, 20);

    QCOMPARE(result.length(), 20);
    // Empty password should still produce a valid (non-empty) key
    QVERIFY(!result.isEmpty());
}

void TestPasswordDerivation::testEmptySalt()
{
    const QByteArray result = PasswordDerivation::deriveKeyPbkdf2(
        "password", QByteArray(), 1, 20);

    QCOMPARE(result.length(), 20);
    QVERIFY(!result.isEmpty());
}

QTEST_MAIN(TestPasswordDerivation)
#include "test_password_derivation.moc"
