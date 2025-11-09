/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <memory>

#include "../src/shared/dbus/yubikey_manager_proxy.h"
#include "../src/shared/dbus/yubikey_device_proxy.h"
#include "../src/shared/dbus/yubikey_credential_proxy.h"
#include "../src/daemon/dbus/yubikey_manager_object.h"  // For ManagedObjectMap

using namespace YubiKeyOath::Shared;

class TestYubiKeyProxy : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Manager proxy tests
    void testManagerProxyConnection();
    void testGetManagedObjects();
    void testManagerProxyDeviceList();
    void testManagerProxyCredentialList();

    // Device proxy tests
    void testDeviceProxyProperties();
    void testDeviceProxyCredentials();
    void testDeviceProxyMethods();

    // Credential proxy tests
    void testCredentialProxyProperties();
    void testCredentialProxyGenerateCode();

    // Signal tests
    void testDeviceConnectedSignal();
    void testCredentialsChangedSignal();

private:
    void printDebugInfo();
    bool isDaemonAvailable();

    // Helper to get manager proxy
    YubiKeyManagerProxy* managerProxy() {
        return YubiKeyManagerProxy::instance();
    }
};

void TestYubiKeyProxy::initTestCase()
{
    qDebug() << "=== TestYubiKeyProxy: Starting test suite ===";

    // Check if daemon is available
    if (!isDaemonAvailable()) {
        QSKIP("YubiKey OATH daemon is not running. Start daemon with: systemctl --user start yubikey-oath-daemon.service");
    }

    // Wait a bit for daemon to initialize
    QTest::qWait(500);

    printDebugInfo();
}

void TestYubiKeyProxy::cleanupTestCase()
{
    qDebug() << "=== TestYubiKeyProxy: Test suite finished ===";
}

bool TestYubiKeyProxy::isDaemonAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface("pl.jkolo.yubikey.oath.daemon",
                         "/pl/jkolo/yubikey/oath",
                         "org.freedesktop.DBus.ObjectManager",
                         bus);

    return iface.isValid();
}

void TestYubiKeyProxy::printDebugInfo()
{
    qDebug() << "\n=== Daemon State ===";
    qDebug() << "Daemon available:" << managerProxy()->isDaemonAvailable();
    qDebug() << "Devices count:" << managerProxy()->devices().size();
    qDebug() << "Total credentials:" << managerProxy()->getAllCredentials().size();

    qDebug() << "\n=== Devices ===";
    const auto devices = managerProxy()->devices();
    for (auto *device : devices) {
        qDebug() << "Device:" << device->serialNumber();
        qDebug() << "  Name:" << device->name();
        qDebug() << "  Connected:" << device->isConnected();
        qDebug() << "  Requires password:" << device->requiresPassword();
        qDebug() << "  Has valid password:" << device->hasValidPassword();
        qDebug() << "  Credentials count:" << device->credentials().size();
    }

    qDebug() << "\n=== Credentials ===";
    const auto credentials = managerProxy()->getAllCredentials();
    int count = 0;
    for (auto *cred : credentials) {
        qDebug() << "Credential" << ++count << ":" << cred->name();
        qDebug() << "  Issuer:" << cred->issuer();
        qDebug() << "  Username:" << cred->account();
        qDebug() << "  Type:" << cred->type();
        qDebug() << "  Requires touch:" << cred->requiresTouch();
        qDebug() << "  Device:" << cred->deviceId();
        if (count >= 5) {
            qDebug() << "... and" << (credentials.size() - count) << "more";
            break;
        }
    }
    qDebug() << "";
}

void TestYubiKeyProxy::testManagerProxyConnection()
{
    qDebug() << "\n=== Test: Manager Proxy Connection ===";

    QVERIFY(managerProxy()->isDaemonAvailable());
}

