/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * @file test_proxy_unit.cpp
 * @brief Unit tests for proxy classes with mocked D-Bus service
 *
 * These are TRUE unit tests that don't require a running daemon.
 * They use a mock D-Bus service to simulate daemon behavior.
 *
 * Target: >80% coverage for proxy classes
 */

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <QDebug>
#include <memory>

#include "../src/shared/dbus/oath_manager_proxy.h"
#include "../src/shared/dbus/oath_device_proxy.h"
#include "../src/shared/dbus/oath_credential_proxy.h"
#include "../src/shared/types/yubikey_value_types.h"
#include "../src/daemon/dbus/oath_manager_object.h"  // For ManagedObjectMap type

using namespace YubiKeyOath::Shared;

/**
 * @brief Mock D-Bus service simulating YubiKey OATH daemon
 *
 * This mock service registers on D-Bus and responds to method calls
 * without requiring a real YubiKey or daemon.
 */
class MockOathService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "pl.jkolo.yubikey.oath.Manager")
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")
    Q_CLASSINFO("D-Bus Interface", "pl.jkolo.yubikey.oath.Device")
    Q_CLASSINFO("D-Bus Interface", "pl.jkolo.yubikey.oath.Credential")

    Q_PROPERTY(QString Version READ version CONSTANT)

