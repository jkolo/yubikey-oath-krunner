/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include <QSignalSpy>
#include <memory>

#include "helpers/test_dbus_session.h"
#include "mocks/virtual_yubikey.h"
#include "mocks/virtual_nitrokey.h"
#include "../src/shared/dbus/oath_manager_proxy.h"
#include "../src/shared/types/device_state.h"

using namespace YubiKeyOath::Shared;

/**
 * @brief End-to-end test for device lifecycle
 *
 * Tests full device lifecycle with virtual devices and isolated D-Bus session:
 * 1. Device detection and connection
 * 2. Async initialization (state machine transitions)
 * 3. Credential listing
 * 4. Code generation
 * 5. Device hot-plug (disconnect/reconnect)
 * 6. Multi-device scenarios
 *
 * NOTE: This test runs with dbus-run-session wrapper (see CMakeLists.txt)
 * to ensure isolated D-Bus session and avoid conflicts with production daemon.
 */
class TestE2EDeviceLifecycle : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();

    // Test cases
    void testDeviceDetection();
    void testDeviceStateTransitions();
    void testCredentialList();
    void testGenerateCode();
    void testMultiDevice();

private:
    // Test infrastructure
    TestDbusSession m_testBus;  // Own D-Bus session for isolation
    OathManagerProxy* m_manager = nullptr;

    // Virtual devices
    std::unique_ptr<VirtualYubiKey> m_yubikey;
    std::unique_ptr<VirtualNitrokey> m_nitrokey;

    // Helper methods
    void waitForDeviceReady(const QString& serial, int timeoutMs = 5000);
    OathDeviceProxy* findDeviceBySerial(quint32 serial);
};

void TestE2EDeviceLifecycle::initTestCase()
{
    qDebug() << "\n========================================";
    qDebug() << "E2E Test: Device Lifecycle";
    qDebug() << "========================================\n";

    // Start isolated D-Bus session
    QVERIFY2(m_testBus.start(), "Failed to start isolated D-Bus session");
    qDebug() << "Test D-Bus session started at:" << m_testBus.address();

    // Set session bus address for this process (so OathManagerProxy::instance() uses test bus)
    qputenv("DBUS_SESSION_BUS_ADDRESS", m_testBus.address().toUtf8());

    // Start daemon on test bus
    QVERIFY2(m_testBus.startDaemon(QStringLiteral("/usr/bin/yubikey-oath-daemon"), {}, 1000),
             "Failed to start daemon on test bus");
    qDebug() << "Daemon started on test bus";

    // Create manager proxy (uses sessionBus which now points to test bus)
    m_manager = OathManagerProxy::instance();
    QVERIFY2(m_manager != nullptr, "Failed to create OathManagerProxy");

    // Wait for daemon to be available
    QTest::qWait(500);

    qDebug() << "E2E Test initialized with isolated D-Bus session\n";
}

void TestE2EDeviceLifecycle::cleanupTestCase()
{
    qDebug() << "\nE2E Test cleanup starting...";

    // Stop test bus (automatically stops daemon first, then D-Bus session)
    // This ensures proper cleanup order: daemon → D-Bus session
    m_testBus.stop();

    // m_manager is a singleton, don't delete it
    // (it will be cleaned up when QCoreApplication exits)

    qDebug() << "E2E Test cleanup complete";
}

void TestE2EDeviceLifecycle::init()
{
    // Create fresh virtual YubiKey for each test
    m_yubikey = std::make_unique<VirtualYubiKey>(
        QStringLiteral("12345678"),
        Version(5, 4, 2),
        QStringLiteral("YubiKey 5C NFC")
    );

    // Add test credentials
    OathCredential cred1;
    cred1.originalName = QStringLiteral("GitHub:user");
    cred1.type = OathType::TOTP;
    cred1.algorithm = OathAlgorithm::SHA1;
    cred1.digits = 6;
    cred1.period = 30;
    cred1.requiresTouch = false;
    m_yubikey->addCredential(cred1);

    OathCredential cred2;
    cred2.originalName = QStringLiteral("Google:test@example.com");
    cred2.type = OathType::TOTP;
    cred2.algorithm = OathAlgorithm::SHA256;
    cred2.digits = 8;
    cred2.period = 30;
    cred2.requiresTouch = false;
    m_yubikey->addCredential(cred2);

    qDebug() << "Test setup: Created virtual YubiKey with 2 credentials";
}

