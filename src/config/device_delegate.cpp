/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "device_delegate.h"
#include "device_card_layout.h"
#include "device_card_painter.h"
#include "yubikey_config.h"
#include "yubikey_device_model.h"
#include "logging_categories.h"

#include <KLocalizedString>

#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QLineEdit>
#include <QKeyEvent>
#include <QIcon>

using namespace YubiKeyOath::Config;

/**
 * @brief Event filter for handling Enter/Escape keys in inline editors
 *
 * This filter intercepts Enter and Escape key presses to prevent them from
 * propagating to the parent dialog. Instead, it closes the QLineEdit which
 * triggers editingFinished signal connected in createEditor().
 *
 * Without this filter, Enter key would propagate to the parent dialog and close it.
 */
class LineEditEventFilter : public QObject
{
public:
    explicit LineEditEventFilter(QObject *parent = nullptr)
        : QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress) {
            const auto *keyEvent = dynamic_cast<const QKeyEvent *>(event);
            if (keyEvent == nullptr) {
                return QObject::eventFilter(obj, event);
            }

            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                // Close the editor - this will trigger editingFinished signal
                auto *lineEdit = qobject_cast<QLineEdit *>(obj);
                if (lineEdit != nullptr) {
                    qCDebug(YubiKeyConfigLog) << "LineEditEventFilter: Enter pressed - closing editor";
                    lineEdit->clearFocus();  // Triggers editingFinished
                }
                return true;  // Consume event - prevent propagation to parent dialog
            }
            if (keyEvent->key() == Qt::Key_Escape) {
                // Cancel editing by clearing focus without accepting changes
                auto *lineEdit = qobject_cast<QLineEdit *>(obj);
                if (lineEdit != nullptr) {
                    qCDebug(YubiKeyConfigLog) << "LineEditEventFilter: Escape pressed - canceling edit";
                    lineEdit->undo();  // Revert to original text
                    lineEdit->clearFocus();
                }
                return true;  // Consume event
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

/**
 * @brief Helper class to emit delegate signals from const context
 *
 * This helper is needed because createEditor() is const but we need to emit signals.
 * It stores a non-const pointer to the delegate and provides methods to emit signals.
 */
class DelegateSignalEmitter : public QObject
{
    Q_OBJECT
public:
    explicit DelegateSignalEmitter(DeviceDelegate *delegate, QObject *parent = nullptr)
        : QObject(parent), m_delegate(delegate) {}

    void emitCommitData(QWidget *editor) const
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        Q_EMIT m_delegate->commitData(editor);
    }

    void emitCloseEditor(QWidget *editor, QAbstractItemDelegate::EndEditHint hint) const
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        Q_EMIT m_delegate->closeEditor(editor, hint);
    }

private:
    DeviceDelegate *m_delegate;
};

DeviceDelegate::DeviceDelegate(IDeviceIconResolver *iconResolver, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_iconResolver(iconResolver)
{
}

QSize DeviceDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(index)
    // Card height: icon (64px) + margins (16px top/bottom) + extra space for Last Seen = ~110px
    return {option.rect.width(), 110};
}

void DeviceDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // Get data from model
    const QString deviceName = index.data(YubiKeyDeviceModel::DeviceNameRole).toString();
    const bool isConnected = index.data(YubiKeyDeviceModel::IsConnectedRole).toBool();
    const bool requiresPassword = index.data(YubiKeyDeviceModel::RequiresPasswordRole).toBool();
    const bool hasValidPassword = index.data(YubiKeyDeviceModel::HasValidPasswordRole).toBool();
    const bool showAuthorizeButton = index.data(YubiKeyDeviceModel::ShowAuthorizeButtonRole).toBool();
    const uint32_t deviceModel = index.data(YubiKeyDeviceModel::DeviceModelRole).toUInt();

    // Calculate button positions
    const DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

    // Draw card background
    DeviceCardPainter::drawCardBackground(painter, option, option.rect);

    // Draw device icon
    const QString iconPath = m_iconResolver->getModelIcon(deviceModel);
    DeviceCardPainter::drawDeviceIcon(painter, iconPath, rects.iconRect);

    // Draw device name
    DeviceCardPainter::drawDeviceName(painter, deviceName, rects.nameRect, option);

    // Determine status
    QString statusText;
    QColor statusColor;

    if (!isConnected) {
        statusText = i18n("Disconnected");
        statusColor = Qt::gray;
    } else if (requiresPassword && !hasValidPassword) {
        statusText = i18n("Password required");
        statusColor = QColor(255, 165, 0);  // Orange
    } else if (requiresPassword && hasValidPassword) {
        statusText = i18n("Authorized");
        statusColor = QColor(76, 175, 80);  // Green
    } else {
        statusText = i18n("Connected");
        statusColor = QColor(76, 175, 80);  // Green
    }

    // Draw status indicator
    DeviceCardPainter::drawStatusIndicator(painter, statusText, statusColor, rects.statusRect);

    // Draw Last Seen (only for disconnected devices)
    if (!isConnected) {
        const QDateTime lastSeen = index.data(YubiKeyDeviceModel::LastSeenRole).toDateTime();
        if (lastSeen.isValid()) {
            const QString lastSeenText = RelativeTimeFormatter::formatRelativeTime(lastSeen);
            DeviceCardPainter::drawLastSeen(painter, lastSeenText, rects.lastSeenRect, option);
        }
    }

    // Draw buttons
    const bool isHovered = (m_hoveredIndex == index);

    // Authorize button (only if needed)
    if (showAuthorizeButton) {
        const bool authorizeHovered = isHovered && (m_hoveredButton == QStringLiteral("authorize"));
        DeviceCardPainter::drawButton(painter, rects.authorizeButton, QStringLiteral("password-show-on"),
                                      authorizeHovered, i18n("Authorize"));
    }

    // Change Password button (only if connected)
    if (isConnected) {
        const bool passwordHovered = isHovered && (m_hoveredButton == QStringLiteral("password"));
        DeviceCardPainter::drawButton(painter, rects.changePasswordButton, QStringLiteral("lock-edit"), passwordHovered);
    }

    // Forget button (always visible)
    const bool forgetHovered = isHovered && (m_hoveredButton == QStringLiteral("forget"));
    DeviceCardPainter::drawButton(painter, rects.forgetButton, QStringLiteral("edit-delete"), forgetHovered);

    painter->restore();
}

