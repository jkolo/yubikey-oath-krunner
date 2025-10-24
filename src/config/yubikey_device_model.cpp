/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_device_model.h"
#include "dbus/yubikey_dbus_client.h"
#include "ui/password_dialog_helper.h"
#include "logging_categories.h"

#include <KLocalizedString>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

YubiKeyDeviceModel::YubiKeyDeviceModel(YubiKeyDBusClient *dbusClient, QObject *parent)
    : QAbstractListModel(parent)
    , m_dbusClient(dbusClient)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Initialized with D-Bus client";

    // Connect to D-Bus client signals for real-time updates
    connect(m_dbusClient, &YubiKeyDBusClient::deviceConnected,
            this, &YubiKeyDeviceModel::onDeviceConnected);
    connect(m_dbusClient, &YubiKeyDBusClient::deviceDisconnected,
            this, &YubiKeyDeviceModel::onDeviceDisconnected);
    connect(m_dbusClient, &YubiKeyDBusClient::credentialsUpdated,
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
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Refreshing device list from daemon";

    beginResetModel();

    // Get all devices from daemon via D-Bus
    m_devices = m_dbusClient->listDevices();

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

    // Test and save password via D-Bus
    bool success = m_dbusClient->savePassword(deviceId, password);

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
        m_dbusClient,
        this,
        [this]() {
            // Success - refresh device list from daemon
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

    // Forget device via D-Bus (daemon will remove from database and delete password)
    m_dbusClient->forgetDevice(deviceId);

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

    // Update via D-Bus
    bool success = m_dbusClient->setDeviceName(deviceId, trimmedName);

    if (success) {
        qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device name updated successfully via D-Bus";

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
    } else {
        qCWarning(YubiKeyConfigLog) << "YubiKeyDeviceModel: Failed to update device name via D-Bus";
    }

    return success;
}

void YubiKeyDeviceModel::onDeviceConnected(const QString &deviceId)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device connected:" << deviceId;

    // Refresh device list from daemon
    refreshDevices();
}

void YubiKeyDeviceModel::onDeviceDisconnected(const QString &deviceId)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Device disconnected:" << deviceId;

    // Refresh device list from daemon
    refreshDevices();
}

void YubiKeyDeviceModel::onCredentialsUpdated(const QString &deviceId)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Credentials updated for device:" << deviceId;

    // Refresh device list from daemon
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

} // namespace YubiKey
} // namespace KRunner
