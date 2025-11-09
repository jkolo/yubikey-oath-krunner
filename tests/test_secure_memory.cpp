/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/daemon/utils/secure_memory.h"

#include <QtTest>
#include <QByteArray>
#include <QString>

using namespace YubiKeyOath::Daemon;

/**
 * @brief Tests for SecureMemory utility class
 *
 * Verifies secure memory wiping functionality for passwords and secrets.
 */
class TestSecureMemory : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // wipeString() tests
    void testWipeString_EmptyString();
    void testWipeString_NonEmptyString();
    void testWipeString_LongString();
    void testWipeString_UnicodeString();

    // wipeByteArray() tests
    void testWipeByteArray_EmptyArray();
    void testWipeByteArray_NonEmptyArray();
    void testWipeByteArray_BinaryData();

    // SecureString RAII tests
    void testSecureString_DefaultConstructor();
    void testSecureString_StringConstructor();
    void testSecureString_AutoWipeOnDestruction();
    void testSecureString_MoveSemantics();
    void testSecureString_ImplicitConversion();
    void testSecureString_IsEmpty();
    void testSecureString_DataAccess();

private:
    /**
     * @brief Helper to verify QString is cleared
     */
    static bool isStringCleared(const QString &str) {
        return str.isEmpty();
    }

    /**
     * @brief Helper to verify QByteArray is cleared
     */
    static bool isByteArrayCleared(const QByteArray &data) {
        return data.isEmpty();
    }
};

void TestSecureMemory::initTestCase()
{
    qDebug() << "=== TestSecureMemory: Starting test suite ===";
}

void TestSecureMemory::cleanupTestCase()
{
    qDebug() << "=== TestSecureMemory: Test suite finished ===";
}

// ============================================================================
// wipeString() Tests
// ============================================================================

void TestSecureMemory::testWipeString_EmptyString()
{
    qDebug() << "\n=== Test: Wipe Empty String ===";

    QString str;
    SecureMemory::wipeString(str);

    QVERIFY(isStringCleared(str));
    qDebug() << "  Empty string remains empty after wipe";
}

void TestSecureMemory::testWipeString_NonEmptyString()
{
    qDebug() << "\n=== Test: Wipe Non-Empty String ===";

    QString str = QStringLiteral("MySecretPassword123!");
    QVERIFY(!str.isEmpty());
    qDebug() << "  Original string length:" << str.length();

    SecureMemory::wipeString(str);

    QVERIFY(isStringCleared(str));
    qDebug() << "  String cleared after wipe";
}

void TestSecureMemory::testWipeString_LongString()
{
    qDebug() << "\n=== Test: Wipe Long String ===";

    // Create 1KB password
    QString str;
    for (int i = 0; i < 512; ++i) {
        str += QStringLiteral("ab");
    }

    QCOMPARE(str.length(), 1024);
    qDebug() << "  Long string length:" << str.length();

    SecureMemory::wipeString(str);

    QVERIFY(isStringCleared(str));
    qDebug() << "  Long string cleared after wipe";
}

void TestSecureMemory::testWipeString_UnicodeString()
{
    qDebug() << "\n=== Test: Wipe Unicode String ===";

    QString str = QStringLiteral("пароль密码🔐");  // Russian + Chinese + Emoji
    QVERIFY(!str.isEmpty());
    qDebug() << "  Unicode string length:" << str.length();

    SecureMemory::wipeString(str);

    QVERIFY(isStringCleared(str));
    qDebug() << "  Unicode string cleared after wipe";
}

// ============================================================================
// wipeByteArray() Tests
// ============================================================================

void TestSecureMemory::testWipeByteArray_EmptyArray()
{
    qDebug() << "\n=== Test: Wipe Empty ByteArray ===";

    QByteArray data;
    SecureMemory::wipeByteArray(data);

    QVERIFY(isByteArrayCleared(data));
    qDebug() << "  Empty array remains empty after wipe";
}

void TestSecureMemory::testWipeByteArray_NonEmptyArray()
{
    qDebug() << "\n=== Test: Wipe Non-Empty ByteArray ===";

    QByteArray data = "SecretKey123";
    QVERIFY(!data.isEmpty());
    qDebug() << "  Original array size:" << data.size();

    SecureMemory::wipeByteArray(data);

    QVERIFY(isByteArrayCleared(data));
    qDebug() << "  Array cleared after wipe";
}

