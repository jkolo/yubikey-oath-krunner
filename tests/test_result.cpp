/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "shared/common/result.h"

using namespace YubiKeyOath::Shared;

/**
 * @brief Unit tests for Result<T> template
 *
 * Tests the Result<T> type used for unified error handling throughout the project.
 */
class TestResult : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Result<T> tests
    void testSuccessCreation();
    void testErrorCreation();
    void testIsSuccess();
    void testIsError();
    void testValue();
    void testValueOr();
    void testError();
    void testBoolConversion();
    void testMoveSemantics();

    // Result<void> specialization tests
    void testVoidSuccess();
    void testVoidError();
    void testVoidIsSuccess();
    void testVoidBoolConversion();

    // Edge cases
    void testEmptyStringValue();
    void testNullValue();
};

// ========== Result<T> Tests ==========

void TestResult::testSuccessCreation()
{
    // Test creating success result with QString
    auto result = Result<QString>::success("test_value");

    QVERIFY(result.isSuccess());
    QVERIFY(!result.isError());
    QCOMPARE(result.value(), QString("test_value"));
    QVERIFY(result.error().isEmpty());
}

void TestResult::testErrorCreation()
{
    // Test creating error result
    auto result = Result<QString>::error("test_error");

    QVERIFY(!result.isSuccess());
    QVERIFY(result.isError());
    QCOMPARE(result.error(), QString("test_error"));
    // Note: Calling value() on error Result is undefined behavior (would trigger Q_ASSERT)
}

void TestResult::testIsSuccess()
{
    auto success = Result<int>::success(42);
    auto error = Result<int>::error("failure");

    QVERIFY(success.isSuccess());
    QVERIFY(!error.isSuccess());
}

void TestResult::testIsError()
{
    auto success = Result<int>::success(42);
    auto error = Result<int>::error("failure");

    QVERIFY(!success.isError());
    QVERIFY(error.isError());
}

void TestResult::testValue()
{
    // Test value() returns correct value
    auto result = Result<int>::success(42);
    QCOMPARE(result.value(), 42);

    // Test with QString
    auto strResult = Result<QString>::success("hello");
    QCOMPARE(strResult.value(), QString("hello"));

    // Test with complex type
    struct TestStruct {
        int a;
        QString b;
        bool operator==(const TestStruct &other) const {
            return a == other.a && b == other.b;
        }
    };

    const TestStruct test{.a = 123, .b = "test"};
    auto structResult = Result<TestStruct>::success(test);
    QVERIFY(structResult.value() == test);
}

void TestResult::testValueOr()
{
    // Test valueOr() with success
    auto success = Result<int>::success(42);
    QCOMPARE(success.valueOr(99), 42);

    // Test valueOr() with error
    auto error = Result<int>::error("failed");
    QCOMPARE(error.valueOr(99), 99);

    // Test with QString
    auto strError = Result<QString>::error("failed");
    QCOMPARE(strError.valueOr("default"), QString("default"));
}

void TestResult::testError()
{
    // Test error() returns error message
    auto error = Result<int>::error("test error message");
    QCOMPARE(error.error(), QString("test error message"));

    // Test error() on success returns empty string
    auto success = Result<int>::success(42);
    QVERIFY(success.error().isEmpty());
}

void TestResult::testBoolConversion()
{
    // Test explicit bool conversion
    auto success = Result<int>::success(42);
    auto error = Result<int>::error("failed");

    QVERIFY(static_cast<bool>(success));
    QVERIFY(!static_cast<bool>(error));

    // Test in if statement
    if (success) {
        QVERIFY(true); // Should execute
    } else {
        QFAIL("Success should evaluate to true");
    }

    if (error) {
        QFAIL("Error should evaluate to false");
    } else {
        QVERIFY(true); // Should execute
    }
}

void TestResult::testMoveSemantics()
{
    // Test that Result properly handles move semantics
    QString largeString;
    largeString.fill('a', 10000); // Large string
    auto result = Result<QString>::success(std::move(largeString));

    QVERIFY(result.isSuccess());
    QCOMPARE(result.value().size(), 10000);

    // Test valueOr with large default value
    QString defaultString;
    defaultString.fill('b', 5000);
    auto error = Result<QString>::error("failed");
    const QString value = error.valueOr(defaultString);
    QCOMPARE(value.size(), 5000);
}

// ========== Result<void> Tests ==========

void TestResult::testVoidSuccess()
{
    auto result = Result<void>::success();

    QVERIFY(result.isSuccess());
    QVERIFY(!result.isError());
    QVERIFY(result.error().isEmpty());
}

void TestResult::testVoidError()
{
    auto result = Result<void>::error("void operation failed");

    QVERIFY(!result.isSuccess());
    QVERIFY(result.isError());
    QCOMPARE(result.error(), QString("void operation failed"));
}

void TestResult::testVoidIsSuccess()
{
    auto success = Result<void>::success();
    auto error = Result<void>::error("failed");

    QVERIFY(success.isSuccess());
    QVERIFY(!error.isSuccess());
}

void TestResult::testVoidBoolConversion()
{
    auto success = Result<void>::success();
    auto error = Result<void>::error("failed");

    QVERIFY(static_cast<bool>(success));
    QVERIFY(!static_cast<bool>(error));
}

// ========== Edge Cases ==========

void TestResult::testEmptyStringValue()
{
    // Test that empty string as value is different from error
    auto result = Result<QString>::success("");

    QVERIFY(result.isSuccess()); // Empty string is still success
    QVERIFY(!result.isError());
    QCOMPARE(result.value(), QString(""));
    QVERIFY(result.error().isEmpty());
}

void TestResult::testNullValue()
{
    // Test with nullptr for pointer types
    auto result = Result<QString*>::success(nullptr);

    QVERIFY(result.isSuccess());
    QCOMPARE(result.value(), nullptr);

    // Test valueOr with nullptr
    QString str("default");
    auto error = Result<QString*>::error("failed");
    QCOMPARE(error.valueOr(&str), &str);
}

QTEST_MAIN(TestResult)
#include "test_result.moc"
