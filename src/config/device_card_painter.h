/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QPainter>
#include <QRect>
#include <QColor>
#include <QString>
#include <QStyleOptionViewItem>

namespace YubiKeyOath {
namespace Config {

/**
 * @brief Painter for device card visual elements
 *
 * Responsible for rendering all visual components of a device card:
 * background, icon, text, status indicator, buttons.
 *
 * All methods are static - this is a stateless utility class.
 * Extracted from DeviceDelegate to follow Single Responsibility Principle.
 */
class DeviceCardPainter
{
public:
    /**
     * @brief Draws card background with selection/hover states
     * @param painter QPainter for drawing
     * @param option Style option containing palette and state
     * @param rect Rectangle to draw in
     */
    static void drawCardBackground(QPainter *painter,
                                   const QStyleOptionViewItem &option,
                                   const QRect &rect);

    /**
     * @brief Draws device icon
     * @param painter QPainter for drawing
     * @param iconPath Icon resource path
     * @param rect Rectangle for icon
     */
    static void drawDeviceIcon(QPainter *painter,
                               const QString &iconPath,
                               const QRect &rect);

    /**
     * @brief Draws device name text
     * @param painter QPainter for drawing
     * @param deviceName Device name to display
     * @param rect Rectangle for text
     * @param option Style option containing palette and font
     */
    static void drawDeviceName(QPainter *painter,
                               const QString &deviceName,
                               const QRect &rect,
                               const QStyleOptionViewItem &option);

    /**
     * @brief Draws status indicator (colored dot + text)
     * @param painter QPainter for drawing
     * @param statusText Status text to display
     * @param statusColor Color of the status dot
     * @param rect Rectangle for status indicator
     */
    static void drawStatusIndicator(QPainter *painter,
                                    const QString &statusText,
                                    const QColor &statusColor,
                                    const QRect &rect);

    /**
     * @brief Draws "Last seen" timestamp
     * @param painter QPainter for drawing
     * @param lastSeenText Relative time text (e.g., "2 hours ago")
     * @param rect Rectangle for text
     * @param option Style option containing palette and font
     */
    static void drawLastSeen(QPainter *painter,
                            const QString &lastSeenText,
                            const QRect &rect,
                            const QStyleOptionViewItem &option);

    /**
     * @brief Draws a button with icon and optional text
     * @param painter QPainter for drawing
     * @param rect Button rectangle
     * @param iconName Icon name (Freedesktop icon naming)
     * @param hovered Whether button is hovered
     * @param text Button text (optional, for text buttons like "Authorize")
     */
    static void drawButton(QPainter *painter,
                          const QRect &rect,
                          const QString &iconName,
                          bool hovered,
                          const QString &text = QString());
};

} // namespace Config
} // namespace YubiKeyOath