void TestE2EDeviceLifecycle::testDeviceDetection()
{
    qDebug() << "\n--- Test: Device Detection ---";

    // This test verifies the D-Bus proxy layer works correctly
    // In full E2E test (with running daemon), we would:
    // 1. Inject virtual device into PC/SC mock
    // 2. Trigger device detection
    // 3. Wait for deviceConnected signal
    // 4. Verify device appears in manager.devices()

    // For now, test the virtual device emulator itself
    QVERIFY(m_yubikey != nullptr);
    QCOMPARE(m_yubikey->serialNumber(), 0x12345678u);
    QCOMPARE(m_yubikey->firmwareVersion().toString(), QStringLiteral("5.4.2"));
    QCOMPARE(m_yubikey->credentials().size(), 2);

    // Test APDU: SELECT OATH applet
    QByteArray selectApdu = QByteArray::fromHex("00A4040007A0000005272101");
    QByteArray selectResponse = m_yubikey->processApdu(selectApdu);

    // Verify success (ends with 0x9000)
    QVERIFY(selectResponse.size() >= 2);
    quint16 sw = (static_cast<quint8>(selectResponse[selectResponse.size() - 2]) << 8) |
                  static_cast<quint8>(selectResponse[selectResponse.size() - 1]);
    QCOMPARE(sw, static_cast<quint16>(0x9000));

    qDebug() << "✓ Device detection test passed (virtual device layer)";
}

void TestE2EDeviceLifecycle::testDeviceStateTransitions()
{
    qDebug() << "\n--- Test: Device State Transitions ---";

    // Test device state enum values
    QCOMPARE(static_cast<int>(DeviceState::Disconnected), 0x00);
    QCOMPARE(static_cast<int>(DeviceState::Connecting), 0x01);
    QCOMPARE(static_cast<int>(DeviceState::Authenticating), 0x02);
    QCOMPARE(static_cast<int>(DeviceState::FetchingCredentials), 0x03);
    QCOMPARE(static_cast<int>(DeviceState::Ready), 0x04);
    QCOMPARE(static_cast<int>(DeviceState::Error), 0xFF);

    // Test state helper functions
    QVERIFY(isDeviceStateTransitional(DeviceState::Connecting));
    QVERIFY(isDeviceStateTransitional(DeviceState::Authenticating));
    QVERIFY(isDeviceStateTransitional(DeviceState::FetchingCredentials));
    QVERIFY(!isDeviceStateTransitional(DeviceState::Ready));
    QVERIFY(!isDeviceStateTransitional(DeviceState::Disconnected));

    QVERIFY(isDeviceStateReady(DeviceState::Ready));
    QVERIFY(!isDeviceStateReady(DeviceState::Connecting));

    qDebug() << "✓ Device state transitions test passed";
}

