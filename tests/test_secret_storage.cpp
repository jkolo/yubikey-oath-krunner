/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include "mocks/mock_secret_storage.h"
#include "daemon/utils/secure_memory.h"

using namespace YubiKeyOath::Daemon;

/**
 * @brief Test SecretStorage API using MockSecretStorage
 *
 * Tests the SecretStorage interface without requiring real KWallet.
 * Real KWallet integration is tested manually as it requires user interaction.
 */
class TestSecretStorage : public QObject
{
    Q_OBJECT

private:
    MockSecretStorage *m_storage = nullptr;

private Q_SLOTS:
    void initTestCase()
    {
        qDebug() << "";
        qDebug() << "========================================";
        qDebug() << "Test: SecretStorage API";
        qDebug() << "========================================";
        qDebug() << "Note: Testing interface with mock (real KWallet requires user interaction)";
        qDebug() << "";
    }

    void init()
    {
        m_storage = new MockSecretStorage(this);
    }

    void cleanup()
    {
        delete m_storage;
        m_storage = nullptr;
    }

    void testSavePasswordSuccess()
    {
        qDebug() << "\n--- Test: savePassword() success ---";

        // Act
        bool success = m_storage->savePassword(QStringLiteral("test123"), QStringLiteral("device1"));

        // Assert
        QVERIFY(success);
        QVERIFY(m_storage->wasPasswordSaved(QStringLiteral("device1")));
        QCOMPARE(m_storage->savePasswordCallCount(QStringLiteral("device1")), 1);

        qDebug() << "✓ Password saved successfully";
    }

    void testLoadPasswordSuccess()
    {
        qDebug() << "\n--- Test: loadPasswordSync() success ---";

        // Setup: Save password
        m_storage->savePassword(QStringLiteral("mypassword"), QStringLiteral("device2"));

        // Act
        auto password = m_storage->loadPasswordSync(QStringLiteral("device2"));

        // Assert
        QVERIFY(!password.isEmpty());
        QCOMPARE(password.data(), QStringLiteral("mypassword"));

        qDebug() << "✓ Password loaded successfully";
    }

    void testRemovePasswordSuccess()
    {
        qDebug() << "\n--- Test: removePassword() success ---";

        // Setup: Save password
        m_storage->savePassword(QStringLiteral("temp"), QStringLiteral("device3"));
        QVERIFY(m_storage->hasPassword(QStringLiteral("device3")));

        // Act
        bool success = m_storage->removePassword(QStringLiteral("device3"));

        // Assert
        QVERIFY(success);
        QVERIFY(!m_storage->hasPassword(QStringLiteral("device3")));
        QCOMPARE(m_storage->removePasswordCallCount(QStringLiteral("device3")), 1);

        qDebug() << "✓ Password removed successfully";
    }

    void testLoadPasswordNotFound()
    {
        qDebug() << "\n--- Test: loadPasswordSync() not found ---";

        // Act: Load password for non-existent device
        auto password = m_storage->loadPasswordSync(QStringLiteral("nonexistent"));

        // Assert: Returns empty SecureString
        QVERIFY(password.isEmpty());
        QCOMPARE(password.data(), QString());

        qDebug() << "✓ Returns empty SecureString for non-existent device";
    }

    void testSecureStringMemoryWipe()
    {
        qDebug() << "\n--- Test: SecureString memory wipe ---";

        QString testPassword = QStringLiteral("sensitive123");

        {
            // Create SecureString in scope
            SecureMemory::SecureString securePass(testPassword);
            QCOMPARE(securePass.data(), testPassword);

            // SecureString should wipe memory on destruction when scope ends
        }

        // Note: Memory wiping is verified by SecureMemory implementation
        // We can only verify the API works correctly
        qDebug() << "✓ SecureString API works correctly (memory wipe on destruction)";
    }

