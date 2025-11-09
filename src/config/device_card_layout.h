/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QRect>
#include <QStyleOptionViewItem>

namespace YubiKeyOath {
namespace Config {

/**
 * @brief Layout calculator for device card visual elements
 *
 * Responsible for calculating positions and sizes of all UI elements
 * in a device card: icon, name, status, last seen, and action buttons.
 *
 * Extracted from DeviceDelegate to follow Single Responsibility Principle.
 */
class DeviceCardLayout
{
public:
    /**
     * @brief Structure holding rectangles for all device card elements
     */
    struct ButtonRects
    {
        QRect iconRect;              ///< Device icon area
        QRect nameRect;              ///< Device name text area
        QRect statusRect;            ///< Status indicator area
        QRect lastSeenRect;          ///< Last seen timestamp area
        QRect authorizeButton;       ///< Authorize button area
        QRect changePasswordButton;  ///< Change password button area
        QRect forgetButton;          ///< Forget device button area
    };

    /**
     * @brief Calculates positions and sizes for all device card elements
     * @param option Qt style option containing view dimensions
     * @return ButtonRects structure with all calculated rectangles
     *
     * Layout structure:
     * - Icon: 64x64px on the left
     * - Device name: Right of icon, top-aligned
     * - Status indicator: Below name
     * - Last seen: Below status (only for disconnected devices)
     * - Buttons: Right-aligned, vertically centered (Authorize, Change Password, Forget)
     */
    static ButtonRects calculateButtonRects(const QStyleOptionViewItem &option);
};

} // namespace Config
} // namespace YubiKeyOath
