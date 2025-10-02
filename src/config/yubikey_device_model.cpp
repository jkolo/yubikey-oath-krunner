/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_device_model.h"
#include "../shared/dbus/yubikey_dbus_client.h"
#include "logging_categories.h"

#include <KLocalizedString>
#include <QDebug>
#include <QProcess>

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

    // Show password dialog immediately (recursive until success or cancel)
    showPasswordDialogForDevice(deviceId, QString());
}

void YubiKeyDeviceModel::showPasswordDialogForDevice(const QString &deviceId, const QString &errorMessage)
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: showPasswordDialogForDevice() for device:" << deviceId;

    // Create QProcess with parent to ensure cleanup
    QProcess *process = new QProcess(this);

    // Connect to finished signal to get the password
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, deviceId, errorMessage](int exitCode, QProcess::ExitStatus exitStatus) {
        Q_UNUSED(exitStatus)

        if (exitCode == 0) {
            // User entered password
            QString password = QString::fromUtf8(process->readAllStandardOutput()).trimmed();

            if (!password.isEmpty()) {
                // SavePassword will test and save the password
                qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Saving password for device:" << deviceId;

                if (m_dbusClient->savePassword(deviceId, password)) {
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
                } else {
                    // Invalid password or save failed - show dialog again with error message
                    qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Invalid password or save failed - showing dialog again";
                    showPasswordDialogForDevice(deviceId, i18n("Invalid password. Please try again."));
                }
            } else {
                qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Empty password entered";
            }
        } else {
            qCDebug(YubiKeyConfigLog) << "YubiKeyDeviceModel: Password dialog cancelled";
        }

        process->deleteLater();
    });

    // Find device name for display
    QString deviceName = deviceId;
    DeviceInfo *device = findDevice(deviceId);
    if (device) {
        deviceName = device->deviceName;
    }

    // Build dialog message
    QString dialogMessage;
    if (!errorMessage.isEmpty()) {
        dialogMessage = errorMessage + QStringLiteral("\n\n") +
                       i18n("Device: %1\n\nPlease enter the OATH password:", deviceName);
    } else {
        dialogMessage = i18n("Device: %1\n\nThis YubiKey requires a password.\nPlease enter the OATH password:", deviceName);
    }

    // Start kdialog process
    process->start(QStringLiteral("kdialog"), QStringList{
        QStringLiteral("--password"),
        dialogMessage,
        QStringLiteral("--title"),
        i18n("YubiKey OATH Password")
    });
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