void TestSecureMemory::testWipeByteArray_BinaryData()
{
    qDebug() << "\n=== Test: Wipe Binary Data ===";

    // Create binary data with null bytes
    QByteArray data;
    data.append('\x00');
    data.append('\xFF');
    data.append('\x42');
    data.append('\x00');

    QCOMPARE(data.size(), 4);
    qDebug() << "  Binary data size:" << data.size();

    SecureMemory::wipeByteArray(data);

    QVERIFY(isByteArrayCleared(data));
    qDebug() << "  Binary data cleared after wipe";
}

// ============================================================================
// SecureString RAII Tests
// ============================================================================

void TestSecureMemory::testSecureString_DefaultConstructor()
{
    qDebug() << "\n=== Test: SecureString Default Constructor ===";

    SecureMemory::SecureString secureStr;

    QVERIFY(secureStr.isEmpty());
    QVERIFY(secureStr.data().isEmpty());
    qDebug() << "  Default constructor creates empty SecureString";
}

void TestSecureMemory::testSecureString_StringConstructor()
{
    qDebug() << "\n=== Test: SecureString String Constructor ===";

    QString password = QStringLiteral("TestPassword");
    SecureMemory::SecureString secureStr(std::move(password));

    QVERIFY(!secureStr.isEmpty());
    QCOMPARE(secureStr.data(), QStringLiteral("TestPassword"));
    qDebug() << "  String constructor works correctly";
}

void TestSecureMemory::testSecureString_AutoWipeOnDestruction()
{
    qDebug() << "\n=== Test: SecureString Auto-Wipe on Destruction ===";

    QString originalPassword = QStringLiteral("WillBeWiped");

    {
        SecureMemory::SecureString secureStr(std::move(originalPassword));
        QVERIFY(!secureStr.isEmpty());
        qDebug() << "  SecureString created with password";

        // SecureString goes out of scope here - destructor should wipe
    }

    qDebug() << "  SecureString destroyed (password should be wiped from memory)";
    // NOTE: We cannot verify actual memory wiping in unit test,
    // but we can verify the destructor runs without crashing
}

void TestSecureMemory::testSecureString_MoveSemantics()
{
    qDebug() << "\n=== Test: SecureString Move Semantics ===";

    SecureMemory::SecureString str1(QStringLiteral("Password1"));
    QCOMPARE(str1.data(), QStringLiteral("Password1"));

    // Move construction
    SecureMemory::SecureString str2(std::move(str1));
    QCOMPARE(str2.data(), QStringLiteral("Password1"));
    qDebug() << "  Move construction works";

    // Move assignment
    SecureMemory::SecureString str3;
    str3 = std::move(str2);
    QCOMPARE(str3.data(), QStringLiteral("Password1"));
    qDebug() << "  Move assignment works";
}

void TestSecureMemory::testSecureString_ImplicitConversion()
{
    qDebug() << "\n=== Test: SecureString Implicit Conversion ===";

    SecureMemory::SecureString secureStr(QStringLiteral("ConvertMe"));

    // Implicit conversion to const QString&
    const QString &ref = secureStr;
    QCOMPARE(ref, QStringLiteral("ConvertMe"));
    qDebug() << "  Implicit conversion to const QString& works";

    // Can be passed to functions expecting const QString&
    auto checkPassword = [](const QString &pwd) {
        return pwd == QStringLiteral("ConvertMe");
    };

    QVERIFY(checkPassword(secureStr));
    qDebug() << "  Can be passed to functions expecting const QString&";
}

void TestSecureMemory::testSecureString_IsEmpty()
{
    qDebug() << "\n=== Test: SecureString isEmpty() ===";

    SecureMemory::SecureString emptyStr;
    QVERIFY(emptyStr.isEmpty());

    SecureMemory::SecureString nonEmptyStr(QStringLiteral("NotEmpty"));
    QVERIFY(!nonEmptyStr.isEmpty());

    qDebug() << "  isEmpty() works correctly";
}

void TestSecureMemory::testSecureString_DataAccess()
{
    qDebug() << "\n=== Test: SecureString data() Access ===";

    SecureMemory::SecureString secureStr(QStringLiteral("AccessMe"));

    const QString &data = secureStr.data();
    QCOMPARE(data, QStringLiteral("AccessMe"));

    // Verify data() returns const reference (cannot be modified)
    // This is enforced by the signature: const QString &data() const
    qDebug() << "  data() returns const reference";
}

QTEST_MAIN(TestSecureMemory)
#include "test_secure_memory.moc"