bool DeviceDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                 const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (event->type() == QEvent::MouseMove) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast) - Safe after event type check
        const auto *mouseEvent = static_cast<const QMouseEvent *>(event);
        const DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);

        // Update hovered button
        const QString oldHoveredButton = m_hoveredButton;
        const QModelIndex oldHoveredIndex = m_hoveredIndex;

        m_hoveredIndex = index;
        m_hoveredButton.clear();

        const bool showAuthorize = index.data(YubiKeyDeviceModel::ShowAuthorizeButtonRole).toBool();
        const bool isConnected = index.data(YubiKeyDeviceModel::IsConnectedRole).toBool();

        if (showAuthorize && rects.authorizeButton.contains(mouseEvent->pos())) {
            m_hoveredButton = QStringLiteral("authorize");
        } else if (isConnected && rects.changePasswordButton.contains(mouseEvent->pos())) {
            m_hoveredButton = QStringLiteral("password");
        } else if (rects.forgetButton.contains(mouseEvent->pos())) {
            m_hoveredButton = QStringLiteral("forget");
        }

        // Request repaint if hover state changed
        if (m_hoveredButton != oldHoveredButton || m_hoveredIndex != oldHoveredIndex) {
            // Trigger repaint for both old and new hovered items
            if (oldHoveredIndex.isValid()) {
                model->dataChanged(oldHoveredIndex, oldHoveredIndex);
            }
            if (m_hoveredIndex.isValid()) {
                model->dataChanged(m_hoveredIndex, m_hoveredIndex);
            }
        }

        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast) - Safe after event type check
        const auto *mouseEvent = static_cast<const QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            return false;
        }

        const DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);
        const QString deviceId = index.data(YubiKeyDeviceModel::DeviceIdRole).toString();
        const QString deviceName = index.data(YubiKeyDeviceModel::DeviceNameRole).toString();
        const bool showAuthorize = index.data(YubiKeyDeviceModel::ShowAuthorizeButtonRole).toBool();
        const bool isConnected = index.data(YubiKeyDeviceModel::IsConnectedRole).toBool();

        qCDebug(YubiKeyConfigLog) << "DeviceDelegate: Mouse click at" << mouseEvent->pos();

        // Check which button was clicked
        if (showAuthorize && rects.authorizeButton.contains(mouseEvent->pos())) {
            qCDebug(YubiKeyConfigLog) << "DeviceDelegate: Authorize button clicked for device:" << deviceId;
            Q_EMIT authorizeClicked(deviceId, deviceName);
            return true;
        }

        if (isConnected && rects.changePasswordButton.contains(mouseEvent->pos())) {
            qCDebug(YubiKeyConfigLog) << "DeviceDelegate: Change password button clicked for device:" << deviceId;
            Q_EMIT changePasswordClicked(deviceId, deviceName);
            return true;
        }

        if (rects.forgetButton.contains(mouseEvent->pos())) {
            qCDebug(YubiKeyConfigLog) << "DeviceDelegate: Forget button clicked for device:" << deviceId;
            Q_EMIT forgetClicked(deviceId);
            return true;
        }

        // Check if device name was clicked (for editing)
        if (rects.nameRect.contains(mouseEvent->pos())) {
            qCDebug(YubiKeyConfigLog) << "DeviceDelegate: Device name clicked, requesting edit";
            Q_EMIT nameEditRequested(index);
            return true;
        }
    }

    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QWidget *DeviceDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)

    auto *editor = new QLineEdit(parent);
    editor->setFrame(true);

    // Install event filter to handle Enter/Escape keys
    // This prevents Enter from propagating to parent dialog and closing it
    auto *filter = new LineEditEventFilter(editor);
    editor->installEventFilter(filter);

    // Create signal emitter helper to work around const createEditor()
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    auto *emitter = new DelegateSignalEmitter(const_cast<DeviceDelegate*>(this), editor);

    // CRITICAL: Connect editingFinished to commit data
    // This handles focus loss (clicking elsewhere, Tab key, etc.)
    // Without this connection, changes are NOT saved when editor loses focus
    connect(editor, &QLineEdit::editingFinished, emitter, [emitter, editor]() {
        qCDebug(YubiKeyConfigLog) << "DeviceDelegate: editingFinished - committing and closing editor";
        emitter->emitCommitData(editor);
        emitter->emitCloseEditor(editor, QAbstractItemDelegate::NoHint);
    });

    qCDebug(YubiKeyConfigLog) << "DeviceDelegate: Editor created with event filter and signal connections";

    return editor;
}

void DeviceDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    const QString value = index.data(YubiKeyDeviceModel::DeviceNameRole).toString();
    auto *lineEdit = qobject_cast<QLineEdit *>(editor);
    if (lineEdit) {
        lineEdit->setText(value);
    }
}

void DeviceDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                 const QModelIndex &index) const
{
    auto *lineEdit = qobject_cast<QLineEdit *>(editor);
    if (!lineEdit) {
        return;
    }

    const QString newName = lineEdit->text().trimmed();
    if (!newName.isEmpty()) {
        const QString deviceId = index.data(YubiKeyDeviceModel::DeviceIdRole).toString();
        auto *deviceModel = qobject_cast<YubiKeyDeviceModel *>(model);
        if (deviceModel) {
            deviceModel->setDeviceName(deviceId, newName);
        }
    }
}

void DeviceDelegate::updateEditorGeometry(QWidget *editor,
                                         const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
    Q_UNUSED(index)
    // Position editor over device name area
    const DeviceCardLayout::ButtonRects rects = DeviceCardLayout::calculateButtonRects(option);
    editor->setGeometry(rects.nameRect);
}

// Include MOC-generated code for local classes with Q_OBJECT
#include "device_delegate.moc"
