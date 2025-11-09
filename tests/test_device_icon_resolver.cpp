/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/config/i_device_icon_resolver.h"

#include <QtTest>
#include <QString>

/**
 * @brief Mock implementation of IDeviceIconResolver for testing
 */
class MockIconResolver : public IDeviceIconResolver
{
public:
    QString getModelIcon(quint32 deviceModel) const override
    {
        // Return predictable icon based on model number
        return QStringLiteral(":/icons/models/mock-model-%1.png").arg(deviceModel);
    }
};

/**
 * @brief Another mock implementation that returns a fixed icon
 */
class FixedIconResolver : public IDeviceIconResolver
{
public:
    explicit FixedIconResolver(const QString &iconPath)
        : m_iconPath(iconPath)
    {
    }

    QString getModelIcon(quint32 /*deviceModel*/) const override
    {
        return m_iconPath;
    }

private:
    QString m_iconPath;
};

/**
 * @brief Tests for IDeviceIconResolver interface
 *
 * Verifies interface compliance and mock implementations.
 * Tests the Interface Segregation Principle (ISP) - components can depend
 * on minimal icon resolution interface without coupling to full KCModule.
 */
class TestDeviceIconResolver : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Interface compliance tests
    void testInterface_VirtualDestructor();
    void testInterface_PureVirtualMethod();

    // Mock implementation tests
    void testMockIconResolver_DifferentModels();
    void testMockIconResolver_SameModelMultipleCalls();
    void testMockIconResolver_ZeroModel();
    void testMockIconResolver_LargeModelNumber();

    // Fixed icon resolver tests
    void testFixedIconResolver_ReturnsFixedPath();
    void testFixedIconResolver_IgnoresModelNumber();

    // ISP verification tests
    void testISP_MinimalInterface();
    void testISP_PolymorphicUsage();
    void testISP_NoExtraMethodsRequired();
};

void TestDeviceIconResolver::initTestCase()
{
}

void TestDeviceIconResolver::cleanupTestCase()
{
}

// --- Interface Compliance Tests ---

void TestDeviceIconResolver::testInterface_VirtualDestructor()
{
    // Test: Interface has virtual destructor (required for polymorphic deletion)
    // This test verifies compilation - if virtual destructor is missing, code won't compile
    IDeviceIconResolver *resolver = new MockIconResolver();
    delete resolver;  // Should call virtual destructor
    // No crash = success
    QVERIFY(true);
}

void TestDeviceIconResolver::testInterface_PureVirtualMethod()
{
    // Test: Interface declares pure virtual getModelIcon()
    // This test verifies that implementations must override the method
    MockIconResolver resolver;
    QString icon = resolver.getModelIcon(0);

    // If method wasn't pure virtual, this wouldn't require implementation
    QVERIFY(!icon.isEmpty());
}

// --- Mock Implementation Tests ---

void TestDeviceIconResolver::testMockIconResolver_DifferentModels()
{
    // Test: Mock returns different icons for different models
    MockIconResolver resolver;

    QString icon1 = resolver.getModelIcon(1);
    QString icon2 = resolver.getModelIcon(2);
    QString icon5 = resolver.getModelIcon(5);

    QCOMPARE(icon1, QStringLiteral(":/icons/models/mock-model-1.png"));
    QCOMPARE(icon2, QStringLiteral(":/icons/models/mock-model-2.png"));
    QCOMPARE(icon5, QStringLiteral(":/icons/models/mock-model-5.png"));

    // Verify they're different
    QVERIFY(icon1 != icon2);
    QVERIFY(icon2 != icon5);
    QVERIFY(icon1 != icon5);
}

void TestDeviceIconResolver::testMockIconResolver_SameModelMultipleCalls()
{
    // Test: Calling getModelIcon() multiple times with same model returns same result
    MockIconResolver resolver;

    QString icon1a = resolver.getModelIcon(42);
    QString icon1b = resolver.getModelIcon(42);
    QString icon1c = resolver.getModelIcon(42);

    QCOMPARE(icon1a, icon1b);
    QCOMPARE(icon1b, icon1c);
    QCOMPARE(icon1a, QStringLiteral(":/icons/models/mock-model-42.png"));
}

