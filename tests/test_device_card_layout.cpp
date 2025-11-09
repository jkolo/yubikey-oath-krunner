/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "../src/config/device_card_layout.h"

#include <QtTest>
#include <QStyleOptionViewItem>
#include <QRect>

using namespace YubiKeyOath::Config;

/**
 * @brief Tests for DeviceCardLayout
 *
 * Verifies layout calculations for device card UI elements.
 * Tests positioning, sizing, and constraints.
 */
class TestDeviceCardLayout : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Basic calculation tests
    void testCalculateButtonRects_BasicLayout();
    void testCalculateButtonRects_AllRectsPopulated();
    void testCalculateButtonRects_NoOverlap();
    void testCalculateButtonRects_WithinBounds();

    // Size and position tests
    void testCalculateButtonRects_IconSize();
    void testCalculateButtonRects_ButtonSizes();
    void testCalculateButtonRects_Margins();
    void testCalculateButtonRects_VerticalCentering();

    // Different card sizes
    void testCalculateButtonRects_SmallCard();
    void testCalculateButtonRects_LargeCard();
    void testCalculateButtonRects_WideCard();
    void testCalculateButtonRects_NarrowCard();

private:
    QStyleOptionViewItem createOption(int x, int y, int width, int height) const;
    bool rectsOverlap(const QRect &a, const QRect &b) const;
};

void TestDeviceCardLayout::initTestCase()
{
}

void TestDeviceCardLayout::cleanupTestCase()
{
}

QStyleOptionViewItem TestDeviceCardLayout::createOption(int x, int y, int width, int height) const
{
    QStyleOptionViewItem option;
    option.rect = QRect(x, y, width, height);
    return option;
}

bool TestDeviceCardLayout::rectsOverlap(const QRect &a, const QRect &b) const
{
    return a.intersects(b);
}

// --- Basic Calculation Tests ---

void TestDeviceCardLayout::testCalculateButtonRects_BasicLayout()
{
    // Test: Basic layout calculation works
    QStyleOptionViewItem option = createOption(0, 0, 800, 100);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // All rects should be valid (non-empty)
    QVERIFY(!rects.iconRect.isEmpty());
    QVERIFY(!rects.nameRect.isEmpty());
    QVERIFY(!rects.statusRect.isEmpty());
    QVERIFY(!rects.lastSeenRect.isEmpty());
    QVERIFY(!rects.authorizeButton.isEmpty());
    QVERIFY(!rects.changePasswordButton.isEmpty());
    QVERIFY(!rects.forgetButton.isEmpty());
}

void TestDeviceCardLayout::testCalculateButtonRects_AllRectsPopulated()
{
    // Test: All 7 rectangles are calculated
    QStyleOptionViewItem option = createOption(0, 0, 600, 80);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // Verify all rects have positive dimensions
    QVERIFY(rects.iconRect.width() > 0);
    QVERIFY(rects.iconRect.height() > 0);
    QVERIFY(rects.nameRect.width() > 0);
    QVERIFY(rects.nameRect.height() > 0);
    QVERIFY(rects.statusRect.width() > 0);
    QVERIFY(rects.statusRect.height() > 0);
    QVERIFY(rects.lastSeenRect.width() > 0);
    QVERIFY(rects.lastSeenRect.height() > 0);
    QVERIFY(rects.authorizeButton.width() > 0);
    QVERIFY(rects.authorizeButton.height() > 0);
    QVERIFY(rects.changePasswordButton.width() > 0);
    QVERIFY(rects.changePasswordButton.height() > 0);
    QVERIFY(rects.forgetButton.width() > 0);
    QVERIFY(rects.forgetButton.height() > 0);
}

void TestDeviceCardLayout::testCalculateButtonRects_NoOverlap()
{
    // Test: Critical elements don't overlap
    QStyleOptionViewItem option = createOption(0, 0, 800, 100);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // Icon shouldn't overlap with text
    QVERIFY(!rectsOverlap(rects.iconRect, rects.nameRect));
    QVERIFY(!rectsOverlap(rects.iconRect, rects.statusRect));

    // Buttons shouldn't overlap each other
    QVERIFY(!rectsOverlap(rects.authorizeButton, rects.changePasswordButton));
    QVERIFY(!rectsOverlap(rects.changePasswordButton, rects.forgetButton));
    QVERIFY(!rectsOverlap(rects.authorizeButton, rects.forgetButton));

    // Buttons shouldn't overlap with text
    QVERIFY(!rectsOverlap(rects.nameRect, rects.forgetButton));
}