public:
    explicit MockOathService(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    QString version() const { return QStringLiteral("2.0.0-mock"); }

public Q_SLOTS:
    // ObjectManager methods
    ManagedObjectMap GetManagedObjects()
    {
        qDebug() << "MockOathService::GetManagedObjects() called";
        ManagedObjectMap result;

        // Device 1
        QDBusObjectPath devicePath1(QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1"));
        InterfacePropertiesMap deviceInterfaces1;
        QVariantMap deviceProps1;
        deviceProps1[QStringLiteral("DeviceId")] = QStringLiteral("mock_device_1");
        deviceProps1[QStringLiteral("Name")] = QStringLiteral("Mock YubiKey 1");
        deviceProps1[QStringLiteral("IsConnected")] = true;
        deviceProps1[QStringLiteral("RequiresPassword")] = false;
        deviceProps1[QStringLiteral("HasValidPassword")] = true;
        deviceInterfaces1[QStringLiteral("pl.jkolo.yubikey.oath.Device")] = deviceProps1;
        result[devicePath1] = deviceInterfaces1;

        // Credential 1
        QDBusObjectPath credPath1(QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/github_3ajdoe"));
        InterfacePropertiesMap credInterfaces1;
        QVariantMap credProps1;
        credProps1[QStringLiteral("FullName")] = QStringLiteral("GitHub:jdoe");
        credProps1[QStringLiteral("Issuer")] = QStringLiteral("GitHub");
        credProps1[QStringLiteral("Username")] = QStringLiteral("jdoe");
        credProps1[QStringLiteral("Type")] = QStringLiteral("TOTP");
        credProps1[QStringLiteral("Algorithm")] = QStringLiteral("SHA1");
        credProps1[QStringLiteral("Digits")] = 6;
        credProps1[QStringLiteral("Period")] = 30;
        credProps1[QStringLiteral("RequiresTouch")] = false;
        credProps1[QStringLiteral("DeviceId")] = QStringLiteral("mock_device_1");
        credInterfaces1[QStringLiteral("pl.jkolo.yubikey.oath.Credential")] = credProps1;
        result[credPath1] = credInterfaces1;

        // Credential 2
        QDBusObjectPath credPath2(QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/google_3ajdoe"));
        InterfacePropertiesMap credInterfaces2;
        QVariantMap credProps2;
        credProps2[QStringLiteral("FullName")] = QStringLiteral("Google:jdoe");
        credProps2[QStringLiteral("Issuer")] = QStringLiteral("Google");
        credProps2[QStringLiteral("Username")] = QStringLiteral("jdoe");
        credProps2[QStringLiteral("Type")] = QStringLiteral("TOTP");
        credProps2[QStringLiteral("Algorithm")] = QStringLiteral("SHA256");
        credProps2[QStringLiteral("Digits")] = 6;
        credProps2[QStringLiteral("Period")] = 30;
        credProps2[QStringLiteral("RequiresTouch")] = true;
        credProps2[QStringLiteral("DeviceId")] = QStringLiteral("mock_device_1");
        credInterfaces2[QStringLiteral("pl.jkolo.yubikey.oath.Credential")] = credProps2;
        result[credPath2] = credInterfaces2;

        qDebug() << "MockOathService: Returning" << result.size() << "objects";
        return result;
    }

    // Device methods
    bool SavePassword(const QString &password)
    {
        Q_UNUSED(password)
        return true;
    }

    bool ChangePassword(const QString &oldPassword, const QString &newPassword)
    {
        Q_UNUSED(oldPassword)
        Q_UNUSED(newPassword)
        return true;
    }

    void Forget()
    {
        // Mock forget
    }

    // Credential methods
    QVariantMap GenerateCode()
    {
        QVariantMap result;
        result[QStringLiteral("code")] = QStringLiteral("123456");
        result[QStringLiteral("validUntil")] = QDateTime::currentSecsSinceEpoch() + 30;
        return result;
    }

    bool CopyToClipboard()
    {
        return true;
    }

    bool TypeCode(bool fallbackToCopy)
    {
        Q_UNUSED(fallbackToCopy)
        return true;
    }

    void Delete()
    {
        // Mock delete
    }

Q_SIGNALS:
    // ObjectManager signals
    void InterfacesAdded(const QDBusObjectPath &object_path,
                        const QVariantMap &interfaces_and_properties);
    void InterfacesRemoved(const QDBusObjectPath &object_path,
                          const QStringList &interfaces);

    // Device signals
    void CredentialAdded(const QDBusObjectPath &credentialPath);
    void CredentialRemoved(const QDBusObjectPath &credentialPath);
    void nameChanged(const QString &newName);
    void connectionChanged(bool connected);
};

/**
 * @brief Unit tests for proxy classes
 *
 * Tests proxy classes with mocked D-Bus service.
 * No physical YubiKey or daemon required.
 */
class TestProxyUnit : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // YubiKeyCredentialProxy tests
    void testCredentialProxyConstruction();
    void testCredentialProxyProperties();
    void testCredentialProxyGenerateCode();
    void testCredentialProxyCopyToClipboard();
    void testCredentialProxyTypeCode();
    void testCredentialProxyToCredentialInfo();

    // YubiKeyDeviceProxy tests
    void testDeviceProxyConstruction();
    void testDeviceProxyProperties();
    void testDeviceProxyCredentialManagement();
    void testDeviceProxySavePassword();
    void testDeviceProxyForget();
    void testDeviceProxyToDeviceInfo();
    void testDeviceProxySignals();

    // YubiKeyManagerProxy tests
    void testManagerProxySingleton();
    void testManagerProxyDaemonAvailability();
    void testManagerProxyDeviceList();
    void testManagerProxyGetAllCredentials();
    void testManagerProxyRefresh();
    void testManagerProxySignals();

private:
    std::unique_ptr<MockOathService> m_mockService;
    bool registerMockOathService();
    void unregisterMockOathService();
};

void TestProxyUnit::initTestCase()
{
    qDebug() << "=== TestProxyUnit: Starting unit test suite ===";

    // Check if real daemon is running
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface(QStringLiteral("pl.jkolo.yubikey.oath.daemon"),
                        QStringLiteral("/pl/jkolo/yubikey/oath"),
                        QString(),
                        bus);

    if (iface.isValid()) {
        qWarning() << "Real YubiKey daemon is running. Unit tests require daemon to be stopped.";
        qWarning() << "To run unit tests, stop the daemon with: systemctl --user stop yubikey-oath-daemon";
        QSKIP("Cannot run unit tests while real daemon is running");
    }

    // Register mock D-Bus service
    if (!registerMockOathService()) {
        QFAIL("Failed to register mock D-Bus service");
    }

    // Wait for service registration
    QTest::qWait(100);
}

void TestProxyUnit::cleanupTestCase()
{
    unregisterMockOathService();
    qDebug() << "=== TestProxyUnit: Test suite finished ===";
}

void TestProxyUnit::init()
{
    // Reset before each test
}

void TestProxyUnit::cleanup()
{
    // Cleanup after each test
}

bool TestProxyUnit::registerMockOathService()
{
    m_mockService = std::make_unique<MockOathService>();

    QDBusConnection bus = QDBusConnection::sessionBus();

    // Register service name
    if (!bus.registerService(QStringLiteral("pl.jkolo.yubikey.oath.daemon"))) {
        qCritical() << "Failed to register D-Bus service:" << bus.lastError().message();
        return false;
    }

    // Register manager object
    if (!bus.registerObject(QStringLiteral("/pl/jkolo/yubikey/oath"),
                           m_mockService.get(),
                           QDBusConnection::ExportAllProperties |
                           QDBusConnection::ExportAllSlots |
                           QDBusConnection::ExportAllSignals)) {
        qCritical() << "Failed to register manager object:" << bus.lastError().message();
        return false;
    }

    // Register device object
    if (!bus.registerObject(QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1"),
                           m_mockService.get(),
                           QDBusConnection::ExportAllProperties |
                           QDBusConnection::ExportAllSlots |
                           QDBusConnection::ExportAllSignals)) {
        qCritical() << "Failed to register device object:" << bus.lastError().message();
        return false;
    }

    // Register credential objects
    bus.registerObject(QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/github_3ajdoe"),
                      m_mockService.get(),
                      QDBusConnection::ExportAllProperties |
                      QDBusConnection::ExportAllSlots |
                      QDBusConnection::ExportAllSignals);

    bus.registerObject(QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/google_3ajdoe"),
                      m_mockService.get(),
                      QDBusConnection::ExportAllProperties |
                      QDBusConnection::ExportAllSlots |
                      QDBusConnection::ExportAllSignals);

    qDebug() << "Mock D-Bus service registered successfully";
    return true;
}

void TestProxyUnit::unregisterMockOathService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QStringLiteral("/pl/jkolo/yubikey/oath"));
    bus.unregisterObject(QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1"));
    bus.unregisterObject(QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/github_3ajdoe"));
    bus.unregisterObject(QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/google_3ajdoe"));
    bus.unregisterService(QStringLiteral("pl.jkolo.yubikey.oath.daemon"));
    m_mockService.reset();
}

// ========== YubiKeyCredentialProxy Tests ==========

void TestProxyUnit::testCredentialProxyConstruction()
{
    qDebug() << "\n=== Test: CredentialProxy Construction ===";

    // Create properties map as expected by proxy constructor
    QVariantMap properties;
    properties[QStringLiteral("FullName")] = QStringLiteral("GitHub:jdoe");
    properties[QStringLiteral("Issuer")] = QStringLiteral("GitHub");
    properties[QStringLiteral("Username")] = QStringLiteral("jdoe");
    properties[QStringLiteral("Type")] = QStringLiteral("TOTP");
    properties[QStringLiteral("Algorithm")] = QStringLiteral("SHA1");
    properties[QStringLiteral("Digits")] = 6;
    properties[QStringLiteral("Period")] = 30;
    properties[QStringLiteral("RequiresTouch")] = false;
    properties[QStringLiteral("DeviceId")] = QStringLiteral("mock_device_1");

    OathCredentialProxy proxy(
        QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/github_3ajdoe"),
        properties
    );

    QCOMPARE(proxy.fullName(), QStringLiteral("GitHub:jdoe"));
    QCOMPARE(proxy.issuer(), QStringLiteral("GitHub"));
    QCOMPARE(proxy.username(), QStringLiteral("jdoe"));
    QCOMPARE(proxy.type(), QStringLiteral("TOTP"));
    QCOMPARE(proxy.algorithm(), QStringLiteral("SHA1"));
    QCOMPARE(proxy.digits(), 6);
    QCOMPARE(proxy.period(), 30);
    QCOMPARE(proxy.requiresTouch(), false);
    QCOMPARE(proxy.deviceId(), QStringLiteral("mock_device_1"));

    qDebug() << "✅ CredentialProxy constructed successfully";
}

void TestProxyUnit::testCredentialProxyProperties()
{
    qDebug() << "\n=== Test: CredentialProxy Properties ===";

    QVariantMap properties;
    properties[QStringLiteral("FullName")] = QStringLiteral("Google:jdoe");
    properties[QStringLiteral("Issuer")] = QStringLiteral("Google");
    properties[QStringLiteral("Username")] = QStringLiteral("jdoe");
    properties[QStringLiteral("Type")] = QStringLiteral("TOTP");
    properties[QStringLiteral("Algorithm")] = QStringLiteral("SHA256");
    properties[QStringLiteral("Digits")] = 6;
    properties[QStringLiteral("Period")] = 30;
    properties[QStringLiteral("RequiresTouch")] = true;
    properties[QStringLiteral("DeviceId")] = QStringLiteral("mock_device_1");

    OathCredentialProxy proxy(
        QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/google_3ajdoe"),
        properties
    );

    // All properties should be const/cached
    QVERIFY(!proxy.fullName().isEmpty());
    QVERIFY(!proxy.issuer().isEmpty());
    QVERIFY(!proxy.username().isEmpty());
    QVERIFY(!proxy.type().isEmpty());
    QVERIFY(!proxy.algorithm().isEmpty());
    QVERIFY(proxy.digits() >= 6 && proxy.digits() <= 8);
    QVERIFY(proxy.period() > 0);
    QVERIFY(!proxy.deviceId().isEmpty());

    qDebug() << "✅ All credential properties accessible";
}

void TestProxyUnit::testCredentialProxyGenerateCode()
{
    qDebug() << "\n=== Test: CredentialProxy GenerateCode ===";

    QVariantMap properties;
    properties[QStringLiteral("FullName")] = QStringLiteral("GitHub:jdoe");
    properties[QStringLiteral("Issuer")] = QStringLiteral("GitHub");
    properties[QStringLiteral("Username")] = QStringLiteral("jdoe");
    properties[QStringLiteral("Type")] = QStringLiteral("TOTP");
    properties[QStringLiteral("Algorithm")] = QStringLiteral("SHA1");
    properties[QStringLiteral("Digits")] = 6;
    properties[QStringLiteral("Period")] = 30;
    properties[QStringLiteral("RequiresTouch")] = false;
    properties[QStringLiteral("DeviceId")] = QStringLiteral("mock_device_1");

    OathCredentialProxy proxy(
        QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/github_3ajdoe"),
        properties
    );

    // Use async API with signal spy
    QSignalSpy spy(&proxy, &OathCredentialProxy::codeGenerated);
    proxy.generateCode();

    QVERIFY2(spy.wait(5000), "codeGenerated signal not received");
    QCOMPARE(spy.count(), 1);

    auto args = spy.takeFirst();
    QString code = args.at(0).toString();
    qint64 validUntil = args.at(1).toLongLong();
    QString error = args.at(2).toString();

    QVERIFY2(error.isEmpty(), qPrintable("Error: " + error));
    QVERIFY2(!code.isEmpty(), "Generated code should not be empty");
    QVERIFY2(code.length() == 6, "Generated code should have 6 digits");
    QVERIFY2(validUntil > 0, "validUntil should be set for TOTP");

    qDebug() << "  Code:" << code;
    qDebug() << "  Valid until:" << validUntil;
    qDebug() << "✅ GenerateCode works";
}

void TestProxyUnit::testCredentialProxyCopyToClipboard()
{
    qDebug() << "\n=== Test: CredentialProxy CopyToClipboard ===";

    QVariantMap properties;
    properties[QStringLiteral("FullName")] = QStringLiteral("GitHub:jdoe");
    properties[QStringLiteral("Issuer")] = QStringLiteral("GitHub");
    properties[QStringLiteral("Username")] = QStringLiteral("jdoe");
    properties[QStringLiteral("Type")] = QStringLiteral("TOTP");
    properties[QStringLiteral("Algorithm")] = QStringLiteral("SHA1");
    properties[QStringLiteral("Digits")] = 6;
    properties[QStringLiteral("Period")] = 30;
    properties[QStringLiteral("RequiresTouch")] = false;
    properties[QStringLiteral("DeviceId")] = QStringLiteral("mock_device_1");

    OathCredentialProxy proxy(
        QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/github_3ajdoe"),
        properties
    );

    // Use async API with signal spy
    QSignalSpy spy(&proxy, &OathCredentialProxy::clipboardCopied);
    proxy.copyToClipboard();

    QVERIFY2(spy.wait(5000), "clipboardCopied signal not received");
    QCOMPARE(spy.count(), 1);

    auto args = spy.takeFirst();
    bool success = args.at(0).toBool();
    QString error = args.at(1).toString();

    QVERIFY2(error.isEmpty(), qPrintable("Error: " + error));
    QVERIFY2(success, "CopyToClipboard should succeed");

    qDebug() << "✅ CopyToClipboard works";
}

void TestProxyUnit::testCredentialProxyTypeCode()
{
    qDebug() << "\n=== Test: CredentialProxy TypeCode ===";

    QVariantMap properties;
    properties[QStringLiteral("FullName")] = QStringLiteral("GitHub:jdoe");
    properties[QStringLiteral("Issuer")] = QStringLiteral("GitHub");
    properties[QStringLiteral("Username")] = QStringLiteral("jdoe");
    properties[QStringLiteral("Type")] = QStringLiteral("TOTP");
    properties[QStringLiteral("Algorithm")] = QStringLiteral("SHA1");
    properties[QStringLiteral("Digits")] = 6;
    properties[QStringLiteral("Period")] = 30;
    properties[QStringLiteral("RequiresTouch")] = false;
    properties[QStringLiteral("DeviceId")] = QStringLiteral("mock_device_1");

    OathCredentialProxy proxy(
        QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/github_3ajdoe"),
        properties
    );

    // Use async API with signal spy
    QSignalSpy spy(&proxy, &OathCredentialProxy::codeTyped);
    proxy.typeCode(false);

    QVERIFY2(spy.wait(5000), "codeTyped signal not received");
    QCOMPARE(spy.count(), 1);

    auto args = spy.takeFirst();
    bool success = args.at(0).toBool();
    QString error = args.at(1).toString();

    QVERIFY2(error.isEmpty(), qPrintable("Error: " + error));
    QVERIFY2(success, "TypeCode should succeed");

    qDebug() << "✅ TypeCode works";
}

void TestProxyUnit::testCredentialProxyToCredentialInfo()
{
    qDebug() << "\n=== Test: CredentialProxy ToCredentialInfo ===";

    QVariantMap properties;
    properties[QStringLiteral("FullName")] = QStringLiteral("GitHub:jdoe");
    properties[QStringLiteral("Issuer")] = QStringLiteral("GitHub");
    properties[QStringLiteral("Username")] = QStringLiteral("jdoe");
    properties[QStringLiteral("Type")] = QStringLiteral("TOTP");
    properties[QStringLiteral("Algorithm")] = QStringLiteral("SHA1");
    properties[QStringLiteral("Digits")] = 6;
    properties[QStringLiteral("Period")] = 30;
    properties[QStringLiteral("RequiresTouch")] = false;
    properties[QStringLiteral("DeviceId")] = QStringLiteral("mock_device_1");

    OathCredentialProxy proxy(
        QStringLiteral("/pl/jkolo/yubikey/oath/devices/mock_device_1/credentials/github_3ajdoe"),
        properties
    );

    CredentialInfo info = proxy.toCredentialInfo();

    QCOMPARE(info.name, proxy.fullName());
    QCOMPARE(info.issuer, proxy.issuer());
    QCOMPARE(info.account, proxy.username());
    QCOMPARE(info.requiresTouch, proxy.requiresTouch());
    QCOMPARE(info.deviceId, proxy.deviceId());

    qDebug() << "✅ ToCredentialInfo conversion works";
}

// ========== YubiKeyDeviceProxy Tests ==========

void TestProxyUnit::testDeviceProxyConstruction()
{
    qDebug() << "\n=== Test: DeviceProxy Construction ===";

    // This test will be implemented after we verify manager proxy works
    QSKIP("DeviceProxy is created by ManagerProxy, test via manager");
}

void TestProxyUnit::testDeviceProxyProperties()
{
    qDebug() << "\n=== Test: DeviceProxy Properties ===";
    QSKIP("DeviceProxy is created by ManagerProxy, test via manager");
}

void TestProxyUnit::testDeviceProxyCredentialManagement()
{
    qDebug() << "\n=== Test: DeviceProxy Credential Management ===";
    QSKIP("DeviceProxy is created by ManagerProxy, test via manager");
}

void TestProxyUnit::testDeviceProxySavePassword()
{
    qDebug() << "\n=== Test: DeviceProxy SavePassword ===";
    QSKIP("DeviceProxy is created by ManagerProxy, test via manager");
}

void TestProxyUnit::testDeviceProxyForget()
{
    qDebug() << "\n=== Test: DeviceProxy Forget ===";
    QSKIP("DeviceProxy is created by ManagerProxy, test via manager");
}

void TestProxyUnit::testDeviceProxyToDeviceInfo()
{
    qDebug() << "\n=== Test: DeviceProxy ToDeviceInfo ===";
    QSKIP("DeviceProxy is created by ManagerProxy, test via manager");
}

void TestProxyUnit::testDeviceProxySignals()
{
    qDebug() << "\n=== Test: DeviceProxy Signals ===";
    QSKIP("DeviceProxy is created by ManagerProxy, test via manager");
}

// ========== YubiKeyManagerProxy Tests ==========

void TestProxyUnit::testManagerProxySingleton()
{
    qDebug() << "\n=== Test: ManagerProxy Singleton Pattern ===";

    OathManagerProxy *instance1 = OathManagerProxy::instance();
    OathManagerProxy *instance2 = OathManagerProxy::instance();

    QVERIFY(instance1 != nullptr);
    QVERIFY(instance2 != nullptr);
    QCOMPARE(instance1, instance2); // Same instance

    qDebug() << "✅ Singleton pattern works correctly";
}

void TestProxyUnit::testManagerProxyDaemonAvailability()
{
    qDebug() << "\n=== Test: ManagerProxy Daemon Availability ===";

    OathManagerProxy *manager = OathManagerProxy::instance();

    // Mock service should be available
    QVERIFY2(manager->isDaemonAvailable(), "Mock daemon should be available");

    qDebug() << "  Daemon available:" << manager->isDaemonAvailable();
    qDebug() << "  Version:" << manager->version();
    qDebug() << "✅ Daemon availability detection works";
}

void TestProxyUnit::testManagerProxyDeviceList()
{
    qDebug() << "\n=== Test: ManagerProxy Device List ===";

    OathManagerProxy *manager = OathManagerProxy::instance();

    // Refresh to load mock data
    manager->refresh();
    QTest::qWait(200); // Wait for async refresh

    QList<OathDeviceProxy*> devices = manager->devices();

    qDebug() << "  Found" << devices.size() << "devices";
    QVERIFY2(devices.size() > 0, "Should have at least 1 mock device");

    for (auto *device : devices) {
        QVERIFY(device != nullptr);
        QVERIFY(device->serialNumber() != 0);
        qDebug() << "    Device:" << device->serialNumber() << "-" << device->name();
    }

    qDebug() << "✅ Device list works";
}

void TestProxyUnit::testManagerProxyGetAllCredentials()
{
    qDebug() << "\n=== Test: ManagerProxy GetAllCredentials ===";

    OathManagerProxy *manager = OathManagerProxy::instance();

    // Refresh to load mock data
    manager->refresh();
    QTest::qWait(200);

    QList<OathCredentialProxy*> credentials = manager->getAllCredentials();

    qDebug() << "  Found" << credentials.size() << "credentials";
    QVERIFY2(credentials.size() > 0, "Should have mock credentials");

    for (auto *cred : credentials) {
        QVERIFY(cred != nullptr);
        QVERIFY(!cred->fullName().isEmpty());
        QVERIFY(!cred->deviceId().isEmpty());
        qDebug() << "    Credential:" << cred->fullName();
    }

    qDebug() << "✅ GetAllCredentials works";
}

void TestProxyUnit::testManagerProxyRefresh()
{
    qDebug() << "\n=== Test: ManagerProxy Refresh ===";

    OathManagerProxy *manager = OathManagerProxy::instance();

    int devicesBefore = manager->devices().size();
    int credentialsBefore = manager->getAllCredentials().size();

    qDebug() << "  Before refresh: devices=" << devicesBefore
             << "credentials=" << credentialsBefore;

    // Refresh
    manager->refresh();
    QTest::qWait(200);

    int devicesAfter = manager->devices().size();
    int credentialsAfter = manager->getAllCredentials().size();

    qDebug() << "  After refresh: devices=" << devicesAfter
             << "credentials=" << credentialsAfter;

    // Should have loaded mock data
    QVERIFY2(devicesAfter > 0, "Refresh should load devices");
    QVERIFY2(credentialsAfter > 0, "Refresh should load credentials");

    qDebug() << "✅ Refresh works";
}

void TestProxyUnit::testManagerProxySignals()
{
    qDebug() << "\n=== Test: ManagerProxy Signals ===";

    OathManagerProxy *manager = OathManagerProxy::instance();

    // Test signal setup
    QSignalSpy deviceConnectedSpy(manager, &OathManagerProxy::deviceConnected);
    QSignalSpy deviceDisconnectedSpy(manager, &OathManagerProxy::deviceDisconnected);
    QSignalSpy credentialsChangedSpy(manager, &OathManagerProxy::credentialsChanged);
    QSignalSpy daemonAvailableSpy(manager, &OathManagerProxy::daemonAvailable);
    QSignalSpy daemonUnavailableSpy(manager, &OathManagerProxy::daemonUnavailable);

    QVERIFY(deviceConnectedSpy.isValid());
    QVERIFY(deviceDisconnectedSpy.isValid());
    QVERIFY(credentialsChangedSpy.isValid());
    QVERIFY(daemonAvailableSpy.isValid());
    QVERIFY(daemonUnavailableSpy.isValid());

    qDebug() << "✅ All signals are properly configured";
}

QTEST_MAIN(TestProxyUnit)
#include "test_proxy_unit.moc"
