/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_card_painter.h"

#include <KLocalizedString>
#include <QApplication>
#include <QIcon>
#include <QPen>
#include <QStyle>

namespace YubiKeyOath {
namespace Config {

void DeviceCardPainter::drawCardBackground(QPainter *painter,
                                           const QStyleOptionViewItem &option,
                                           const QRect &rect)
{
    painter->save();

    // Determine background color based on state
    QColor bgColor = option.palette.base().color();
    if (option.state & QStyle::State_Selected) {
        bgColor = option.palette.highlight().color().lighter(160);
    } else if (option.state & QStyle::State_MouseOver) {
        bgColor = option.palette.alternateBase().color();
    }

    // Fill background
    painter->fillRect(rect.adjusted(4, 2, -4, -2), bgColor);

    // Draw subtle border
    painter->setPen(QPen(option.palette.mid().color(), 1));
    painter->drawRoundedRect(rect.adjusted(4, 2, -4, -2), 4, 4);

    painter->restore();
}

void DeviceCardPainter::drawDeviceIcon(QPainter *painter,
                                       const QString &iconName,
                                       const QRect &rect)
{
    painter->save();

    // Load icon from theme (automatic size/fallback selection)
    const QIcon icon = QIcon::fromTheme(iconName);
    if (!icon.isNull()) {
        icon.paint(painter, rect);
    }

    painter->restore();
}

void DeviceCardPainter::drawDeviceName(QPainter *painter,
                                       const QString &deviceName,
                                       const QRect &rect,
                                       const QStyleOptionViewItem &option)
{
    painter->save();

    painter->setPen(option.palette.text().color());
    QFont nameFont = option.font;
    nameFont.setPointSize(nameFont.pointSize() + 2);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, deviceName);

    painter->restore();
}

void DeviceCardPainter::drawStatusIndicator(QPainter *painter,
                                            const QString &statusText,
                                            const QColor &statusColor,
                                            const QRect &rect)
{
    painter->save();

    // Draw colored dot
    const int dotSize = 8;
    const int dotX = rect.left();
    const int dotY = rect.center().y() - (dotSize / 2);
    painter->setBrush(statusColor);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(dotX, dotY, dotSize, dotSize);

    // Draw status text
    painter->setPen(qApp->palette().text().color());
    const QRect textRect = rect.adjusted(dotSize + 8, 0, 0, 0);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, statusText);

    painter->restore();
}

void DeviceCardPainter::drawLastSeen(QPainter *painter,
                                     const QString &lastSeenText,
                                     const QRect &rect,
                                     const QStyleOptionViewItem &option)
{
    painter->save();

    QFont lastSeenFont = option.font;
    lastSeenFont.setPointSize(lastSeenFont.pointSize() - 2);
    painter->setFont(lastSeenFont);
    painter->setPen(option.palette.mid().color());

    const QString text = i18n("Last seen: %1", lastSeenText);
    painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, text);

    painter->restore();
}

void DeviceCardPainter::drawButton(QPainter *painter,
                                   const QRect &rect,
                                   const QString &iconName,
                                   bool hovered,
                                   const QString &text)
{
    painter->save();

    // Draw button background
    QColor buttonColor = qApp->palette().button().color();
    if (hovered) {
        buttonColor = buttonColor.lighter(110);
    }
    painter->fillRect(rect, buttonColor);

    // Draw button border
    painter->setPen(QPen(qApp->palette().mid().color(), 1));
    painter->drawRoundedRect(rect, 4, 4);

    // Draw icon
    const QIcon icon = QIcon::fromTheme(iconName);
    if (!icon.isNull()) {
        const int iconSize = 16;
        const int iconX = text.isEmpty() ? rect.center().x() - (iconSize / 2) : rect.left() + 8;
        const int iconY = rect.center().y() - (iconSize / 2);
        const QRect iconRect(iconX, iconY, iconSize, iconSize);
        icon.paint(painter, iconRect);
    }

    // Draw text if provided
    if (!text.isEmpty()) {
        painter->setPen(qApp->palette().buttonText().color());
        const QRect textRect = rect.adjusted(28, 0, -4, 0);  // Leave space for icon
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }

    painter->restore();
}

} // namespace Config
} // namespace YubiKeyOath
