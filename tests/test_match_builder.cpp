/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include <KRunner/AbstractRunner>
#include <KRunner/QueryMatch>
#include "krunner/matching/match_builder.h"
#include "mocks/mock_configuration_provider.h"
#include "shared/types/oath_credential.h"

using namespace KRunner::YubiKey;

/**
 * @brief Minimal KRunner for testing
 *
 * KRunner::AbstractRunner requires a KPluginMetaData, so we create
 * a minimal implementation for testing.
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
        // Create minimal plugin metadata for testing
        QJsonObject rawData;
        rawData["KPlugin"] = QJsonObject{
            {"Id", "krunner_yubikey_test"},
            {"Name", "YubiKey Test Runner"}
        };
        return KPluginMetaData(rawData, QString());
    }
};

/**
 * @brief Test helper class to access protected calculateRelevance()
 */
class TestableMatchBuilder : public MatchBuilder
{
public:
    using MatchBuilder::MatchBuilder;

    // Expose protected method for testing
    qreal testCalculateRelevance(const CredentialInfo &credential, const QString &query) const {
        return calculateRelevance(credential, query);
    }
};

/**
 * @brief Unit tests for MatchBuilder
 *
 * Tests QueryMatch creation and relevance scoring algorithm.
 *
 * Note: Full buildCredentialMatch() testing requires complex KRunner setup
 * with Actions and runtime environment. These tests focus on the core
 * calculateRelevance() logic which is most critical for search quality.
 */
class TestMatchBuilder : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Relevance calculation tests
    void testCalculateRelevance_ExactNameMatch();
    void testCalculateRelevance_IssuerStartsWith();
    void testCalculateRelevance_UsernameStartsWith();
    void testCalculateRelevance_NameContains();
    void testCalculateRelevance_DefaultRelevance();

    // Case insensitivity tests
    void testCalculateRelevance_CaseInsensitive();

    // Edge cases
    void testCalculateRelevance_EmptyQuery();
    void testCalculateRelevance_EmptyCredential();
    void testCalculateRelevance_PartialMatches();

    // buildPasswordErrorMatch tests
    void testBuildPasswordErrorMatch();

    // Real-world scenarios
    void testRelevanceScoring_RealWorldQueries();

private:
    MinimalRunner *m_runner = nullptr;
    MockConfigurationProvider *m_config = nullptr;
    TestableMatchBuilder *m_builder = nullptr;
    KRunner::Actions m_actions; // Empty actions for testing
};

void TestMatchBuilder::initTestCase()
{
    m_runner = new MinimalRunner(this);
    m_config = new MockConfigurationProvider(this);
    m_builder = new TestableMatchBuilder(m_runner, m_config, m_actions);
}

void TestMatchBuilder::cleanupTestCase()
{
    delete m_builder;
    delete m_config;
    delete m_runner;
}

// ========== Relevance Calculation Tests ==========

void TestMatchBuilder::testCalculateRelevance_ExactNameMatch()
{
    CredentialInfo cred;
    cred.name = "Google:user@example.com";
    cred.issuer = "Google";
    cred.username = "user@example.com";

    // Test case: Query matches start of full name
    // Expected: 1.0 relevance (highest)
    qreal relevance = m_builder->testCalculateRelevance(cred, "Google");
    QCOMPARE(relevance, 1.0);

    // Test case insensitivity
    relevance = m_builder->testCalculateRelevance(cred, "google");
    QCOMPARE(relevance, 1.0);

    relevance = m_builder->testCalculateRelevance(cred, "GOOGLE");
    QCOMPARE(relevance, 1.0);
}

void TestMatchBuilder::testCalculateRelevance_IssuerStartsWith()
{
    CredentialInfo cred;
    cred.name = "Example:Google";  // Name doesn't start with "google"
    cred.issuer = "Google";
    cred.username = "user";

    // Test case: Query matches start of issuer
    // Expected: 0.9 relevance
    qreal relevance = m_builder->testCalculateRelevance(cred, "Goo");
    QCOMPARE(relevance, 0.9);

    // Case insensitive
    relevance = m_builder->testCalculateRelevance(cred, "goo");
    QCOMPARE(relevance, 0.9);
}

