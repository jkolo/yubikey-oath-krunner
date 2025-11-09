/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/config/relative_time_formatter.h"

#include <QtTest>
#include <QDateTime>

using namespace YubiKeyOath::Config;

/**
 * @brief Tests for RelativeTimeFormatter utility class
 *
 * Verifies relative time string formatting for various time ranges.
 */
class TestRelativeTimeFormatter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Core functionality tests
    void testFormatRelativeTime_JustNow();
    void testFormatRelativeTime_MinutesAgo_Singular();
    void testFormatRelativeTime_MinutesAgo_Plural();
    void testFormatRelativeTime_HoursAgo_Singular();
    void testFormatRelativeTime_HoursAgo_Plural();
    void testFormatRelativeTime_Yesterday();
    void testFormatRelativeTime_DaysAgo_Singular();
    void testFormatRelativeTime_DaysAgo_Plural();
    void testFormatRelativeTime_WeeksAgo_Singular();
    void testFormatRelativeTime_WeeksAgo_Plural();
    void testFormatRelativeTime_MonthsAgo_Singular();
    void testFormatRelativeTime_MonthsAgo_Plural();
    void testFormatRelativeTime_YearOrMore();

    // Edge cases
    void testFormatRelativeTime_ExactBoundaries();
    void testFormatRelativeTime_NullDateTime();
    void testFormatRelativeTime_FutureDateTime();

private:
    QDateTime m_now;
};

void TestRelativeTimeFormatter::initTestCase()
{
    m_now = QDateTime::currentDateTime();
}

void TestRelativeTimeFormatter::cleanupTestCase()
{
}

// --- Core Functionality Tests ---

void TestRelativeTimeFormatter::testFormatRelativeTime_JustNow()
{
    // Test: < 1 minute ago → "just now"
    QDateTime time = m_now.addSecs(-30);  // 30 seconds ago
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QCOMPARE(result, QStringLiteral("just now"));

    // Boundary: 59 seconds
    time = m_now.addSecs(-59);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QCOMPARE(result, QStringLiteral("just now"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_MinutesAgo_Singular()
{
    // Test: exactly 1 minute ago
    QDateTime time = m_now.addSecs(-60);  // 1 minute
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("1"));
    QVERIFY(result.contains("minute"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_MinutesAgo_Plural()
{
    // Test: 2-59 minutes ago
    QDateTime time = m_now.addSecs(-120);  // 2 minutes
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("2"));
    QVERIFY(result.contains("minute"));

    // 30 minutes
    time = m_now.addSecs(-30 * 60);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("30"));
    QVERIFY(result.contains("minute"));

    // 59 minutes (boundary)
    time = m_now.addSecs(-59 * 60);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("59"));
    QVERIFY(result.contains("minute"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_HoursAgo_Singular()
{
    // Test: exactly 1 hour ago
    QDateTime time = m_now.addSecs(-60 * 60);  // 1 hour
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("1"));
    QVERIFY(result.contains("hour"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_HoursAgo_Plural()
{
    // Test: 2-23 hours ago
    QDateTime time = m_now.addSecs(-2 * 60 * 60);  // 2 hours
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("2"));
    QVERIFY(result.contains("hour"));

    // 12 hours
    time = m_now.addSecs(-12 * 60 * 60);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("12"));
    QVERIFY(result.contains("hour"));

    // 23 hours (boundary)
    time = m_now.addSecs(-23 * 60 * 60);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("23"));
    QVERIFY(result.contains("hour"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_Yesterday()
{
    // Test: ~24 hours ago → "yesterday"
    QDateTime time = m_now.addDays(-1);
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QCOMPARE(result, QStringLiteral("yesterday"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_DaysAgo_Singular()
{
    // Test: < 2 days but > yesterday range
    // This might show "1 day ago" depending on implementation
    QDateTime time = m_now.addDays(-1).addSecs(-60 * 60);  // 1 day + 1 hour
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    // Accept either "yesterday" or "1 day ago" - depends on implementation
    QVERIFY(result.contains("yesterday") || (result.contains("1") && result.contains("day")));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_DaysAgo_Plural()
{
    // Test: 2-6 days ago
    QDateTime time = m_now.addDays(-2);
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("2"));
    QVERIFY(result.contains("day"));

    // 6 days (boundary)
    time = m_now.addDays(-6);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("6"));
    QVERIFY(result.contains("day"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_WeeksAgo_Singular()
{
    // Test: exactly 1 week ago
    QDateTime time = m_now.addDays(-7);
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("1"));
    QVERIFY(result.contains("week"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_WeeksAgo_Plural()
{
    // Test: 2-3 weeks ago
    QDateTime time = m_now.addDays(-14);  // 2 weeks
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("2"));
    QVERIFY(result.contains("week"));

    // 3 weeks
    time = m_now.addDays(-21);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("3"));
    QVERIFY(result.contains("week"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_MonthsAgo_Singular()
{
    // Test: ~1 month ago (30 days)
    QDateTime time = m_now.addDays(-30);
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("1"));
    QVERIFY(result.contains("month"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_MonthsAgo_Plural()
{
    // Test: 2-11 months ago
    QDateTime time = m_now.addDays(-60);  // ~2 months
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("2"));
    QVERIFY(result.contains("month"));

    // ~6 months
    time = m_now.addDays(-180);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("6"));
    QVERIFY(result.contains("month"));

    // ~11 months (boundary)
    time = m_now.addDays(-330);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result.contains("month"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_YearOrMore()
{
    // Test: >= 12 months → "yyyy-MM-dd" format
    QDateTime time = m_now.addDays(-365);  // 1 year
    QString result = RelativeTimeFormatter::formatRelativeTime(time);

    // Should be formatted as date: yyyy-MM-dd
    QVERIFY(result.contains("-"));  // Date separator
    QCOMPARE(result.length(), 10);  // "yyyy-MM-dd" is 10 characters

    // Verify it's a valid date format
    QDate parsedDate = QDate::fromString(result, QStringLiteral("yyyy-MM-dd"));
    QVERIFY(parsedDate.isValid());
}

// --- Edge Cases ---

void TestRelativeTimeFormatter::testFormatRelativeTime_ExactBoundaries()
{
    // Test exact boundary transitions

    // 60 seconds = 1 minute (should be "1 minute ago", not "just now")
    QDateTime time = m_now.addSecs(-60);
    QString result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(result != QStringLiteral("just now"));
    QVERIFY(result.contains("minute"));

    // 3600 seconds = 1 hour (should be "1 hour ago", not minutes)
    time = m_now.addSecs(-3600);
    result = RelativeTimeFormatter::formatRelativeTime(time);
    QVERIFY(!result.contains("minute"));
    QVERIFY(result.contains("hour"));
}

void TestRelativeTimeFormatter::testFormatRelativeTime_NullDateTime()
{
    // Test: null/invalid QDateTime
    QDateTime invalidTime;
    QString result = RelativeTimeFormatter::formatRelativeTime(invalidTime);

    // Should handle gracefully (return empty string or "unknown")
    // Implementation-dependent, but shouldn't crash
    QVERIFY(!result.isNull());
}

void TestRelativeTimeFormatter::testFormatRelativeTime_FutureDateTime()
{
    // Test: future datetime (edge case - shouldn't happen but test defensive coding)
    QDateTime futureTime = m_now.addDays(7);
    QString result = RelativeTimeFormatter::formatRelativeTime(futureTime);

    // Should handle gracefully - might show "just now" or format as date
    QVERIFY(!result.isEmpty());
}

QTEST_GUILESS_MAIN(TestRelativeTimeFormatter)
#include "test_relative_time_formatter.moc"
