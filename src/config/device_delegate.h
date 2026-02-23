/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "device_card_layout.h"
#include "device_card_painter.h"
#include "relative_time_formatter.h"
#include "i_device_icon_resolver.h"

#include <QStyledItemDelegate>
#include <QRect>
#include <QStyleOptionButton>
#include <memory>

/**
 * @brief Custom delegate for rendering YubiKey device list items
 *
 * Renders each device as a card with:
 * - Device icon (model-specific)
 * - Device name (editable inline)
 * - Connection status indicator
 * - Action buttons (Authorize, Change Password, Forget)
 *
 * Button clicks are handled via editorEvent() and emitted as signals.
 */
class DeviceDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit DeviceDelegate(std::unique_ptr<IDeviceIconResolver> iconResolver, QObject *parent = nullptr);

    // QStyledItemDelegate interface
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) override;

    // Inline editing of device name
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

Q_SIGNALS:
    /**
     * @brief Emitted when user clicks "Authorize" button
     * @param deviceId Device ID to authorize
     * @param deviceName Device display name
     */
    void authorizeClicked(const QString &deviceId, const QString &deviceName);

    /**
     * @brief Emitted when user clicks "Change Password" button
     * @param deviceId Device ID to change password
     * @param deviceName Device display name
     */
    void changePasswordClicked(const QString &deviceId, const QString &deviceName);

    /**
     * @brief Emitted when user clicks "Forget" button
     * @param deviceId Device ID to forget
     */
    void forgetClicked(const QString &deviceId);

    /**
     * @brief Emitted when user clicks device name to edit
     * @param index Model index of device being edited
     */
    void nameEditRequested(const QModelIndex &index);

private:
    std::unique_ptr<IDeviceIconResolver> m_iconResolver;  // Owns the icon resolver
    mutable QModelIndex m_hoveredIndex;  // Track hovered item
    mutable QString m_hoveredButton;  // Track which button is hovered ("authorize", "password", "forget")
};