void TestMatchBuilder::testCalculateRelevance_UsernameStartsWith()
{
    CredentialInfo cred;
    cred.name = "service:admin@example.com";  // Name starts with "service"
    cred.issuer = "Service";  // Issuer starts with "Service"
    cred.username = "admin@example.com";

    // Test case: Query matches start of username but not name or issuer
    // We need a query that starts username but not name/issuer
    cred.name = "MyService:admin";
    cred.issuer = "MyService";
    cred.username = "admin@example.com";

    qreal relevance = m_builder->testCalculateRelevance(cred, "admin");
    QCOMPARE(relevance, 0.8);
}

void TestMatchBuilder::testCalculateRelevance_NameContains()
{
    CredentialInfo cred;
    cred.name = "MyGoogleAccount:user";  // "google" is in middle of name
    cred.issuer = "MyGoogleAccount";
    cred.username = "user";

    // Test case: Query is contained in name (but doesn't start with it)
    // Expected: 0.7 relevance
    qreal relevance = m_builder->testCalculateRelevance(cred, "google");
    QCOMPARE(relevance, 0.7);

    relevance = m_builder->testCalculateRelevance(cred, "Google");
    QCOMPARE(relevance, 0.7);
}

void TestMatchBuilder::testCalculateRelevance_DefaultRelevance()
{
    CredentialInfo cred;
    cred.name = "Google:user@example.com";
    cred.issuer = "Google";
    cred.username = "user@example.com";

    // Test case: Query doesn't match any field
    // Expected: 0.5 relevance (default)
    qreal relevance = m_builder->testCalculateRelevance(cred, "xyz");
    QCOMPARE(relevance, 0.5);

    relevance = m_builder->testCalculateRelevance(cred, "nomatch");
    QCOMPARE(relevance, 0.5);
}

// ========== Case Insensitivity Tests ==========

void TestMatchBuilder::testCalculateRelevance_CaseInsensitive()
{
    // Verify that matching is case-insensitive
    CredentialInfo cred;
    cred.name = "GitHub:developer";
    cred.issuer = "GitHub";
    cred.username = "developer";

    // All these should give same relevance (1.0 - name starts with)
    QCOMPARE(m_builder->testCalculateRelevance(cred, "GitHub"), 1.0);
    QCOMPARE(m_builder->testCalculateRelevance(cred, "github"), 1.0);
    QCOMPARE(m_builder->testCalculateRelevance(cred, "GITHUB"), 1.0);
    QCOMPARE(m_builder->testCalculateRelevance(cred, "GiThUb"), 1.0);
}

// ========== Edge Cases ==========

void TestMatchBuilder::testCalculateRelevance_EmptyQuery()
{
    CredentialInfo cred;
    cred.name = "Google:user";
    cred.issuer = "Google";
    cred.username = "user";

    // Empty query should return default relevance (0.5)
    qreal relevance = m_builder->testCalculateRelevance(cred, "");
    QCOMPARE(relevance, 0.5);
}

void TestMatchBuilder::testCalculateRelevance_EmptyCredential()
{
    // Credential with empty fields
    CredentialInfo cred;
    cred.name = "";
    cred.issuer = "";
    cred.username = "";

    // Should return default relevance (0.5) since nothing matches
    qreal relevance = m_builder->testCalculateRelevance(cred, "test");
    QCOMPARE(relevance, 0.5);

    // Empty query with empty credential
    relevance = m_builder->testCalculateRelevance(cred, "");
    QCOMPARE(relevance, 0.5);
}