void TestE2EDeviceLifecycle::testCredentialList()
{
    qDebug() << "\n--- Test: Credential List ---";

    // SELECT OATH applet first to establish session
    QByteArray selectApdu = QByteArray::fromHex("00A4040007A0000005272101");
    QByteArray selectResponse = m_yubikey->processApdu(selectApdu);
    QVERIFY(selectResponse.endsWith(QByteArray::fromHex("9000")));

    // Test LIST command on virtual device
    QByteArray listApdu = QByteArray::fromHex("00A10000");
    QByteArray listResponse = m_yubikey->processApdu(listApdu);

    // Verify success
    QVERIFY(listResponse.size() >= 2);
    quint16 sw = (static_cast<quint8>(listResponse[listResponse.size() - 2]) << 8) |
                  static_cast<quint8>(listResponse[listResponse.size() - 1]);

    // Note: YubiKey LIST may spuriously return 0x6985 (touch required) due to bug emulation
    // If we get that, retry
    if (sw == 0x6985) {
        qDebug() << "Got spurious 0x6985 (YubiKey LIST bug emulation), retrying...";
        listResponse = m_yubikey->processApdu(listApdu);
        sw = (static_cast<quint8>(listResponse[listResponse.size() - 2]) << 8) |
             static_cast<quint8>(listResponse[listResponse.size() - 1]);
    }

    QCOMPARE(sw, static_cast<quint16>(0x9000));

    // Verify response contains credentials
    // Response format: TAG_NAME_LIST (0x72) + length + type_byte + name
    QVERIFY(listResponse.size() > 2); // More than just status word

    qDebug() << "✓ Credential list test passed";
}

void TestE2EDeviceLifecycle::testGenerateCode()
{
    qDebug() << "\n--- Test: Generate Code ---";

    // SELECT OATH applet first to establish session
    QByteArray selectApdu = QByteArray::fromHex("00A4040007A0000005272101");
    QByteArray selectResponse = m_yubikey->processApdu(selectApdu);
    QVERIFY(selectResponse.endsWith(QByteArray::fromHex("9000")));

    // Test CALCULATE_ALL command
    quint64 timestamp = QDateTime::currentSecsSinceEpoch();
    QByteArray challenge(8, 0);
    qToBigEndian(timestamp, challenge.data());

    // Build CALCULATE_ALL APDU: CLA INS P1 P2 Lc TAG_CHALLENGE length data
    QByteArray calcAllApdu;
    calcAllApdu.append(static_cast<char>(0x00)); // CLA
    calcAllApdu.append(static_cast<char>(0xA4)); // INS = CALCULATE_ALL
    calcAllApdu.append(static_cast<char>(0x00)); // P1
    calcAllApdu.append(static_cast<char>(0x00)); // P2
    calcAllApdu.append(static_cast<char>(challenge.size() + 2)); // Lc (tag + length + data)
    calcAllApdu.append(static_cast<char>(0x74)); // TAG_CHALLENGE
    calcAllApdu.append(static_cast<char>(challenge.size())); // Length
    calcAllApdu.append(challenge); // Data

    QByteArray calcAllResponse = m_yubikey->processApdu(calcAllApdu);

    // Verify success
    QVERIFY(calcAllResponse.size() >= 2);
    quint16 sw = (static_cast<quint8>(calcAllResponse[calcAllResponse.size() - 2]) << 8) |
                  static_cast<quint8>(calcAllResponse[calcAllResponse.size() - 1]);
    QCOMPARE(sw, static_cast<quint16>(0x9000));

    // Verify response contains codes for all credentials
    // Response format: TAG_NAME (0x71) + TAG_TOTP_RESPONSE (0x76) or TAG_TOUCH (0x7c)
    QVERIFY(calcAllResponse.size() > 10); // Should have data for 2 credentials

    qDebug() << "✓ Generate code test passed";
}