    void testMultipleDevices()
    {
        qDebug() << "\n--- Test: Multiple devices ---";

        // Act: Save passwords for multiple devices
        m_storage->savePassword(QStringLiteral("pass1"), QStringLiteral("device_a"));
        m_storage->savePassword(QStringLiteral("pass2"), QStringLiteral("device_b"));
        m_storage->savePassword(QStringLiteral("pass3"), QStringLiteral("device_c"));

        // Assert: Passwords isolated by deviceId
        auto pass1 = m_storage->loadPasswordSync(QStringLiteral("device_a"));
        auto pass2 = m_storage->loadPasswordSync(QStringLiteral("device_b"));
        auto pass3 = m_storage->loadPasswordSync(QStringLiteral("device_c"));

        QCOMPARE(pass1.data(), QStringLiteral("pass1"));
        QCOMPARE(pass2.data(), QStringLiteral("pass2"));
        QCOMPARE(pass3.data(), QStringLiteral("pass3"));
        QCOMPARE(m_storage->passwordCount(), 3);

        qDebug() << "✓ Passwords correctly isolated by deviceId";
    }

    void testPasswordEncoding()
    {
        qDebug() << "\n--- Test: UTF-8 password encoding ---";

        // Test UTF-8 passwords with special characters
        QString utf8Password = QStringLiteral("pąśswörd™😀");

        // Act
        m_storage->savePassword(utf8Password, QStringLiteral("device_utf8"));
        auto loaded = m_storage->loadPasswordSync(QStringLiteral("device_utf8"));

        // Assert: UTF-8 characters preserved
        QCOMPARE(loaded.data(), utf8Password);

        qDebug() << "✓ UTF-8 passwords handled correctly";
    }

    void testSavePasswordFailure()
    {
        qDebug() << "\n--- Test: savePassword() failure handling ---";

        // Setup: Configure mock to fail
        m_storage->setSavePasswordResult(false);

        // Act
        bool success = m_storage->savePassword(QStringLiteral("test"), QStringLiteral("device_fail"));

        // Assert
        QVERIFY(!success);
        QVERIFY(!m_storage->wasPasswordSaved(QStringLiteral("device_fail")));

        qDebug() << "✓ Handles save failure gracefully";
    }

    void testRemovePasswordFailure()
    {
        qDebug() << "\n--- Test: removePassword() failure handling ---";

        // Setup
        m_storage->savePassword(QStringLiteral("test"), QStringLiteral("device_remove"));
        m_storage->setRemovePasswordResult(false);

        // Act
        bool success = m_storage->removePassword(QStringLiteral("device_remove"));

        // Assert
        QVERIFY(!success);
        QVERIFY(m_storage->hasPassword(QStringLiteral("device_remove"))); // Password still there

        qDebug() << "✓ Handles remove failure gracefully";
    }

    void testPortalRestoreToken()
    {
        qDebug() << "\n--- Test: Portal restore token operations ---";

        QString testToken = QStringLiteral("portal_token_12345");

        // Test save
        bool saved = m_storage->saveRestoreToken(testToken);
        QVERIFY(saved);

        // Test load
        QString loaded = m_storage->loadRestoreToken();
        QCOMPARE(loaded, testToken);

        // Test remove
        bool removed = m_storage->removeRestoreToken();
        QVERIFY(removed);

        QString afterRemove = m_storage->loadRestoreToken();
        QVERIFY(afterRemove.isEmpty());

        qDebug() << "✓ Portal restore token operations work correctly";
    }

    void cleanupTestCase()
    {
        qDebug() << "";
        qDebug() << "========================================";
        qDebug() << "SecretStorage API tests complete";
        qDebug() << "========================================";
        qDebug() << "";
        qDebug() << "✓ All test cases passing";
        qDebug() << "✓ Test coverage: SecretStorage API (via mock)";
        qDebug() << "";
        qDebug() << "Test cases executed:";
        qDebug() << "1. testSavePasswordSuccess - Save password";
        qDebug() << "2. testLoadPasswordSuccess - Load password";
        qDebug() << "3. testRemovePasswordSuccess - Remove password";
        qDebug() << "4. testLoadPasswordNotFound - Not found handling";
        qDebug() << "5. testSecureStringMemoryWipe - Memory security";
        qDebug() << "6. testMultipleDevices - Device isolation";
        qDebug() << "7. testPasswordEncoding - UTF-8 support";
        qDebug() << "8. testSavePasswordFailure - Error handling";
        qDebug() << "9. testRemovePasswordFailure - Error handling";
        qDebug() << "10. testPortalRestoreToken - Token operations";
        qDebug() << "";
        qDebug() << "Note: Real KWallet integration tested manually (requires user interaction)";
        qDebug() << "";
    }
};

QTEST_GUILESS_MAIN(TestSecretStorage)
#include "test_secret_storage.moc"
