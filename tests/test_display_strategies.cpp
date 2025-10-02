/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "krunner/formatting/display_strategies/name_only_strategy.h"
#include "krunner/formatting/display_strategies/name_user_strategy.h"
#include "krunner/formatting/display_strategies/full_strategy.h"
#include "krunner/formatting/display_strategies/display_strategy_factory.h"
#include "shared/types/oath_credential.h"

using namespace KRunner::YubiKey;

/**
 * @brief Unit tests for Display Strategies
 *
 * Tests individual display strategy implementations.
 */
class TestDisplayStrategies : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // NameOnlyStrategy tests
    void testNameOnlyStrategy_WithIssuer();
    void testNameOnlyStrategy_WithoutIssuer();
    void testNameOnlyStrategy_BothEmpty();
    void testNameOnlyStrategy_Identifier();

    // NameUserStrategy tests
    void testNameUserStrategy_BothPresent();
    void testNameUserStrategy_OnlyIssuer();
    void testNameUserStrategy_OnlyUsername();
    void testNameUserStrategy_BothEmpty();
    void testNameUserStrategy_Identifier();

    // FullStrategy tests
    void testFullStrategy_WithCode();
    void testFullStrategy_WithoutCode();
    void testFullStrategy_EmptyCode();
    void testFullStrategy_FormatWithCode();
    void testFullStrategy_FormatWithCodeTouchRequired();
    void testFullStrategy_FormatWithCodeNoCode();
    void testFullStrategy_Identifier();

    // DisplayStrategyFactory tests
    void testFactory_CreateByIdentifier();
    void testFactory_CreateUnknownIdentifier();
    void testFactory_DefaultIdentifier();
};

// ========== NameOnlyStrategy Tests ==========

void TestDisplayStrategies::testNameOnlyStrategy_WithIssuer()
{
    NameOnlyStrategy strategy;
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";

    QString result = strategy.format(cred);
    QCOMPARE(result, QString("Google"));
}

void TestDisplayStrategies::testNameOnlyStrategy_WithoutIssuer()
{
    NameOnlyStrategy strategy;
    OathCredential cred;
    cred.issuer = "";
    cred.username = "user@example.com";

    QString result = strategy.format(cred);
    QCOMPARE(result, QString("user@example.com"));
}

void TestDisplayStrategies::testNameOnlyStrategy_BothEmpty()
{
    NameOnlyStrategy strategy;
    OathCredential cred;
    cred.issuer = "";
    cred.username = "";

    QString result = strategy.format(cred);
    QCOMPARE(result, QString(""));
}

void TestDisplayStrategies::testNameOnlyStrategy_Identifier()
{
    NameOnlyStrategy strategy;
    QCOMPARE(strategy.identifier(), QString("name"));
}

// ========== NameUserStrategy Tests ==========

void TestDisplayStrategies::testNameUserStrategy_BothPresent()
{
    NameUserStrategy strategy;
    OathCredential cred;
    cred.issuer = "GitHub";
    cred.username = "developer";

    QString result = strategy.format(cred);
    QCOMPARE(result, QString("GitHub (developer)"));
}

void TestDisplayStrategies::testNameUserStrategy_OnlyIssuer()
{
    NameUserStrategy strategy;
    OathCredential cred;
    cred.issuer = "AWS";
    cred.username = "";

    QString result = strategy.format(cred);
    QCOMPARE(result, QString("AWS"));
}

void TestDisplayStrategies::testNameUserStrategy_OnlyUsername()
{
    NameUserStrategy strategy;
    OathCredential cred;
    cred.issuer = "";
    cred.username = "admin@company.com";

    QString result = strategy.format(cred);
    QCOMPARE(result, QString("admin@company.com"));
}

void TestDisplayStrategies::testNameUserStrategy_BothEmpty()
{
    NameUserStrategy strategy;
    OathCredential cred;
    cred.issuer = "";
    cred.username = "";

    QString result = strategy.format(cred);
    QCOMPARE(result, QString(""));
}

void TestDisplayStrategies::testNameUserStrategy_Identifier()
{
    NameUserStrategy strategy;
    QCOMPARE(strategy.identifier(), QString("name_user"));
}

// ========== FullStrategy Tests ==========

void TestDisplayStrategies::testFullStrategy_WithCode()
{
    FullStrategy strategy;
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "123456";

    QString result = strategy.format(cred);
    QCOMPARE(result, QString("Google (user@example.com) - 123456"));
}

void TestDisplayStrategies::testFullStrategy_WithoutCode()
{
    FullStrategy strategy;
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    // code not set

    QString result = strategy.format(cred);
    QCOMPARE(result, QString("Google (user@example.com)"));
}

void TestDisplayStrategies::testFullStrategy_EmptyCode()
{
    FullStrategy strategy;
    OathCredential cred;
    cred.issuer = "Google";
    cred.username = "user@example.com";
    cred.code = "";

    QString result = strategy.format(cred);
    QCOMPARE(result, QString("Google (user@example.com)"));
}

void TestDisplayStrategies::testFullStrategy_FormatWithCode()
{
    FullStrategy strategy;
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.username = "shopper";

    // Test with code
    QString result = strategy.formatWithCode(cred, "654321", false);
    QCOMPARE(result, QString("Amazon (shopper) [654321]"));
}

void TestDisplayStrategies::testFullStrategy_FormatWithCodeTouchRequired()
{
    FullStrategy strategy;
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.username = "shopper";

    // Test with touch required
    QString result = strategy.formatWithCode(cred, "", true);
    QVERIFY(result.contains("Amazon (shopper)"));
    QVERIFY(result.contains("Touch Required") || result.contains("touch")); // Translated
}

void TestDisplayStrategies::testFullStrategy_FormatWithCodeNoCode()
{
    FullStrategy strategy;
    OathCredential cred;
    cred.issuer = "Amazon";
    cred.username = "shopper";

    // Test without code and without touch
    QString result = strategy.formatWithCode(cred, "", false);
    QCOMPARE(result, QString("Amazon (shopper)"));
}

void TestDisplayStrategies::testFullStrategy_Identifier()
{
    FullStrategy strategy;
    QCOMPARE(strategy.identifier(), QString("full"));
}

// ========== DisplayStrategyFactory Tests ==========

void TestDisplayStrategies::testFactory_CreateByIdentifier()
{
    // Test creating strategies by identifier
    auto nameStrategy = DisplayStrategyFactory::createStrategy("name");
    QVERIFY(nameStrategy != nullptr);
    QCOMPARE(nameStrategy->identifier(), QString("name"));

    auto nameUserStrategy = DisplayStrategyFactory::createStrategy("name_user");
    QVERIFY(nameUserStrategy != nullptr);
    QCOMPARE(nameUserStrategy->identifier(), QString("name_user"));

    auto fullStrategy = DisplayStrategyFactory::createStrategy("full");
    QVERIFY(fullStrategy != nullptr);
    QCOMPARE(fullStrategy->identifier(), QString("full"));
}

void TestDisplayStrategies::testFactory_CreateUnknownIdentifier()
{
    // Unknown identifier should fallback to default (name_user)
    auto strategy = DisplayStrategyFactory::createStrategy("invalid");
    QVERIFY(strategy != nullptr);
    QCOMPARE(strategy->identifier(), QString("name_user"));
}

void TestDisplayStrategies::testFactory_DefaultIdentifier()
{
    QString defaultId = DisplayStrategyFactory::defaultIdentifier();
    QCOMPARE(defaultId, QString("name_user"));
}

QTEST_MAIN(TestDisplayStrategies)
#include "test_display_strategies.moc"
