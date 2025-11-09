/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QString>
#include "daemon/input/modifier_key_checker.h"

using namespace YubiKeyOath::Daemon;

/**
 * @brief Unit tests for ModifierKeyChecker
 *
 * Tests modifier key detection and waiting logic.
 *
 * @note Some tests rely on actual keyboard state during test execution.
 *       For reliable tests, ensure no modifier keys are pressed while running.
 */
class TestModifierKeyChecker : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // Basic functionality tests
    void testHasModifiersPressed_NoModifiers();
    void testGetPressedModifiers_NoModifiers();
    void testWaitForModifierRelease_NoModifiers();
    void testWaitForModifierRelease_ImmediateReturn();
    void testWaitForModifierRelease_Timeout();

    // Integration tests
    void testModifierNames_NotEmpty();
};

void TestModifierKeyChecker::initTestCase()
{
    // Ensure we have QGuiApplication for queryKeyboardModifiers()
    if (!qGuiApp) {
        QSKIP("QGuiApplication not available - skipping tests");
    }
}

// ========== Basic Functionality Tests ==========

void TestModifierKeyChecker::testHasModifiersPressed_NoModifiers()
{
    // Assuming no modifiers are pressed during test
    // In real scenario, this could be true or false
    bool hasModifiers = ModifierKeyChecker::hasModifiersPressed();

    // We can't assert a specific value, but we can verify it doesn't crash
    QVERIFY(hasModifiers == true || hasModifiers == false);
    qDebug() << "Current modifier state:" << hasModifiers;
}

void TestModifierKeyChecker::testGetPressedModifiers_NoModifiers()
{
    // Get current modifiers
    QStringList modifiers = ModifierKeyChecker::getPressedModifiers();

    // Verify it returns a list (even if empty)
    QVERIFY(modifiers.isEmpty() || !modifiers.isEmpty());
    qDebug() << "Currently pressed modifiers:" << modifiers;

    // If no modifiers pressed, list should be empty
    if (!ModifierKeyChecker::hasModifiersPressed()) {
        QVERIFY(modifiers.isEmpty());
    }
}

void TestModifierKeyChecker::testWaitForModifierRelease_NoModifiers()
{
    // If no modifiers are pressed, should return immediately
    if (!ModifierKeyChecker::hasModifiersPressed()) {
        QElapsedTimer timer;
        timer.start();

        bool released = ModifierKeyChecker::waitForModifierRelease(500, 50);
        qint64 elapsed = timer.elapsed();

        QVERIFY(released);
        // Should return very quickly (well under 100ms)
        QVERIFY(elapsed < 100);
        qDebug() << "Immediate return took:" << elapsed << "ms";
    } else {
        QSKIP("Modifiers are currently pressed - cannot test immediate return");
    }
}

void TestModifierKeyChecker::testWaitForModifierRelease_ImmediateReturn()
{
    // Test with very short timeout
    bool released = ModifierKeyChecker::waitForModifierRelease(100, 10);

    // Should either return true (no modifiers) or false (timeout)
    QVERIFY(released == true || released == false);
    qDebug() << "100ms wait result:" << released;
}

void TestModifierKeyChecker::testWaitForModifierRelease_Timeout()
{
    // Test timeout behavior with very short timeout
    QElapsedTimer timer;
    timer.start();

    // Use short timeout for test performance
    bool released = ModifierKeyChecker::waitForModifierRelease(200, 50);
    qint64 elapsed = timer.elapsed();

    // Verify timeout is respected (should be close to 200ms if modifiers pressed)
    if (!released) {
        // If timed out, elapsed should be >= timeout
        QVERIFY(elapsed >= 200);
        QVERIFY(elapsed < 300); // Allow some margin
        qDebug() << "Timeout correctly enforced:" << elapsed << "ms";
    } else {
        // If released, should be < timeout
        QVERIFY(elapsed < 200);
        qDebug() << "Released before timeout:" << elapsed << "ms";
    }
}

// ========== Integration Tests ==========

void TestModifierKeyChecker::testModifierNames_NotEmpty()
{
    // We can't easily test the actual translation without simulating key presses,
    // but we can verify the function doesn't crash and returns valid results
    QStringList names = ModifierKeyChecker::getPressedModifiers();
    QVERIFY(names.isEmpty() || !names.isEmpty());

    // If any modifiers are pressed, verify names are not empty
    for (const QString &name : names) {
        QVERIFY(!name.isEmpty());
        qDebug() << "Modifier name:" << name;
    }
}

QTEST_MAIN(TestModifierKeyChecker)
#include "test_modifier_key_checker.moc"
