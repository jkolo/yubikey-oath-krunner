/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/daemon/workflows/notification_utils.h"
#include "../src/daemon/workflows/notification_helper.h"

#include <QtTest>
#include <QDateTime>

using namespace YubiKeyOath::Daemon;

/**
 * @brief Tests for NotificationUtils and NotificationHelper
 *
 * Verifies notification hints creation and timer progress calculations.
 */
class TestNotificationUtils : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // NotificationUrgency constants
    void testUrgencyConstants();

    // createNotificationHints
    void testHints_DefaultValues();
    void testHints_CriticalUrgency();
    void testHints_WithIcon();
    void testHints_WithoutIcon();
    void testHints_UrgencyIsByte();
    void testHints_ProgressValue();

    // TimerProgress calculation
    void testTimerProgress_NotExpired();
    void testTimerProgress_Expired();
    void testTimerProgress_JustExpired();
    void testTimerProgress_HalfwayDone();
    void testTimerProgress_FullTime();
};

void TestNotificationUtils::testUrgencyConstants()
{
    QCOMPARE(NotificationUrgency::Low, static_cast<uchar>(0));
    QCOMPARE(NotificationUrgency::Normal, static_cast<uchar>(1));
    QCOMPARE(NotificationUrgency::Critical, static_cast<uchar>(2));
}

void TestNotificationUtils::testHints_DefaultValues()
{
    const QVariantMap hints = NotificationUtils::createNotificationHints();

    QVERIFY(hints.contains("urgency"));
    QVERIFY(hints.contains("value"));
    QCOMPARE(hints["value"].toInt(), 100);
}

void TestNotificationUtils::testHints_CriticalUrgency()
{
    const QVariantMap hints = NotificationUtils::createNotificationHints(
        NotificationUrgency::Critical);

    QCOMPARE(hints["urgency"].value<uchar>(), NotificationUrgency::Critical);
}

void TestNotificationUtils::testHints_WithIcon()
{
    const QVariantMap hints = NotificationUtils::createNotificationHints(
        NotificationUrgency::Normal, 100, "yubikey-5c-nfc");

    QVERIFY(hints.contains("image-path"));
    QCOMPARE(hints["image-path"].toString(), "yubikey-5c-nfc");
}

void TestNotificationUtils::testHints_WithoutIcon()
{
    const QVariantMap hints = NotificationUtils::createNotificationHints(
        NotificationUrgency::Normal, 100);

    QVERIFY(!hints.contains("image-path"));
}

void TestNotificationUtils::testHints_UrgencyIsByte()
{
    const QVariantMap hints = NotificationUtils::createNotificationHints(
        NotificationUrgency::Normal);

    // CRITICAL: urgency must be byte type for D-Bus compatibility
    const QVariant urgencyVar = hints["urgency"];
    QCOMPARE(urgencyVar.typeId(), QMetaType::UChar);
}

void TestNotificationUtils::testHints_ProgressValue()
{
    const QVariantMap hints50 = NotificationUtils::createNotificationHints(
        NotificationUrgency::Normal, 50);
    QCOMPARE(hints50["value"].toInt(), 50);

    const QVariantMap hints0 = NotificationUtils::createNotificationHints(
        NotificationUrgency::Normal, 0);
    QCOMPARE(hints0["value"].toInt(), 0);
}

void TestNotificationUtils::testTimerProgress_NotExpired()
{
    // Expires 20 seconds from now, total 30 seconds
    const QDateTime expiration = QDateTime::currentDateTimeUtc().addSecs(20);
    const auto progress = NotificationHelper::calculateTimerProgress(expiration, 30);

    QVERIFY(!progress.expired);
    QVERIFY(progress.remainingSeconds > 0);
    QVERIFY(progress.remainingSeconds <= 20);
    QCOMPARE(progress.totalSeconds, 30);
    QVERIFY(progress.progressPercent > 0);
    QVERIFY(progress.progressPercent <= 100);
}

void TestNotificationUtils::testTimerProgress_Expired()
{
    // Expired 10 seconds ago
    const QDateTime expiration = QDateTime::currentDateTimeUtc().addSecs(-10);
    const auto progress = NotificationHelper::calculateTimerProgress(expiration, 30);

    QVERIFY(progress.expired);
    QCOMPARE(progress.remainingSeconds, 0);
    QCOMPARE(progress.progressPercent, 0);
}

void TestNotificationUtils::testTimerProgress_JustExpired()
{
    // Expires right now (0 seconds remaining)
    const QDateTime expiration = QDateTime::currentDateTimeUtc();
    const auto progress = NotificationHelper::calculateTimerProgress(expiration, 30);

    // Should be expired (remaining <= 0)
    QVERIFY(progress.expired);
    QCOMPARE(progress.remainingSeconds, 0);
}

void TestNotificationUtils::testTimerProgress_HalfwayDone()
{
    // 15 seconds remaining of 30 total
    const QDateTime expiration = QDateTime::currentDateTimeUtc().addSecs(15);
    const auto progress = NotificationHelper::calculateTimerProgress(expiration, 30);

    QVERIFY(!progress.expired);
    // Progress should be approximately 50% (±5% for timing tolerance)
    QVERIFY(progress.progressPercent >= 45);
    QVERIFY(progress.progressPercent <= 55);
}

void TestNotificationUtils::testTimerProgress_FullTime()
{
    // Full 30 seconds remaining of 30 total
    const QDateTime expiration = QDateTime::currentDateTimeUtc().addSecs(30);
    const auto progress = NotificationHelper::calculateTimerProgress(expiration, 30);

    QVERIFY(!progress.expired);
    // Progress should be approximately 100%
    QVERIFY(progress.progressPercent >= 95);
    QVERIFY(progress.progressPercent <= 100);
}

QTEST_MAIN(TestNotificationUtils)
#include "test_notification_utils.moc"
