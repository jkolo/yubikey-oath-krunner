/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_card_layout.h"

namespace YubiKeyOath {
namespace Config {

DeviceCardLayout::ButtonRects DeviceCardLayout::calculateButtonRects(const QStyleOptionViewItem &option)
{
    ButtonRects rects;

    const int margin = 12;
    const int iconSize = 64;
    const int buttonSize = 32;
    const int buttonSpacing = 6;

    // Icon on the left
    rects.iconRect = QRect(
        option.rect.left() + margin,
        option.rect.top() + ((option.rect.height() - iconSize) / 2),
        iconSize,
        iconSize
    );

    // Device name below icon
    rects.nameRect = QRect(
        rects.iconRect.right() + margin,
        option.rect.top() + margin,
        option.rect.width() - rects.iconRect.width() - (buttonSize * 3) - (buttonSpacing * 3) - (margin * 4),
        22
    );

    // Status indicator below name
    rects.statusRect = QRect(
        rects.nameRect.left(),
        rects.nameRect.bottom() + 4,
        rects.nameRect.width(),
        18
    );

    // Last Seen below status
    rects.lastSeenRect = QRect(
        rects.nameRect.left(),
        rects.statusRect.bottom() + 4,
        rects.nameRect.width(),
        16
    );

    // Buttons on the right side, vertically centered
    const int buttonsY = option.rect.top() + ((option.rect.height() - buttonSize) / 2);
    const int rightEdge = option.rect.right() - margin;

    // Forget button (rightmost)
    rects.forgetButton = QRect(
        rightEdge - buttonSize,
        buttonsY,
        buttonSize,
        buttonSize
    );

    // Change Password button
    rects.changePasswordButton = QRect(
        rects.forgetButton.left() - buttonSize - buttonSpacing,
        buttonsY,
        buttonSize,
        buttonSize
    );

    // Authorize button
    rects.authorizeButton = QRect(
        rects.changePasswordButton.left() - 96 - buttonSpacing,  // Wider for text
        buttonsY,
        96,
        buttonSize
    );

    return rects;
}

} // namespace Config
} // namespace YubiKeyOath
