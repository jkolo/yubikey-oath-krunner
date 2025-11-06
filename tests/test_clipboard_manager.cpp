/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QTest>
#include <QSignalSpy>
#include <QClipboard>
#include <QMimeData>
#include <KSystemClipboard>
#include "../src/daemon/clipboard/clipboard_manager.h"

using namespace YubiKeyOath::Daemon;

/**
 * @brief Integration tests for ClipboardManager
 *
 * Tests clipboard operations including:
 * - Basic copy operations
 * - MIME data hints (x-kde-passwordManagerHint)
 * - Auto-clear timer
 * - Selective clear (user changed clipboard)
 * - State management
 *
 * Note: These are integration tests using real KSystemClipboard.
 * They interact with the actual system clipboard but clean up after themselves.
 */
class TestClipboardManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    // Basic copy operations
    void testCopyToClipboard_Success();
    void testCopyToClipboard_EmptyString();
    void testCopyToClipboard_SpecialCharacters();
    void testCopyToClipboard_Unicode();
    void testCopyToClipboard_LongText();

    // MIME data hints
    void testCopyToClipboard_AddsPasswordManagerHint();
    void testPasswordManagerHint_ValueIsSecret();

    // Verification
    void testCopyToClipboard_VerifiesClipboardContent();

    // Auto-clear functionality
    void testAutoClear_NoTimeout_TimerNotStarted();
    void testAutoClear_WithTimeout_TimerStarted();
    void testAutoClear_TimerFires_ClearsClipboard();
    void testAutoClear_MultipleCopies_RestartsTimer();

    // Manual clear operations
    void testClearClipboard_ClearsWhenContentMatches();
    void testClearClipboard_DoesNotClearWhenUserChangedContent();
    void testClearClipboard_StopsTimer();
    void testClearClipboard_ClearsState();
    void testClearClipboard_MultipleCallsSafe();

    // State management
    void testStateManagement_LastCopiedTextUpdated();
    void testStateManagement_ClearResetsState();

    // Timer management
    void testTimerManagement_StartedOnCopyWithTimeout();
    void testTimerManagement_NotStartedOnCopyWithoutTimeout();
    void testTimerManagement_StoppedOnManualClear();
    void testTimerManagement_StoppedOnAutoClear();

private:
    ClipboardManager *m_manager = nullptr;
    QString m_originalClipboardContent;  // For restoration
};

void TestClipboardManager::initTestCase()
{
    qDebug() << "=== TestClipboardManager: Starting test suite ===";
}

void TestClipboardManager::init()
{
    // Save original clipboard content
    if (KSystemClipboard::instance()) {
        m_originalClipboardContent = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    }

    // Create clipboard manager
    m_manager = new ClipboardManager(this);
}

void TestClipboardManager::cleanup()
{
    delete m_manager;
    m_manager = nullptr;

    // Restore original clipboard content
    if (KSystemClipboard::instance() && !m_originalClipboardContent.isNull()) {
        auto *mimeData = new QMimeData();
        mimeData->setText(m_originalClipboardContent);
        KSystemClipboard::instance()->setMimeData(mimeData, QClipboard::Clipboard);
    }
}

void TestClipboardManager::cleanupTestCase()
{
    qDebug() << "=== TestClipboardManager: Test suite completed ===";
}

void TestClipboardManager::testCopyToClipboard_Success()
{
    // Arrange
    const QString testText = QStringLiteral("123456");

    // Act
    bool result = m_manager->copyToClipboard(testText, 0);

    // Assert
    QVERIFY(result);

    // Verify clipboard contains the text
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, testText);
}

void TestClipboardManager::testCopyToClipboard_EmptyString()
{
    // Arrange
    const QString emptyText;

    // Act
    bool result = m_manager->copyToClipboard(emptyText, 0);

    // Assert
    QVERIFY(result);  // Empty string is valid

    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, emptyText);
}

void TestClipboardManager::testCopyToClipboard_SpecialCharacters()
{
    // Arrange
    const QString specialText = QStringLiteral("!@#$%^&*()_+-={}[]|\\:;\"'<>,.?/~`");

    // Act
    bool result = m_manager->copyToClipboard(specialText, 0);

    // Assert
    QVERIFY(result);

    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, specialText);
}

void TestClipboardManager::testCopyToClipboard_Unicode()
{
    // Arrange
    const QString unicodeText = QStringLiteral("Hello 世界 🔑 Ñoño");

    // Act
    bool result = m_manager->copyToClipboard(unicodeText, 0);

    // Assert
    QVERIFY(result);

    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, unicodeText);
}

void TestClipboardManager::testCopyToClipboard_LongText()
{
    // Arrange
    QString longText;
    for (int i = 0; i < 100; ++i) {
        longText += QStringLiteral("1234567890");
    }
    // 1000 characters

    // Act
    bool result = m_manager->copyToClipboard(longText, 0);

    // Assert
    QVERIFY(result);

    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, longText);
}

