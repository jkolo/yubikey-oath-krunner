/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include <KRunner/AbstractRunner>
#include <KRunner/QueryMatch>
#include <KRunner/Action>
#include "krunner/actions/action_manager.h"

using namespace YubiKeyOath::Runner;

/**
 * @brief Minimal KRunner for testing
 */
class MinimalRunner : public KRunner::AbstractRunner
{
public:
    MinimalRunner(QObject *parent = nullptr)
        : KRunner::AbstractRunner(parent, createMetaData())
    {
    }

    void match(KRunner::RunnerContext &context) override {
        Q_UNUSED(context);
    }

    void run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match) override {
        Q_UNUSED(context);
        Q_UNUSED(match);
    }

private:
    static KPluginMetaData createMetaData() {
        QJsonObject rawData;
        rawData["KPlugin"] = QJsonObject{
            {"Id", "krunner_yubikey_test"},
            {"Name", "YubiKey Test Runner"}
        };
        return KPluginMetaData(rawData, QString());
    }
};

/**
 * @brief Unit tests for ActionManager
 *
 * Tests action selection logic and validation.
 */
class TestActionManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // isValidAction tests
    void testIsValidAction_Copy();
    void testIsValidAction_Type();
    void testIsValidAction_Invalid();
    void testIsValidAction_Empty();

    // getActionName tests
    void testGetActionName_Copy();
    void testGetActionName_Type();
    void testGetActionName_Unknown();

    // determineAction tests
    // Note: setSelectedAction() is private, so we can only test default behavior
    void testDetermineAction_PrimaryActionWithNoSelection();
    void testDetermineAction_InvalidPrimaryFallback();

private:
    MinimalRunner *m_runner = nullptr;
    ActionManager *m_manager = nullptr;
};

void TestActionManager::initTestCase()
{
    m_runner = new MinimalRunner(this);
    m_manager = new ActionManager();
}

void TestActionManager::cleanupTestCase()
{
    delete m_manager;
    delete m_runner;
}

// ========== isValidAction Tests ==========

void TestActionManager::testIsValidAction_Copy()
{
    QVERIFY(m_manager->isValidAction("copy"));
    QVERIFY(m_manager->isValidAction(QString("copy")));
}

void TestActionManager::testIsValidAction_Type()
{
    QVERIFY(m_manager->isValidAction("type"));
    QVERIFY(m_manager->isValidAction(QString("type")));
}

void TestActionManager::testIsValidAction_Invalid()
{
    QVERIFY(!m_manager->isValidAction("invalid"));
    QVERIFY(!m_manager->isValidAction("delete"));
    QVERIFY(!m_manager->isValidAction("paste"));
    QVERIFY(!m_manager->isValidAction("COPY")); // Case sensitive
    QVERIFY(!m_manager->isValidAction("TYPE")); // Case sensitive
}

void TestActionManager::testIsValidAction_Empty()
{
    QVERIFY(!m_manager->isValidAction(""));
    QVERIFY(!m_manager->isValidAction(QString()));
}

// ========== getActionName Tests ==========

void TestActionManager::testGetActionName_Copy()
{
    QString name = m_manager->getActionName("copy");
    QVERIFY(!name.isEmpty());
    // Name will be translated, just verify it's not "Unknown action"
    QVERIFY(name.contains("clipboard") || name.contains("Copy"));
}

void TestActionManager::testGetActionName_Type()
{
    QString name = m_manager->getActionName("type");
    QVERIFY(!name.isEmpty());
    QVERIFY(name.contains("Type") || name.contains("type"));
}

void TestActionManager::testGetActionName_Unknown()
{
    QString name = m_manager->getActionName("invalid");
    QVERIFY(!name.isEmpty());
    // Should return "Unknown action" or translated equivalent
}

// ========== determineAction Tests ==========

void TestActionManager::testDetermineAction_PrimaryActionWithNoSelection()
{
    // Create match without selected action (user pressed Enter)
    KRunner::QueryMatch match(m_runner);
    match.setId("test_match");

    // No selectedAction set (empty)

    // Should use primary action
    QString action = m_manager->determineAction(match, "copy");
    QCOMPARE(action, QString("copy"));

    // Test with type as primary
    action = m_manager->determineAction(match, "type");
    QCOMPARE(action, QString("type"));
}

void TestActionManager::testDetermineAction_InvalidPrimaryFallback()
{
    KRunner::QueryMatch match(m_runner);
    match.setId("test_match");

    // No selected action, invalid primary action
    QString action = m_manager->determineAction(match, "invalid_action");

    // Should fallback to "copy"
    QCOMPARE(action, QString("copy"));

    // Test with empty primary
    action = m_manager->determineAction(match, "");
    QCOMPARE(action, QString("copy"));
}

QTEST_MAIN(TestActionManager)
#include "test_action_manager.moc"
