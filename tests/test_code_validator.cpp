/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "daemon/formatting/code_validator.h"

using namespace KRunner::YubiKey;

/**
 * @brief Unit tests for CodeValidator
 *
 * Tests TOTP code validity calculations and expiration time logic.
 */
class TestCodeValidator : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // calculateCodeValidity() tests
    void testCalculateCodeValidity_Range();
    void testCalculateCodeValidity_Consistency();

    // calculateExpirationTime() tests
    void testCalculateExpirationTime_StartOfPeriod();
    void testCalculateExpirationTime_MiddleOfPeriod();
    void testCalculateExpirationTime_EndOfPeriod();
    void testCalculateExpirationTime_MultipleSeconds();

    // Edge cases
    void testExpirationTime_Epoch();
    void testExpirationTime_FarFuture();
};

// ========== calculateCodeValidity() Tests ==========

void TestCodeValidator::testCalculateCodeValidity_Range()
{
    // Test that validity is always in valid range (1-30 seconds)
    // Note: This is time-dependent, so we just verify the range
    int validity = CodeValidator::calculateCodeValidity();

    QVERIFY(validity >= 1);
    QVERIFY(validity <= 30);
}

void TestCodeValidator::testCalculateCodeValidity_Consistency()
{
    // Test that calling multiple times within same second gives same result
    int validity1 = CodeValidator::calculateCodeValidity();
    int validity2 = CodeValidator::calculateCodeValidity();

    // Should be same or at most 1 second apart if we crossed a second boundary
    QVERIFY(qAbs(validity1 - validity2) <= 1);
}

// ========== calculateExpirationTime() Tests ==========

void TestCodeValidator::testCalculateExpirationTime_StartOfPeriod()
{
    // Test at exact start of 30-second period
    // Epoch time 0 is start of a period (0 % 30 == 0)
    QDateTime startTime = QDateTime::fromSecsSinceEpoch(0);
    QDateTime expiration = CodeValidator::calculateExpirationTime(startTime);

    // At start of period, remaining = 30, so expiration = start + 30
    QCOMPARE(expiration.toSecsSinceEpoch(), 30);

    // Test at 60 seconds (another period start)
    QDateTime start60 = QDateTime::fromSecsSinceEpoch(60);
    QDateTime expiration60 = CodeValidator::calculateExpirationTime(start60);
    QCOMPARE(expiration60.toSecsSinceEpoch(), 90); // 60 + 30
}

void TestCodeValidator::testCalculateExpirationTime_MiddleOfPeriod()
{
    // Test at middle of period (15 seconds into period)
    // 15 % 30 = 15, remaining = 30 - 15 = 15
    QDateTime midTime = QDateTime::fromSecsSinceEpoch(15);
    QDateTime expiration = CodeValidator::calculateExpirationTime(midTime);

    QCOMPARE(expiration.toSecsSinceEpoch(), 30); // 15 + 15

    // Test at 45 seconds (15 seconds into second period)
    QDateTime mid45 = QDateTime::fromSecsSinceEpoch(45);
    QDateTime expiration45 = CodeValidator::calculateExpirationTime(mid45);
    QCOMPARE(expiration45.toSecsSinceEpoch(), 60); // 45 + 15
}

void TestCodeValidator::testCalculateExpirationTime_EndOfPeriod()
{
    // Test 1 second before end of period
    // 29 % 30 = 29, remaining = 30 - 29 = 1
    QDateTime endTime = QDateTime::fromSecsSinceEpoch(29);
    QDateTime expiration = CodeValidator::calculateExpirationTime(endTime);

    QCOMPARE(expiration.toSecsSinceEpoch(), 30); // 29 + 1

    // Test at 59 seconds (1 second before end of second period)
    QDateTime end59 = QDateTime::fromSecsSinceEpoch(59);
    QDateTime expiration59 = CodeValidator::calculateExpirationTime(end59);
    QCOMPARE(expiration59.toSecsSinceEpoch(), 60); // 59 + 1
}

void TestCodeValidator::testCalculateExpirationTime_MultipleSeconds()
{
    // Test various seconds within a period to ensure correct calculation
    struct TestCase {
        qint64 inputSeconds;
        qint64 expectedExpiration;
    };

    QVector<TestCase> testCases = {
        {0, 30},    // Start of period
        {1, 30},    // 1 second in
        {10, 30},   // 10 seconds in
        {20, 30},   // 20 seconds in
        {29, 30},   // Last second of period
        {30, 60},   // Start of second period
        {31, 60},   // 1 second into second period
        {55, 60},   // 25 seconds into second period
        {60, 90},   // Start of third period
        {100, 120}, // 10 seconds into 4th period (100 % 30 = 10, remaining = 20)
        {1000, 1020}, // 10 seconds into period at 1000 (1000 % 30 = 10)
    };

    for (const auto &testCase : testCases) {
        QDateTime input = QDateTime::fromSecsSinceEpoch(testCase.inputSeconds);
        QDateTime expiration = CodeValidator::calculateExpirationTime(input);

        QCOMPARE(expiration.toSecsSinceEpoch(), testCase.expectedExpiration);
    }
}

// ========== Edge Cases ==========

void TestCodeValidator::testExpirationTime_Epoch()
{
    // Test at Unix epoch (1970-01-01 00:00:00)
    QDateTime epoch = QDateTime::fromSecsSinceEpoch(0);
    QDateTime expiration = CodeValidator::calculateExpirationTime(epoch);

    QVERIFY(expiration.isValid());
    QVERIFY(expiration > epoch);
    QCOMPARE(expiration.toSecsSinceEpoch() - epoch.toSecsSinceEpoch(), 30);
}

void TestCodeValidator::testExpirationTime_FarFuture()
{
    // Test with far future date to ensure no overflow issues
    // Year 2100: approximately 4.1 billion seconds since epoch
    qint64 farFuture = 4102444800; // 2100-01-01
    QDateTime futureTime = QDateTime::fromSecsSinceEpoch(farFuture);
    QDateTime expiration = CodeValidator::calculateExpirationTime(futureTime);

    QVERIFY(expiration.isValid());
    QVERIFY(expiration > futureTime);

    // Calculate expected expiration
    int offset = farFuture % 30;
    int remaining = 30 - offset;
    qint64 expectedExpiration = farFuture + remaining;

    QCOMPARE(expiration.toSecsSinceEpoch(), expectedExpiration);
}

QTEST_MAIN(TestCodeValidator)
#include "test_code_validator.moc"