void TestDeviceCardLayout::testCalculateButtonRects_WithinBounds()
{
    // Test: All elements stay within option.rect
    QStyleOptionViewItem option = createOption(0, 0, 800, 100);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    QVERIFY(option.rect.contains(rects.iconRect));
    QVERIFY(option.rect.contains(rects.nameRect));
    QVERIFY(option.rect.contains(rects.statusRect));
    QVERIFY(option.rect.contains(rects.lastSeenRect));
    QVERIFY(option.rect.contains(rects.authorizeButton));
    QVERIFY(option.rect.contains(rects.changePasswordButton));
    QVERIFY(option.rect.contains(rects.forgetButton));
}

// --- Size and Position Tests ---

void TestDeviceCardLayout::testCalculateButtonRects_IconSize()
{
    // Test: Icon is 64x64 (as per spec)
    QStyleOptionViewItem option = createOption(0, 0, 800, 100);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    QCOMPARE(rects.iconRect.width(), 64);
    QCOMPARE(rects.iconRect.height(), 64);
}

void TestDeviceCardLayout::testCalculateButtonRects_ButtonSizes()
{
    // Test: Buttons have correct sizes
    QStyleOptionViewItem option = createOption(0, 0, 800, 100);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // Change Password and Forget buttons: 32x32
    QCOMPARE(rects.changePasswordButton.width(), 32);
    QCOMPARE(rects.changePasswordButton.height(), 32);
    QCOMPARE(rects.forgetButton.width(), 32);
    QCOMPARE(rects.forgetButton.height(), 32);

    // Authorize button: wider (96px) for text
    QCOMPARE(rects.authorizeButton.width(), 96);
    QCOMPARE(rects.authorizeButton.height(), 32);
}

void TestDeviceCardLayout::testCalculateButtonRects_Margins()
{
    // Test: Left margin is 12px
    QStyleOptionViewItem option = createOption(0, 0, 800, 100);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // Icon should have 12px left margin
    QCOMPARE(rects.iconRect.left(), 12);
}

void TestDeviceCardLayout::testCalculateButtonRects_VerticalCentering()
{
    // Test: Icon and buttons are vertically centered
    QStyleOptionViewItem option = createOption(0, 0, 800, 100);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // Icon should be vertically centered
    int iconCenterY = rects.iconRect.center().y();
    int cardCenterY = option.rect.center().y();
    QCOMPARE(iconCenterY, cardCenterY);

    // All buttons should be at same vertical position
    QCOMPARE(rects.authorizeButton.top(), rects.changePasswordButton.top());
    QCOMPARE(rects.changePasswordButton.top(), rects.forgetButton.top());

    // Buttons should be vertically centered
    int buttonCenterY = rects.forgetButton.center().y();
    QCOMPARE(buttonCenterY, cardCenterY);
}

// --- Different Card Sizes ---

void TestDeviceCardLayout::testCalculateButtonRects_SmallCard()
{
    // Test: Layout works with smaller card (600x80)
    QStyleOptionViewItem option = createOption(0, 0, 600, 80);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // All elements should still be within bounds
    QVERIFY(option.rect.contains(rects.iconRect));
    QVERIFY(option.rect.contains(rects.forgetButton));

    // Critical elements shouldn't overlap
    QVERIFY(!rectsOverlap(rects.iconRect, rects.nameRect));
    QVERIFY(!rectsOverlap(rects.authorizeButton, rects.forgetButton));
}

void TestDeviceCardLayout::testCalculateButtonRects_LargeCard()
{
    // Test: Layout works with larger card (1200x150)
    QStyleOptionViewItem option = createOption(0, 0, 1200, 150);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // Icon size should remain constant
    QCOMPARE(rects.iconRect.width(), 64);
    QCOMPARE(rects.iconRect.height(), 64);

    // All elements should be within bounds
    QVERIFY(option.rect.contains(rects.iconRect));
    QVERIFY(option.rect.contains(rects.nameRect));
    QVERIFY(option.rect.contains(rects.forgetButton));
}

void TestDeviceCardLayout::testCalculateButtonRects_WideCard()
{
    // Test: Layout adjusts to very wide card
    QStyleOptionViewItem option = createOption(0, 0, 1600, 100);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // Name rect should expand to use available width
    QVERIFY(rects.nameRect.width() > 400);  // Should be quite wide

    // Buttons should still be right-aligned
    QVERIFY(rects.forgetButton.right() < option.rect.right());
}

void TestDeviceCardLayout::testCalculateButtonRects_NarrowCard()
{
    // Test: Layout handles narrow card gracefully
    QStyleOptionViewItem option = createOption(0, 0, 400, 100);

    DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // Elements should fit (though tightly)
    QVERIFY(option.rect.contains(rects.iconRect));
    QVERIFY(option.rect.contains(rects.forgetButton));

    // Name rect might be narrow but should be positive
    QVERIFY(rects.nameRect.width() > 0);
}

QTEST_GUILESS_MAIN(TestDeviceCardLayout)
#include "test_device_card_layout.moc"