void TestClipboardManager::testCopyToClipboard_AddsPasswordManagerHint()
{
    // Arrange
    const QString testText = QStringLiteral("secret123");

    // Act
    m_manager->copyToClipboard(testText, 0);

    // Assert - check MIME data contains password manager hint
    const QMimeData *mimeData = KSystemClipboard::instance()->mimeData(QClipboard::Clipboard);
    QVERIFY(mimeData != nullptr);

    // Check if x-kde-passwordManagerHint is present
    QVERIFY(mimeData->hasFormat(QStringLiteral("x-kde-passwordManagerHint")));
}

void TestClipboardManager::testPasswordManagerHint_ValueIsSecret()
{
    // Arrange
    const QString testText = QStringLiteral("password");

    // Act
    m_manager->copyToClipboard(testText, 0);

    // Assert - check hint value
    const QMimeData *mimeData = KSystemClipboard::instance()->mimeData(QClipboard::Clipboard);
    QVERIFY(mimeData != nullptr);

    const QByteArray hint = mimeData->data(QStringLiteral("x-kde-passwordManagerHint"));
    QCOMPARE(hint, QByteArray("secret"));
}

void TestClipboardManager::testCopyToClipboard_VerifiesClipboardContent()
{
    // Arrange
    const QString testText = QStringLiteral("verify123");

    // Act
    bool result = m_manager->copyToClipboard(testText, 0);

    // Assert - copyToClipboard should verify and return true
    QVERIFY(result);

    // Content should match
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, testText);
}

void TestClipboardManager::testAutoClear_NoTimeout_TimerNotStarted()
{
    // Arrange
    const QString testText = QStringLiteral("noclear");

    // Act - copy with clearAfterSeconds = 0
    m_manager->copyToClipboard(testText, 0);

    // Wait a bit to ensure timer doesn't fire
    QTest::qWait(200);

    // Assert - clipboard should still contain text
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, testText);
}

void TestClipboardManager::testAutoClear_WithTimeout_TimerStarted()
{
    // Arrange
    const QString testText = QStringLiteral("autoclear");

    // Act - copy with 2 second timeout
    m_manager->copyToClipboard(testText, 2);

    // Assert - clipboard contains text initially
    QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, testText);

    // Wait for timer to fire (2 seconds + margin)
    QTest::qWait(2500);

    // Assert - clipboard should be cleared
    clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QVERIFY(clipboardText != testText);  // Should be cleared
}

void TestClipboardManager::testAutoClear_TimerFires_ClearsClipboard()
{
    // Arrange
    const QString testText = QStringLiteral("timerclear");

    // Act
    m_manager->copyToClipboard(testText, 1);  // 1 second timeout

    // Verify initial state
    QCOMPARE(KSystemClipboard::instance()->text(QClipboard::Clipboard), testText);

    // Wait for timer
    QTest::qWait(1500);

    // Assert - cleared
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QVERIFY(clipboardText.isEmpty() || clipboardText != testText);
}

void TestClipboardManager::testAutoClear_MultipleCopies_RestartsTimer()
{
    // Arrange
    const QString text1 = QStringLiteral("first");
    const QString text2 = QStringLiteral("second");

    // Act - copy first text with 3 second timeout
    m_manager->copyToClipboard(text1, 3);

    // Wait 1 second
    QTest::qWait(1000);

    // Copy second text with 3 second timeout (should restart timer)
    m_manager->copyToClipboard(text2, 3);

    // Wait another 2 seconds (total 3 since second copy)
    QTest::qWait(2000);

    // Assert - second text should still be present (timer restarted)
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, text2);
}

void TestClipboardManager::testClearClipboard_ClearsWhenContentMatches()
{
    // Arrange
    const QString testText = QStringLiteral("toclear");
    m_manager->copyToClipboard(testText, 0);

    // Verify it's in clipboard
    QCOMPARE(KSystemClipboard::instance()->text(QClipboard::Clipboard), testText);

    // Act
    m_manager->clearClipboard();

    // Assert - clipboard should be empty
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QVERIFY(clipboardText.isEmpty());
}

void TestClipboardManager::testClearClipboard_DoesNotClearWhenUserChangedContent()
{
    // Arrange
    const QString ourText = QStringLiteral("ourtext");
    const QString userText = QStringLiteral("usertext");

    m_manager->copyToClipboard(ourText, 0);

    // Simulate user changing clipboard
    auto *mimeData = new QMimeData();
    mimeData->setText(userText);
    KSystemClipboard::instance()->setMimeData(mimeData, QClipboard::Clipboard);

    // Act
    m_manager->clearClipboard();

    // Assert - user's text should still be there
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, userText);  // User's content preserved
}

