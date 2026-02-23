/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/daemon/utils/secure_logging.h"

#include <QtTest>

using namespace YubiKeyOath::Daemon;

/**
 * @brief Tests for SecureLogging utility functions
 *
 * Verifies that sensitive data is properly masked in log output.
 */
class TestSecureLogging : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // safeByteInfo
    void testSafeByteInfo_Empty();
    void testSafeByteInfo_NonEmpty();

    // maskSerial (quint32)
    void testMaskSerialInt_Zero();
    void testMaskSerialInt_ShortNumber();
    void testMaskSerialInt_LongNumber();

    // maskSerial (QString)
    void testMaskSerialString_Empty();
    void testMaskSerialString_Short();
    void testMaskSerialString_Long();

    // maskCredentialName
    void testMaskCredentialName_Empty();
    void testMaskCredentialName_WithIssuer();
    void testMaskCredentialName_NoIssuer_Short();
    void testMaskCredentialName_NoIssuer_Long();

    // apduDescription
    void testApduDescription_KnownInstructions();
    void testApduDescription_UnknownInstruction();

    // safeApduInfo
    void testSafeApduInfo_ValidApdu();
    void testSafeApduInfo_TooShort();

    // swDescription
    void testSwDescription_KnownCodes();
    void testSwDescription_UnknownCode();
};

void TestSecureLogging::testSafeByteInfo_Empty()
{
    QCOMPARE(SecureLogging::safeByteInfo(QByteArray()), "[0 bytes]");
}

void TestSecureLogging::testSafeByteInfo_NonEmpty()
{
    QByteArray data(16, 'x');
    const QString result = SecureLogging::safeByteInfo(data);
    QCOMPARE(result, "[16 bytes]");
    // Must NOT contain the actual data
    QVERIFY(!result.contains("x"));
}

void TestSecureLogging::testMaskSerialInt_Zero()
{
    QCOMPARE(SecureLogging::maskSerial(static_cast<quint32>(0)), "(none)");
}

void TestSecureLogging::testMaskSerialInt_ShortNumber()
{
    // <= 4 digits shown as-is
    QCOMPARE(SecureLogging::maskSerial(static_cast<quint32>(1234)), "1234");
}

void TestSecureLogging::testMaskSerialInt_LongNumber()
{
    // > 4 digits masked
    const QString result = SecureLogging::maskSerial(static_cast<quint32>(12345678));
    QCOMPARE(result, "****5678");
}

void TestSecureLogging::testMaskSerialString_Empty()
{
    QCOMPARE(SecureLogging::maskSerial(QString()), "(none)");
}

void TestSecureLogging::testMaskSerialString_Short()
{
    QCOMPARE(SecureLogging::maskSerial(QStringLiteral("1234")), "1234");
}

void TestSecureLogging::testMaskSerialString_Long()
{
    QCOMPARE(SecureLogging::maskSerial(QStringLiteral("12345678")), "****5678");
}

void TestSecureLogging::testMaskCredentialName_Empty()
{
    QCOMPARE(SecureLogging::maskCredentialName(QString()), "(empty)");
}

void TestSecureLogging::testMaskCredentialName_WithIssuer()
{
    const QString result = SecureLogging::maskCredentialName("GitHub:user@example.com");
    QCOMPARE(result, "GitHub:****");
    // Must not expose account
    QVERIFY(!result.contains("user"));
    QVERIFY(!result.contains("example"));
}

void TestSecureLogging::testMaskCredentialName_NoIssuer_Short()
{
    // <= 4 chars shown as-is (no way to meaningfully mask)
    QCOMPARE(SecureLogging::maskCredentialName("test"), "test");
}

void TestSecureLogging::testMaskCredentialName_NoIssuer_Long()
{
    const QString result = SecureLogging::maskCredentialName("mysecretaccount");
    // Shows first 2 chars + ****
    QCOMPARE(result, "my****");
}

void TestSecureLogging::testApduDescription_KnownInstructions()
{
    QCOMPARE(SecureLogging::apduDescription(0xA1), "LIST");
    QCOMPARE(SecureLogging::apduDescription(0xA2), "CALCULATE");
    QCOMPARE(SecureLogging::apduDescription(0xA3), "VALIDATE");
    QCOMPARE(SecureLogging::apduDescription(0xA5), "SEND_REMAINING");
    QCOMPARE(SecureLogging::apduDescription(0x01), "PUT");
    QCOMPARE(SecureLogging::apduDescription(0x02), "DELETE");
    QCOMPARE(SecureLogging::apduDescription(0x03), "SET_CODE");
    QCOMPARE(SecureLogging::apduDescription(0x04), "RESET");
}

void TestSecureLogging::testApduDescription_UnknownInstruction()
{
    const QString result = SecureLogging::apduDescription(0xFF);
    QVERIFY(result.startsWith("CMD_0x"));
}

void TestSecureLogging::testSafeApduInfo_ValidApdu()
{
    // CLA=00, INS=A1 (LIST), P1=00, P2=00
    QByteArray apdu = QByteArray::fromHex("00A10000");
    const QString result = SecureLogging::safeApduInfo(apdu);
    QVERIFY(result.contains("LIST"));
    QVERIFY(result.contains("4 bytes"));
    // Must NOT contain hex dump
    QVERIFY(!result.contains("00a1"));
}

void TestSecureLogging::testSafeApduInfo_TooShort()
{
    QByteArray apdu = QByteArray::fromHex("00A1");
    const QString result = SecureLogging::safeApduInfo(apdu);
    QVERIFY(result.contains("invalid"));
}

void TestSecureLogging::testSwDescription_KnownCodes()
{
    QCOMPARE(SecureLogging::swDescription(0x9000), "SUCCESS");
    QCOMPARE(SecureLogging::swDescription(0x6985), "TOUCH_REQUIRED");
    QCOMPARE(SecureLogging::swDescription(0x6982), "AUTH_REQUIRED");
    QCOMPARE(SecureLogging::swDescription(0x6984), "WRONG_PASSWORD");
    QCOMPARE(SecureLogging::swDescription(0x6A80), "INVALID_DATA");
    QCOMPARE(SecureLogging::swDescription(0x6A82), "NOT_FOUND");
    QCOMPARE(SecureLogging::swDescription(0x6A84), "NO_SPACE");
}

void TestSecureLogging::testSwDescription_UnknownCode()
{
    const QString result = SecureLogging::swDescription(0x1234);
    QVERIFY(result.startsWith("SW_0x"));
    QVERIFY(result.contains("1234"));
}

QTEST_MAIN(TestSecureLogging)
#include "test_secure_logging.moc"