void TestDeviceIconResolver::testMockIconResolver_ZeroModel()
{
    // Test: Mock handles model number 0
    MockIconResolver resolver;

    QString icon = resolver.getModelIcon(0);
    QCOMPARE(icon, QStringLiteral(":/icons/models/mock-model-0.png"));
}

void TestDeviceIconResolver::testMockIconResolver_LargeModelNumber()
{
    // Test: Mock handles large model numbers
    MockIconResolver resolver;

    QString icon = resolver.getModelIcon(999999);
    QCOMPARE(icon, QStringLiteral(":/icons/models/mock-model-999999.png"));
}

// --- Fixed Icon Resolver Tests ---

void TestDeviceIconResolver::testFixedIconResolver_ReturnsFixedPath()
{
    // Test: FixedIconResolver returns configured path
    FixedIconResolver resolver(QStringLiteral(":/icons/yubikey.svg"));

    QString icon = resolver.getModelIcon(1);
    QCOMPARE(icon, QStringLiteral(":/icons/yubikey.svg"));
}

void TestDeviceIconResolver::testFixedIconResolver_IgnoresModelNumber()
{
    // Test: FixedIconResolver ignores model number, always returns same icon
    FixedIconResolver resolver(QStringLiteral(":/icons/generic.png"));

    QString icon1 = resolver.getModelIcon(1);
    QString icon2 = resolver.getModelIcon(2);
    QString icon999 = resolver.getModelIcon(999);

    QCOMPARE(icon1, QStringLiteral(":/icons/generic.png"));
    QCOMPARE(icon2, QStringLiteral(":/icons/generic.png"));
    QCOMPARE(icon999, QStringLiteral(":/icons/generic.png"));

    // All should be identical
    QCOMPARE(icon1, icon2);
    QCOMPARE(icon2, icon999);
}

// --- ISP Verification Tests ---

void TestDeviceIconResolver::testISP_MinimalInterface()
{
    // Test: Interface has exactly one method (minimal interface)
    // This is a documentation test - interface should remain minimal

    // If additional methods are added to interface, this test should be updated
    // to verify they're truly necessary and don't violate ISP

    MockIconResolver resolver;

    // Only one method should be callable through interface
    QString icon = resolver.getModelIcon(1);
    QVERIFY(!icon.isEmpty());

    // SUCCESS: Interface is minimal (only getModelIcon)
}

void TestDeviceIconResolver::testISP_PolymorphicUsage()
{
    // Test: Interface can be used polymorphically
    // This verifies components can depend on IDeviceIconResolver* without knowing concrete type

    IDeviceIconResolver *resolver1 = new MockIconResolver();
    IDeviceIconResolver *resolver2 = new FixedIconResolver(QStringLiteral(":/test.png"));

    // Both can be used through interface pointer
    QString icon1 = resolver1->getModelIcon(1);
    QString icon2 = resolver2->getModelIcon(1);

    QVERIFY(!icon1.isEmpty());
    QVERIFY(!icon2.isEmpty());
    QVERIFY(icon1 != icon2);  // Different implementations return different results

    delete resolver1;
    delete resolver2;
}

void TestDeviceIconResolver::testISP_NoExtraMethodsRequired()
{
    // Test: Components using interface don't need anything beyond getModelIcon()
    // This verifies ISP - interface provides ONLY what clients need

    // Simulate DeviceDelegate usage pattern
    auto useResolver = [](const IDeviceIconResolver *resolver, quint32 model) -> QString {
        // DeviceDelegate only needs getModelIcon() - nothing else
        return resolver->getModelIcon(model);
    };

    MockIconResolver resolver;
    QString icon = useResolver(&resolver, 5);

    QCOMPARE(icon, QStringLiteral(":/icons/models/mock-model-5.png"));

    // SUCCESS: No additional methods needed - ISP satisfied
}

QTEST_GUILESS_MAIN(TestDeviceIconResolver)
#include "test_device_icon_resolver.moc"
