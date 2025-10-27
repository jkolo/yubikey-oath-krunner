/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_device_model.h"
#include "dbus/yubikey_manager_proxy.h"
#include "dbus/yubikey_device_proxy.h"
#include "ui/password_dialog_helper.h"
#include "logging_categories.h"

#include <KLocalizedString>
#include <QDebug>

namespace YubiKeyOath {
namespace Config {
using namespace YubiKeyOath::Shared;

YubiKeyDeviceModel::YubiKeyDeviceModel(YubiKeyManagerProxy *manager, QObject *parent)
    : QAbstractListModel(parent)
    , m_manager(manager)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Initialized with manager proxy";

    // Connect to manager proxy signals for real-time updates
    connect(m_manager, &YubiKeyManagerProxy::deviceConnected,
            this, &YubiKeyDeviceModel::onDeviceConnected);
    connect(m_manager, &YubiKeyManagerProxy::deviceDisconnected,
            this, &YubiKeyDeviceModel::onDeviceDisconnected);
    connect(m_manager, &YubiKeyManagerProxy::credentialsChanged,
            this, &YubiKeyDeviceModel::onCredentialsUpdated);

    // Initial refresh
    refreshDevices();
}

int YubiKeyDeviceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_devices.size();
}

QVariant YubiKeyDeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size()) {
        return QVariant();
    }

    const DeviceInfo &device = m_devices.at(index.row());

    switch (role) {
    case DeviceIdRole:
        return device.deviceId;
    case DeviceNameRole:
        return device.deviceName;
    case IsConnectedRole:
        return device.isConnected;
    case RequiresPasswordRole:
        return device.requiresPassword;
    case HasValidPasswordRole:
        return device.hasValidPassword;
    case ShowAuthorizeButtonRole:
        // Show "Authorize" button if device is connected, requires password, and we don't have valid password
        return device.isConnected && device.requiresPassword && !device.hasValidPassword;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> YubiKeyDeviceModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[DeviceIdRole] = "deviceId";
    roles[DeviceNameRole] = "deviceName";
    roles[IsConnectedRole] = "isConnected";
    roles[RequiresPasswordRole] = "requiresPassword";
    roles[HasValidPasswordRole] = "hasValidPassword";
    roles[ShowAuthorizeButtonRole] = "showAuthorizeButton";
    return roles;
}

void YubiKeyDeviceModel::refreshDevices()
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Refreshing device list from manager proxy";

    beginResetModel();

    // Get all devices from manager proxy and convert to DeviceInfo
    m_devices.clear();
    QList<YubiKeyDeviceProxy*> deviceProxies = m_manager->devices();
    for (const auto *deviceProxy : deviceProxies) {
        m_devices.append(deviceProxy->toDeviceInfo());
    }

    endResetModel();

    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Refresh complete, total devices:" << m_devices.size();

    for (const auto &device : m_devices) {
        qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device" << device.deviceName
                 << "connected:" << device.isConnected
                 << "requiresPassword:" << device.requiresPassword
                 << "hasValidPassword:" << device.hasValidPassword;
    }
}

void YubiKeyDeviceModel::authorizeDevice(const QString &deviceId)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Authorization requested for device:" << deviceId;

    DeviceInfo *device = findDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device not found:" << deviceId;
        return;
    }

    if (!device->isConnected) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device not connected:" << deviceId;
        return;
    }

    if (!device->requiresPassword) {
        qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device does not require password:" << deviceId;
        return;
    }

    // PasswordDialog will be opened from QML via authorizeDevice() signal/method
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device ready for authorization";
}

bool YubiKeyDeviceModel::testAndSavePassword(const QString &deviceId, const QString &password)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Testing password for device:" << deviceId;

    if (password.isEmpty()) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Empty password provided";
        Q_EMIT passwordTestFailed(deviceId, i18n("Password cannot be empty"));
        return false;
    }

    // Get device proxy
    YubiKeyDeviceProxy *deviceProxy = m_manager->getDevice(deviceId);
    if (!deviceProxy) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device not found:" << deviceId;
        Q_EMIT passwordTestFailed(deviceId, i18n("Device not found"));
        return false;
    }

    // Test and save password via device proxy
    bool success = deviceProxy->savePassword(password);

    if (success) {
        qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Password saved successfully";

        // Update device info
        DeviceInfo *device = findDevice(deviceId);
        if (device) {
            device->hasValidPassword = true;
            device->requiresPassword = true;

            // Notify QML of change
            int row = findDeviceIndex(deviceId);
            if (row >= 0) {
                QModelIndex idx = index(row);
                Q_EMIT dataChanged(idx, idx);
            }
        }
        return true;
    } else {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Invalid password or save failed";
        Q_EMIT passwordTestFailed(deviceId, i18n("Invalid password. Please try again."));
        return false;
    }
}