void TestClipboardManager::testClearClipboard_StopsTimer()
{
    // Arrange
    const QString testText = QStringLiteral("timertest");
    m_manager->copyToClipboard(testText, 10);  // 10 second timeout

    // Act - clear immediately
    m_manager->clearClipboard();

    // Wait to ensure timer doesn't fire
    QTest::qWait(500);

    // Assert - clipboard should be empty and stay empty
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QVERIFY(clipboardText.isEmpty());
}

void TestClipboardManager::testClearClipboard_ClearsState()
{
    // Arrange
    const QString testText = QStringLiteral("state");
    m_manager->copyToClipboard(testText, 0);

    // Act
    m_manager->clearClipboard();

    // Assert - copy new text and clear should work
    const QString newText = QStringLiteral("newstate");
    m_manager->copyToClipboard(newText, 0);
    m_manager->clearClipboard();

    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QVERIFY(clipboardText.isEmpty());
}

void TestClipboardManager::testClearClipboard_MultipleCallsSafe()
{
    // Arrange
    const QString testText = QStringLiteral("multiple");
    m_manager->copyToClipboard(testText, 0);

    // Act - call clear multiple times
    m_manager->clearClipboard();
    m_manager->clearClipboard();
    m_manager->clearClipboard();

    // Assert - should not crash, clipboard should be empty
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QVERIFY(clipboardText.isEmpty());
}

void TestClipboardManager::testStateManagement_LastCopiedTextUpdated()
{
    // Arrange & Act
    const QString text1 = QStringLiteral("first");
    const QString text2 = QStringLiteral("second");

    m_manager->copyToClipboard(text1, 0);

    // Clear based on text1
    m_manager->clearClipboard();
    QVERIFY(KSystemClipboard::instance()->text(QClipboard::Clipboard).isEmpty());

    // Copy text2
    m_manager->copyToClipboard(text2, 0);

    // Clear should work for text2
    m_manager->clearClipboard();

    // Assert
    QVERIFY(KSystemClipboard::instance()->text(QClipboard::Clipboard).isEmpty());
}

void TestClipboardManager::testStateManagement_ClearResetsState()
{
    // Arrange
    m_manager->copyToClipboard(QStringLiteral("test"), 0);

    // Act
    m_manager->clearClipboard();

    // Assert - subsequent clear with different content should not match old state
    auto *mimeData = new QMimeData();
    mimeData->setText(QStringLiteral("different"));
    KSystemClipboard::instance()->setMimeData(mimeData, QClipboard::Clipboard);

    m_manager->clearClipboard();  // Should not clear "different" (state was reset)

    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, QStringLiteral("different"));
}

void TestClipboardManager::testTimerManagement_StartedOnCopyWithTimeout()
{
    // Arrange
    const QString testText = QStringLiteral("timer1");

    // Act
    m_manager->copyToClipboard(testText, 1);

    // Assert - wait for timer to fire
    QTest::qWait(1500);

    // Clipboard should be cleared by timer
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QVERIFY(clipboardText.isEmpty() || clipboardText != testText);
}

void TestClipboardManager::testTimerManagement_NotStartedOnCopyWithoutTimeout()
{
    // Arrange
    const QString testText = QStringLiteral("notimer");

    // Act
    m_manager->copyToClipboard(testText, 0);

    // Wait to ensure no timer fires
    QTest::qWait(500);

    // Assert - text should still be there
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, testText);
}

void TestClipboardManager::testTimerManagement_StoppedOnManualClear()
{
    // Arrange
    const QString testText = QStringLiteral("stoptimer");
    m_manager->copyToClipboard(testText, 5);  // 5 second timer

    // Act - clear manually before timer fires
    QTest::qWait(500);
    m_manager->clearClipboard();

    // Wait past original timer duration
    QTest::qWait(5000);

    // Assert - clipboard should be empty (cleared manually, not by timer)
    const QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QVERIFY(clipboardText.isEmpty());
}

void TestClipboardManager::testTimerManagement_StoppedOnAutoClear()
{
    // Arrange
    const QString testText = QStringLiteral("autocleartimer");

    // Act - set timer
    m_manager->copyToClipboard(testText, 1);

    // Wait for auto-clear
    QTest::qWait(1500);

    // Assert - clipboard cleared
    QString clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QVERIFY(clipboardText.isEmpty() || clipboardText != testText);

    // Copy new text without timeout - should not be affected by old timer
    const QString newText = QStringLiteral("newtext");
    m_manager->copyToClipboard(newText, 0);

    QTest::qWait(500);
    clipboardText = KSystemClipboard::instance()->text(QClipboard::Clipboard);
    QCOMPARE(clipboardText, newText);  // Still there
}

QTEST_MAIN(TestClipboardManager)
#include "test_clipboard_manager.moc"