void TestYubiKeyProxy::testGetManagedObjects()
{
    qDebug() << "\n=== Test: GetManagedObjects Raw D-Bus Call ===";

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface("pl.jkolo.yubikey.oath.daemon",
                         "/pl/jkolo/yubikey/oath",
                         "org.freedesktop.DBus.ObjectManager",
                         bus);

    QVERIFY2(iface.isValid(), "ObjectManager interface is not valid");

    // Use correct type - ManagedObjectMap not QVariantMap
    QDBusReply<ManagedObjectMap> reply = iface.call("GetManagedObjects");

    if (!reply.isValid()) {
        qWarning() << "GetManagedObjects failed:" << reply.error().message();
        QFAIL("GetManagedObjects D-Bus call failed");
    }

    ManagedObjectMap objects = reply.value();
    qDebug() << "GetManagedObjects returned" << objects.size() << "objects";

    QVERIFY(objects.size() > 0);

    // Print first few object paths
    int count = 0;
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        qDebug() << "  Object path:" << it.key().path();
        if (++count >= 5) {
            qDebug() << "  ... and" << (objects.size() - count) << "more";
            break;
        }
    }
}

void TestYubiKeyProxy::testManagerProxyDeviceList()
{
    qDebug() << "\n=== Test: Manager Proxy Device List ===";

    const auto devices = managerProxy()->devices();
    qDebug() << "Found" << devices.size() << "devices";

    QVERIFY2(devices.size() > 0, "No devices found. Is YubiKey connected?");

    for (auto *device : devices) {
        QVERIFY(device != nullptr);
        QVERIFY(device->serialNumber() != 0);
        qDebug() << "  Device:" << device->serialNumber() << "-" << device->name();
    }
}

void TestYubiKeyProxy::testManagerProxyCredentialList()
{
    qDebug() << "\n=== Test: Manager Proxy Credential List ===";

    const auto credentials = managerProxy()->getAllCredentials();
    qDebug() << "Found" << credentials.size() << "credentials";

    QVERIFY2(credentials.size() > 0, "No credentials found. Add credentials to YubiKey first.");

    for (auto *cred : credentials) {
        QVERIFY(cred != nullptr);
        QVERIFY(!cred->name().isEmpty());
        QVERIFY(!cred->deviceId().isEmpty());
    }
}

void TestYubiKeyProxy::testDeviceProxyProperties()
{
    qDebug() << "\n=== Test: Device Proxy Properties ===";

    const auto devices = managerProxy()->devices();
    QVERIFY(devices.size() > 0);

    YubiKeyDeviceProxy *device = devices.first();
    QVERIFY(device != nullptr);

    qDebug() << "Testing device:" << device->serialNumber();

    // Test all properties
    QVERIFY(device->serialNumber() != 0);
    QVERIFY(!device->name().isEmpty());
    QVERIFY(device->isConnected() == true); // Should be connected

    qDebug() << "  serialNumber:" << device->serialNumber();
    qDebug() << "  name:" << device->name();
    qDebug() << "  isConnected:" << device->isConnected();
    qDebug() << "  requiresPassword:" << device->requiresPassword();
    qDebug() << "  hasValidPassword:" << device->hasValidPassword();
}

void TestYubiKeyProxy::testDeviceProxyCredentials()
{
    qDebug() << "\n=== Test: Device Proxy Credentials ===";

    const auto devices = managerProxy()->devices();
    QVERIFY(devices.size() > 0);

    YubiKeyDeviceProxy *device = devices.first();
    const auto credentials = device->credentials();

    qDebug() << "Device" << device->serialNumber() << "has" << credentials.size() << "credentials";
    QVERIFY2(credentials.size() > 0, "Device has no credentials");

    for (auto *cred : credentials) {
        QVERIFY(cred != nullptr);
        QVERIFY(!cred->name().isEmpty());
        QVERIFY(!cred->deviceId().isEmpty());  // Credential has device reference (internal ID)
        qDebug() << "  Credential:" << cred->name();
    }
}

void TestYubiKeyProxy::testDeviceProxyMethods()
{
    qDebug() << "\n=== Test: Device Proxy Methods ===";

    const auto devices = managerProxy()->devices();
    QVERIFY(devices.size() > 0);

    YubiKeyDeviceProxy *device = devices.first();

    // Test toDeviceInfo conversion
    DeviceInfo info = device->toDeviceInfo();
    QVERIFY(info.serialNumber != 0);
    QVERIFY(!info.deviceName.isEmpty());
    QCOMPARE(info.serialNumber, device->serialNumber());
    QCOMPARE(info.deviceName, device->name());
    QCOMPARE(info.isConnected, device->isConnected());

    qDebug() << "  toDeviceInfo() works correctly";
}