void YubiKeyDeviceModel::showPasswordDialog(const QString &deviceId,
                                             const QString &deviceName)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Showing password dialog for device:" << deviceId;

    // Validate device state first (same as authorizeDevice)
    DeviceInfo *device = findDevice(deviceId);
    if (!device) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device not found:" << deviceId;
        return;
    }

    if (!device->isConnected) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device not connected:" << deviceId;
        return;
    }

    if (!device->requiresPassword) {
        qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device does not require password:" << deviceId;
        return;
    }

    PasswordDialogHelper::showDialog(
        deviceId,
        deviceName,
        m_manager,
        this,
        [this]() {
            // Success - refresh device list from manager
            refreshDevices();
        }
    );
}

void YubiKeyDeviceModel::forgetDevice(const QString &deviceId)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Forgetting device:" << deviceId;

    int row = findDeviceIndex(deviceId);
    if (row < 0) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device not found:" << deviceId;
        return;
    }

    // Get device proxy
    YubiKeyDeviceProxy *deviceProxy = m_manager->getDevice(deviceId);
    if (!deviceProxy) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device proxy not found:" << deviceId;
        return;
    }

    // Forget device via device proxy (daemon will remove from database and delete password)
    deviceProxy->forget();

    // Remove device from model
    beginRemoveRows(QModelIndex(), row, row);
    m_devices.removeAt(row);
    endRemoveRows();

    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device forgotten successfully:" << deviceId;
}

bool YubiKeyDeviceModel::setDeviceName(const QString &deviceId, const QString &newName)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Setting device name:" << deviceId
                              << "to:" << newName;

    // Validate input
    QString trimmedName = newName.trimmed();
    if (deviceId.isEmpty() || trimmedName.isEmpty()) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Invalid device ID or name (empty after trim)";
        return false;
    }

    // Validate name length (max 64 chars)
    if (trimmedName.length() > 64) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Name too long (max 64 chars)";
        return false;
    }

    // Get device proxy
    YubiKeyDeviceProxy *deviceProxy = m_manager->getDevice(deviceId);
    if (!deviceProxy) {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device proxy not found:" << deviceId;
        return false;
    }

    // Update via device proxy
    deviceProxy->setName(trimmedName);

    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device name updated successfully via device proxy";

    // Update local model
    DeviceInfo *device = findDevice(deviceId);
    if (device) {
        device->deviceName = trimmedName;

        // Notify QML of change
        int row = findDeviceIndex(deviceId);
        if (row >= 0) {
            QModelIndex idx = index(row);
            Q_EMIT dataChanged(idx, idx, {DeviceNameRole});
            qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Model updated and QML notified";
        }
    } else {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device not found in local model after successful D-Bus update";
    }

    return true;
}

void YubiKeyDeviceModel::onDeviceConnected(YubiKeyDeviceProxy *device)
{
    if (device) {
        qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device connected:"
                                  << device->deviceId() << device->name();
    }

    // Refresh device list from manager
    refreshDevices();
}

void YubiKeyDeviceModel::onDeviceDisconnected(const QString &deviceId)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device disconnected:" << deviceId;

    // Refresh device list from manager
    refreshDevices();
}

void YubiKeyDeviceModel::onCredentialsUpdated()
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Credentials updated";

    // Refresh device list from manager
    refreshDevices();
}

DeviceInfo* YubiKeyDeviceModel::findDevice(const QString &deviceId)
{
    for (DeviceInfo &device : m_devices) {
        if (device.deviceId == deviceId) {
            return &device;
        }
    }
    return nullptr;
}

int YubiKeyDeviceModel::findDeviceIndex(const QString &deviceId) const
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).deviceId == deviceId) {
            return i;
        }
    }
    return -1;
}

} // namespace Config
} // namespace YubiKeyOath
