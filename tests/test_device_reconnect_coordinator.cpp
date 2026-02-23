/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/daemon/infrastructure/device_reconnect_coordinator.h"

#include <QtTest>
#include <QSignalSpy>

using namespace YubiKeyOath::Daemon;
using namespace YubiKeyOath::Shared;

/**
 * @brief Tests for DeviceReconnectCoordinator
 *
 * Verifies reconnection lifecycle, signals, and state management.
 */
class TestDeviceReconnectCoordinator : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    // Initial state
    void testInitialState();

    // Basic reconnection
    void testStartReconnect_EmitsStartedSignal();
    void testSuccessfulReconnect_EmitsCompletedTrue();
    void testFailedReconnect_EmitsCompletedFalse();

    // State tracking
    void testIsReconnecting_DuringReconnect();
    void testCurrentDeviceId_DuringReconnect();

    // Cancel
    void testCancel_StopsReconnect();
    void testCancel_WhenNotReconnecting();

    // No reconnect function set
    void testNoReconnectFunction_EmitsFalse();

    // Multiple reconnects (replaces previous)
    void testMultipleReconnects_CancelsPrevious();

    // Cleanup after reconnect
    void testStateCleared_AfterCompletion();

private:
    std::unique_ptr<DeviceReconnectCoordinator> m_coordinator;
};

void TestDeviceReconnectCoordinator::init()
{
    m_coordinator = std::make_unique<DeviceReconnectCoordinator>();
}

void TestDeviceReconnectCoordinator::testInitialState()
{
    QVERIFY(!m_coordinator->isReconnecting());
    QVERIFY(m_coordinator->currentDeviceId().isEmpty());
}

void TestDeviceReconnectCoordinator::testStartReconnect_EmitsStartedSignal()
{
    QSignalSpy startedSpy(m_coordinator.get(), &DeviceReconnectCoordinator::reconnectStarted);

    m_coordinator->setReconnectFunction([](const QString &) {
        return Result<void>::success();
    });

    m_coordinator->startReconnect("device-1", "Reader 1", QByteArray());

    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(startedSpy.at(0).at(0).toString(), "device-1");
}

void TestDeviceReconnectCoordinator::testSuccessfulReconnect_EmitsCompletedTrue()
{
    QSignalSpy completedSpy(m_coordinator.get(), &DeviceReconnectCoordinator::reconnectCompleted);

    m_coordinator->setReconnectFunction([](const QString &) {
        return Result<void>::success();
    });

    m_coordinator->startReconnect("device-1", "Reader 1", QByteArray());

    // Wait for timer (10ms delay + processing)
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.at(0).at(0).toString(), "device-1");
    QCOMPARE(completedSpy.at(0).at(1).toBool(), true);
}

void TestDeviceReconnectCoordinator::testFailedReconnect_EmitsCompletedFalse()
{
    QSignalSpy completedSpy(m_coordinator.get(), &DeviceReconnectCoordinator::reconnectCompleted);

    m_coordinator->setReconnectFunction([](const QString &) {
        return Result<void>::error("Connection failed");
    });

    m_coordinator->startReconnect("device-1", "Reader 1", QByteArray());

    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.at(0).at(0).toString(), "device-1");
    QCOMPARE(completedSpy.at(0).at(1).toBool(), false);
}

void TestDeviceReconnectCoordinator::testIsReconnecting_DuringReconnect()
{
    m_coordinator->setReconnectFunction([](const QString &) {
        return Result<void>::success();
    });

    m_coordinator->startReconnect("device-1", "Reader 1", QByteArray());

    // Should be reconnecting immediately after start
    QVERIFY(m_coordinator->isReconnecting());
}

void TestDeviceReconnectCoordinator::testCurrentDeviceId_DuringReconnect()
{
    m_coordinator->setReconnectFunction([](const QString &) {
        return Result<void>::success();
    });

    m_coordinator->startReconnect("device-42", "Reader 1", QByteArray());

    QCOMPARE(m_coordinator->currentDeviceId(), "device-42");
}

void TestDeviceReconnectCoordinator::testCancel_StopsReconnect()
{
    QSignalSpy completedSpy(m_coordinator.get(), &DeviceReconnectCoordinator::reconnectCompleted);

    m_coordinator->setReconnectFunction([](const QString &) {
        return Result<void>::success();
    });

    m_coordinator->startReconnect("device-1", "Reader 1", QByteArray());
    QVERIFY(m_coordinator->isReconnecting());

    m_coordinator->cancel();
    QVERIFY(!m_coordinator->isReconnecting());

    // Wait to ensure no completed signal comes
    QTest::qWait(50);
    QCOMPARE(completedSpy.count(), 0);
}

void TestDeviceReconnectCoordinator::testCancel_WhenNotReconnecting()
{
    // Should not crash
    m_coordinator->cancel();
    QVERIFY(!m_coordinator->isReconnecting());
}

void TestDeviceReconnectCoordinator::testNoReconnectFunction_EmitsFalse()
{
    QSignalSpy completedSpy(m_coordinator.get(), &DeviceReconnectCoordinator::reconnectCompleted);

    // Don't set reconnect function
    m_coordinator->startReconnect("device-1", "Reader 1", QByteArray());

    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.at(0).at(1).toBool(), false);
}

void TestDeviceReconnectCoordinator::testMultipleReconnects_CancelsPrevious()
{
    QSignalSpy startedSpy(m_coordinator.get(), &DeviceReconnectCoordinator::reconnectStarted);
    QSignalSpy completedSpy(m_coordinator.get(), &DeviceReconnectCoordinator::reconnectCompleted);

    m_coordinator->setReconnectFunction([](const QString &) {
        return Result<void>::success();
    });

    // Start first reconnect
    m_coordinator->startReconnect("device-1", "Reader 1", QByteArray());
    // Start second reconnect immediately (cancels first)
    m_coordinator->startReconnect("device-2", "Reader 2", QByteArray());

    // Second started signal emitted
    QCOMPARE(startedSpy.count(), 2);
    QCOMPARE(startedSpy.at(1).at(0).toString(), "device-2");

    // Wait for completion - only device-2 should complete
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.at(0).at(0).toString(), "device-2");
}

void TestDeviceReconnectCoordinator::testStateCleared_AfterCompletion()
{
    QSignalSpy completedSpy(m_coordinator.get(), &DeviceReconnectCoordinator::reconnectCompleted);

    m_coordinator->setReconnectFunction([](const QString &) {
        return Result<void>::success();
    });

    m_coordinator->startReconnect("device-1", "Reader 1", QByteArray());

    QTRY_COMPARE(completedSpy.count(), 1);

    // State should be cleared
    QVERIFY(!m_coordinator->isReconnecting());
    QVERIFY(m_coordinator->currentDeviceId().isEmpty());
}

QTEST_MAIN(TestDeviceReconnectCoordinator)
#include "test_device_reconnect_coordinator.moc"