void TestYubiKeyProxy::testCredentialProxyProperties()
{
    qDebug() << "\n=== Test: Credential Proxy Properties ===";

    const auto credentials = managerProxy()->getAllCredentials();
    QVERIFY(credentials.size() > 0);

    YubiKeyCredentialProxy *cred = credentials.first();
    QVERIFY(cred != nullptr);

    qDebug() << "Testing credential:" << cred->name();

    // Test all properties
    QVERIFY(!cred->name().isEmpty());
    QVERIFY(!cred->deviceId().isEmpty());
    QVERIFY(!cred->type().isEmpty());

    qDebug() << "  name:" << cred->name();
    qDebug() << "  issuer:" << cred->issuer();
    qDebug() << "  username:" << cred->account();
    qDebug() << "  type:" << cred->type();
    qDebug() << "  algorithm:" << cred->algorithm();
    qDebug() << "  digits:" << cred->digits();
    qDebug() << "  period:" << cred->period();
    qDebug() << "  requiresTouch:" << cred->requiresTouch();
    qDebug() << "  deviceId:" << cred->deviceId();

    // Verify type
    QVERIFY(cred->type() == "TOTP" || cred->type() == "HOTP");

    // Verify digits
    QVERIFY(cred->digits() >= 6 && cred->digits() <= 8);

    // If TOTP, verify period
    if (cred->type() == "TOTP") {
        QVERIFY(cred->period() > 0);
    }
}

void TestYubiKeyProxy::testCredentialProxyGenerateCode()
{
    qDebug() << "\n=== Test: Credential Proxy Generate Code ===";

    const auto credentials = managerProxy()->getAllCredentials();
    QVERIFY(credentials.size() > 0);

    // Find a non-touch credential
    YubiKeyCredentialProxy *cred = nullptr;
    for (auto *c : credentials) {
        if (!c->requiresTouch()) {
            cred = c;
            break;
        }
    }

    if (!cred) {
        QSKIP("No non-touch credentials found. Cannot test generateCode without user interaction.");
    }

    qDebug() << "Testing generateCode for:" << cred->name();

    GenerateCodeResult result = cred->generateCode();

    qDebug() << "  Generated code:" << result.code;
    qDebug() << "  Valid until:" << result.validUntil;

    QVERIFY2(!result.code.isEmpty(), "Generated code is empty");
    QVERIFY2(result.code.length() == cred->digits(), "Generated code has wrong number of digits");

    // Verify code contains only digits
    for (const QChar &ch : result.code) {
        QVERIFY2(ch.isDigit(), "Generated code contains non-digit characters");
    }

    // If TOTP, verify validUntil
    if (cred->type() == "TOTP") {
        QVERIFY2(result.validUntil > 0, "TOTP code has invalid validUntil");
        qint64 now = QDateTime::currentSecsSinceEpoch();
        QVERIFY2(result.validUntil > now, "TOTP code validUntil is in the past");
        QVERIFY2(result.validUntil <= now + cred->period(), "TOTP code validUntil is too far in future");
    }
}

void TestYubiKeyProxy::testDeviceConnectedSignal()
{
    qDebug() << "\n=== Test: Device Connected Signal ===";
    qDebug() << "Note: This test only verifies signal setup, not actual connection events.";

    QSignalSpy spy(managerProxy(), &YubiKeyManagerProxy::deviceConnected);
    QVERIFY(spy.isValid());

    qDebug() << "  deviceConnected signal is properly configured";
}

void TestYubiKeyProxy::testCredentialsChangedSignal()
{
    qDebug() << "\n=== Test: Credentials Changed Signal ===";
    qDebug() << "Note: This test only verifies signal setup, not actual change events.";

    QSignalSpy spy(managerProxy(), &YubiKeyManagerProxy::credentialsChanged);
    QVERIFY(spy.isValid());

    qDebug() << "  credentialsChanged signal is properly configured";
}

QTEST_MAIN(TestYubiKeyProxy)
#include "test_yubikey_proxy.moc"