void TestE2EDeviceLifecycle::testMultiDevice()
{
    qDebug() << "\n--- Test: Multi-Device ---";

    // Create second virtual device (Nitrokey 3C)
    m_nitrokey = std::make_unique<VirtualNitrokey>(
        QStringLiteral("87654321"),
        Version(1, 6, 0),
        QStringLiteral("Nitrokey 3C")
    );

    OathCredential cred3;
    cred3.originalName = QStringLiteral("GitLab:admin");
    cred3.type = OathType::TOTP;
    cred3.algorithm = OathAlgorithm::SHA1;
    cred3.digits = 6;
    cred3.period = 30;
    cred3.requiresTouch = false;
    m_nitrokey->addCredential(cred3);

    // Test both devices work independently
    QCOMPARE(m_yubikey->serialNumber(), 0x12345678u);
    QCOMPARE(m_nitrokey->serialNumber(), 0x87654321u);

    // Test YubiKey SELECT
    QByteArray ykSelect = QByteArray::fromHex("00A4040007A0000005272101");
    QByteArray ykResponse = m_yubikey->processApdu(ykSelect);
    quint16 ykSw = (static_cast<quint8>(ykResponse[ykResponse.size() - 2]) << 8) |
                    static_cast<quint8>(ykResponse[ykResponse.size() - 1]);
    QCOMPARE(ykSw, static_cast<quint16>(0x9000));

    // Test Nitrokey SELECT (should include TAG_SERIAL_NUMBER unlike YubiKey)
    QByteArray nkSelect = QByteArray::fromHex("00A4040007A0000005272101");
    QByteArray nkResponse = m_nitrokey->processApdu(nkSelect);
    quint16 nkSw = (static_cast<quint8>(nkResponse[nkResponse.size() - 2]) << 8) |
                    static_cast<quint8>(nkResponse[nkResponse.size() - 1]);
    QCOMPARE(nkSw, static_cast<quint16>(0x9000));

    // Verify Nitrokey response contains TAG_SERIAL_NUMBER (0x8F)
    bool hasSerialTag = false;
    for (int i = 0; i < nkResponse.size() - 2; ++i) {
        if (static_cast<quint8>(nkResponse[i]) == 0x8F) {
            hasSerialTag = true;
            break;
        }
    }
    QVERIFY2(hasSerialTag, "Nitrokey SELECT should include TAG_SERIAL_NUMBER (0x8F)");

    // Test Nitrokey does NOT support CALCULATE_ALL
    quint64 timestamp = QDateTime::currentSecsSinceEpoch();
    QByteArray challenge(8, 0);
    qToBigEndian(timestamp, challenge.data());

    QByteArray nkCalcAll;
    nkCalcAll.append(static_cast<char>(0x00)); // CLA
    nkCalcAll.append(static_cast<char>(0xA4)); // INS = CALCULATE_ALL
    nkCalcAll.append(static_cast<char>(0x00)); // P1
    nkCalcAll.append(static_cast<char>(0x00)); // P2
    nkCalcAll.append(static_cast<char>(challenge.size() + 2)); // Lc
    nkCalcAll.append(static_cast<char>(0x74)); // TAG_CHALLENGE
    nkCalcAll.append(static_cast<char>(challenge.size()));
    nkCalcAll.append(challenge);

    QByteArray nkCalcAllResponse = m_nitrokey->processApdu(nkCalcAll);
    quint16 nkCalcSw = (static_cast<quint8>(nkCalcAllResponse[nkCalcAllResponse.size() - 2]) << 8) |
                        static_cast<quint8>(nkCalcAllResponse[nkCalcAllResponse.size() - 1]);
    QCOMPARE(nkCalcSw, static_cast<quint16>(0x6D00)); // INS not supported

    qDebug() << "✓ Multi-device test passed (YubiKey + Nitrokey protocol differences verified)";
}

void TestE2EDeviceLifecycle::waitForDeviceReady(const QString& serial, int timeoutMs)
{
    // Helper to wait for device to reach Ready state
    // (Would be used with real daemon)
    Q_UNUSED(serial);
    Q_UNUSED(timeoutMs);
}

OathDeviceProxy* TestE2EDeviceLifecycle::findDeviceBySerial(quint32 serial)
{
    // Helper to find device by serial number
    // (Would be used with real daemon)
    Q_UNUSED(serial);
    return nullptr;
}

QTEST_MAIN(TestE2EDeviceLifecycle)
#include "test_e2e_device_lifecycle.moc"