void TestMatchBuilder::testCalculateRelevance_PartialMatches()
{
    CredentialInfo cred;
    cred.name = "Amazon:shopper@example.com";
    cred.issuer = "Amazon";
    cred.username = "shopper@example.com";

    // Partial matches with different relevance levels
    QCOMPARE(m_builder->testCalculateRelevance(cred, "Am"), 1.0);      // Name starts
    QCOMPARE(m_builder->testCalculateRelevance(cred, "Amaz"), 1.0);    // Name starts
    QCOMPARE(m_builder->testCalculateRelevance(cred, "shop"), 0.8);    // Username starts
    QCOMPARE(m_builder->testCalculateRelevance(cred, "zon"), 0.7);     // Name contains
    QCOMPARE(m_builder->testCalculateRelevance(cred, "xyz"), 0.5);     // No match
}

// ========== buildPasswordErrorMatch Tests ==========

void TestMatchBuilder::testBuildPasswordErrorMatch()
{
    // Create DeviceInfo for testing
    DeviceInfo device;
    device.deviceId = "ABC123DEF456";
    device.deviceName = "YubiKey ABC123";
    device.isConnected = true;
    device.requiresPassword = true;
    device.hasValidPassword = false;

    KRunner::QueryMatch match = m_builder->buildPasswordErrorMatch(device);

    // Verify match properties
    QVERIFY(!match.text().isEmpty());
    QVERIFY(match.text().contains(device.deviceName)); // Should contain device name
    QVERIFY(!match.subtext().isEmpty());
    QVERIFY(match.subtext().contains("ABC123")); // Should contain short device ID
    QCOMPARE(match.iconName(), QString(":/icons/yubikey.svg"));

    // KRunner automatically prefixes match ID with runner plugin ID
    // Match ID should be unique per device: "yubikey_password_error_" + deviceId
    QVERIFY(match.id().contains("yubikey_password_error"));
    QVERIFY(match.id().contains(device.deviceId));
    QCOMPARE(match.relevance(), 1.0);

    // Verify match data format (index 4 should be "true" for password error, index 5 should be deviceId)
    QStringList data = match.data().toStringList();
    QVERIFY(data.size() >= 6);
    QCOMPARE(data[4], QString("true")); // isPasswordError flag
    QCOMPARE(data[5], device.deviceId); // deviceId
}

// ========== Real-World Scenarios ==========

void TestMatchBuilder::testRelevanceScoring_RealWorldQueries()
{
    // Test realistic search scenarios to verify expected ordering

    struct TestCase {
        QString credentialName;
        QString issuer;
        QString username;
        QString query;
        qreal expectedRelevance;
        QString description;
    };

    QVector<TestCase> testCases = {
        // Exact matches should rank highest
        {"Google:user@gmail.com", "Google", "user@gmail.com", "google", 1.0, "Name/issuer starts with query"},
        {"GitHub:developer", "GitHub", "developer", "github", 1.0, "Name/issuer starts with query"},

        // Partial matches should rank lower
        {"Corporate:admin", "Corporate", "admin", "corp", 1.0, "Name starts with query"},
        {"MyService:user", "MyService", "user", "service", 0.7, "Name contains query"},

        // Username matches
        {"AWS:admin@company.com", "AWS", "admin@company.com", "admin", 0.8, "Username starts with query"},

        // No matches
        {"Facebook:user", "Facebook", "user", "xyz", 0.5, "No match - default relevance"},
    };

    for (const auto &testCase : testCases) {
        CredentialInfo cred;
        cred.name = testCase.credentialName;
        cred.issuer = testCase.issuer;
        cred.username = testCase.username;

        qreal relevance = m_builder->testCalculateRelevance(cred, testCase.query);
        QCOMPARE(relevance, testCase.expectedRelevance);
        qDebug() << "Test:" << testCase.description
                 << "- Query:" << testCase.query
                 << "Credential:" << testCase.credentialName
                 << "Relevance:" << relevance;
    }
}

QTEST_MAIN(TestMatchBuilder)
#include "test_match_builder.moc"
