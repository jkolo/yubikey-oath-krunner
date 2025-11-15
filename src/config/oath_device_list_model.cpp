/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_device_list_model.h"
#include "dbus/oath_manager_proxy.h"
#include "dbus/oath_device_proxy.h"
#include "dbus/oath_device_session_proxy.h"
#include "ui/password_dialog_helper.h"
#include "ui/change_password_dialog_helper.h"
#include "logging_categories.h"

#include <KLocalizedString>
#include <QDebug>

namespace YubiKeyOath {
namespace Config {
using namespace YubiKeyOath::Shared;

OathDeviceListModel::OathDeviceListModel(OathManagerProxy *manager, QObject *parent)
    : QAbstractListModel(parent)
    , m_manager(manager)
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Initialized with manager proxy";

    // Connect to manager proxy signals for real-time updates
    connect(m_manager, &OathManagerProxy::deviceConnected,
            this, &OathDeviceListModel::onDeviceConnected);
    connect(m_manager, &OathManagerProxy::deviceDisconnected,
            this, &OathDeviceListModel::onDeviceDisconnected);
    connect(m_manager, &OathManagerProxy::credentialsChanged,
            this, &OathDeviceListModel::onCredentialsUpdated);
    connect(m_manager, &OathManagerProxy::devicePropertyChanged,
            this, &OathDeviceListModel::onDevicePropertyChanged);

    // Initial refresh
    refreshDevices();
}

int OathDeviceListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_devices.size());
}

QVariant OathDeviceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size()) {
        return {};
    }

    const DeviceInfo &device = m_devices.at(index.row());

    switch (role) {
    case DeviceIdRole:
        return device._internalDeviceId;
    case DeviceNameRole:
        return device.deviceName;
    case IsConnectedRole:
        return device.isConnected();
    case RequiresPasswordRole:
        return device.requiresPassword;
    case HasValidPasswordRole:
        return device.hasValidPassword;
    case ShowAuthorizeButtonRole:
        // Show "Authorize" button if device is connected, requires password, and we don't have valid password
        return device.isConnected() && device.requiresPassword && !device.hasValidPassword;
    case DeviceModelRole:
        qCDebug(OathConfigLog) << "DeviceModel role requested for device:" << device.deviceName
                                  << "returning deviceModelCode:" << device.deviceModelCode
                                  << "(hex: 0x" << Qt::hex << device.deviceModelCode << Qt::dec << ")";
        return device.deviceModelCode;
    case DeviceModelStringRole:
        return device.deviceModel;
    case SerialNumberRole:
        return device.serialNumber;
    case FormFactorRole:
        return device.formFactor;
    case CapabilitiesRole:
        return device.capabilities;
    case LastSeenRole:
        return device.lastSeen;
    default:
        return {};
    }
}

QHash<int, QByteArray> OathDeviceListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[DeviceIdRole] = "deviceId";
    roles[DeviceNameRole] = "deviceName";
    roles[IsConnectedRole] = "isConnected";
    roles[RequiresPasswordRole] = "requiresPassword";
    roles[HasValidPasswordRole] = "hasValidPassword";
    roles[ShowAuthorizeButtonRole] = "showAuthorizeButton";
    roles[DeviceModelRole] = "deviceModel";
    roles[DeviceModelStringRole] = "deviceModelString";
    roles[SerialNumberRole] = "serialNumber";
    roles[FormFactorRole] = "formFactor";
    roles[CapabilitiesRole] = "capabilities";
    roles[LastSeenRole] = "lastSeen";
    return roles;
}

Qt::ItemFlags OathDeviceListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    // Items are selectable, enabled, and editable (for inline name editing)
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
}

void OathDeviceListModel::refreshDevices()
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Refreshing device list from manager proxy";

    beginResetModel();

    // Get all devices from manager proxy and convert to DeviceInfo
    m_devices.clear();
    const QList<OathDeviceProxy*> deviceProxies = m_manager->devices();
    for (const auto *deviceProxy : deviceProxies) {
        // Get session proxy for this device to populate session properties
        const auto *session = m_manager->getDeviceSession(deviceProxy->deviceId());
        m_devices.append(deviceProxy->toDeviceInfo(session));
    }

    endResetModel();

    qCDebug(OathConfigLog) << "OathDeviceListModel: Refresh complete, total devices:" << m_devices.size();

    for (const auto &device : m_devices) {
        qCDebug(OathConfigLog) << "OathDeviceListModel: Device" << device.deviceName
                 << "connected:" << device.isConnected()
                 << "requiresPassword:" << device.requiresPassword
                 << "hasValidPassword:" << device.hasValidPassword;
    }
}

void OathDeviceListModel::authorizeDevice(const QString &deviceId)
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Authorization requested for device:" << deviceId;

    const DeviceInfo * const device = findDevice(deviceId);
    if (!device) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device not found:" << deviceId;
        return;
    }

    if (!device->isConnected()) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device not connected:" << deviceId;
        return;
    }

    if (!device->requiresPassword) {
        qCDebug(OathConfigLog) << "OathDeviceListModel: Device does not require password:" << deviceId;
        return;
    }

    // PasswordDialog will be opened from QML via authorizeDevice() signal/method
    qCDebug(OathConfigLog) << "OathDeviceListModel: Device ready for authorization";
}

bool OathDeviceListModel::testAndSavePassword(const QString &deviceId, const QString &password)
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Testing password for device:" << deviceId;

    if (password.isEmpty()) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Empty password provided";
        Q_EMIT passwordTestFailed(deviceId, i18n("Password cannot be empty"));
        return false;
    }

    // Get device proxy
    const OathDeviceProxy * const deviceProxy = m_manager->getDevice(deviceId);
    if (!deviceProxy) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device not found:" << deviceId;
        Q_EMIT passwordTestFailed(deviceId, i18n("Device not found"));
        return false;
    }

    // Get session proxy for password operations
    OathDeviceSessionProxy * const sessionProxy = m_manager->getDeviceSession(deviceId);
    if (!sessionProxy) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Session proxy not found:" << deviceId;
        Q_EMIT passwordTestFailed(deviceId, i18n("Device session not found"));
        return false;
    }

    // Test and save password via session proxy
    const bool success = sessionProxy->savePassword(password);

    if (success) {
        qCDebug(OathConfigLog) << "OathDeviceListModel: Password saved successfully";

        // Update device info
        DeviceInfo * const device = findDevice(deviceId);
        if (device) {
            device->hasValidPassword = true;
            device->requiresPassword = true;

            // Notify QML of change
            const int row = findDeviceIndex(deviceId);
            if (row >= 0) {
                const QModelIndex idx = index(row);
                Q_EMIT dataChanged(idx, idx);
            }
        }
        return true;
    } else {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Invalid password or save failed";
        Q_EMIT passwordTestFailed(deviceId, i18n("Invalid password. Please try again."));
        return false;
    }
}

void OathDeviceListModel::showPasswordDialog(const QString &deviceId,
                                             const QString &deviceName)
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Showing password dialog for device:" << deviceId;

    // Validate device state first (same as authorizeDevice)
    const DeviceInfo * const device = findDevice(deviceId);
    if (!device) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device not found:" << deviceId;
        return;
    }

    if (!device->isConnected()) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device not connected:" << deviceId;
        return;
    }

    if (!device->requiresPassword) {
        qCDebug(OathConfigLog) << "OathDeviceListModel: Device does not require password:" << deviceId;
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

void OathDeviceListModel::showChangePasswordDialog(const QString &deviceId,
                                                   const QString &deviceName)
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Showing change password dialog for device:" << deviceId;

    // Validate device state first
    const DeviceInfo * const device = findDevice(deviceId);
    if (!device) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device not found:" << deviceId;
        return;
    }

    if (!device->isConnected()) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device not connected:" << deviceId;
        return;
    }

    ChangePasswordDialogHelper::showDialog(
        deviceId,
        deviceName,
        device->requiresPassword,
        m_manager,
        this,
        [this]() {
            // Success - refresh device list from manager
            refreshDevices();
        }
    );
}

void OathDeviceListModel::forgetDevice(const QString &deviceId)
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Forgetting device:" << deviceId;

    const int row = findDeviceIndex(deviceId);
    if (row < 0) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device not found:" << deviceId;
        return;
    }

    // Get device proxy
    OathDeviceProxy *deviceProxy = m_manager->getDevice(deviceId);
    if (!deviceProxy) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device proxy not found:" << deviceId;
        return;
    }

    // Forget device via device proxy (daemon will remove from database and delete password)
    deviceProxy->forget();

    // Remove device from model
    beginRemoveRows(QModelIndex(), row, row);
    m_devices.removeAt(row);
    endRemoveRows();

    qCDebug(OathConfigLog) << "OathDeviceListModel: Device forgotten successfully:" << deviceId;
}

bool OathDeviceListModel::setDeviceName(const QString &deviceId, const QString &newName)
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Setting device name:" << deviceId
                              << "to:" << newName;

    // Validate input
    const QString trimmedName = newName.trimmed();
    if (deviceId.isEmpty() || trimmedName.isEmpty()) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Invalid device ID or name (empty after trim)";
        return false;
    }

    // Validate name length (max 64 chars)
    if (trimmedName.length() > 64) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Name too long (max 64 chars)";
        return false;
    }

    // Get device proxy
    OathDeviceProxy *deviceProxy = m_manager->getDevice(deviceId);
    if (!deviceProxy) {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device proxy not found:" << deviceId;
        return false;
    }

    // Update via device proxy
    deviceProxy->setName(trimmedName);

    qCDebug(OathConfigLog) << "OathDeviceListModel: Device name updated successfully via device proxy";

    // Update local model
    DeviceInfo * const device = findDevice(deviceId);
    if (device) {
        device->deviceName = trimmedName;

        // Notify QML of change
        const int row = findDeviceIndex(deviceId);
        if (row >= 0) {
            const QModelIndex idx = index(row);
            Q_EMIT dataChanged(idx, idx, {DeviceNameRole});
            qCDebug(OathConfigLog) << "OathDeviceListModel: Model updated and QML notified";
        }
    } else {
        qCWarning(OathConfigLog) << "OathDeviceListModel: Device not found in local model after successful D-Bus update";
    }

    return true;
}

void OathDeviceListModel::onDeviceConnected(OathDeviceProxy *device)
{
    if (device) {
        qCDebug(OathConfigLog) << "OathDeviceListModel: Device connected:"
                                  << device->serialNumber() << device->name();
    }

    // Refresh device list from manager
    refreshDevices();
}

void OathDeviceListModel::onDeviceDisconnected(const QString &deviceId)
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Device disconnected:" << deviceId;

    // Refresh device list from manager
    refreshDevices();
}

void OathDeviceListModel::onCredentialsUpdated()
{
    qCDebug(OathConfigLog) << "OathDeviceListModel: Credentials updated";

    // Refresh device list from manager
    refreshDevices();
}

void OathDeviceListModel::onDevicePropertyChanged(OathDeviceProxy *device)
{
    if (!device) {
        return;
    }

    const QString deviceId = device->deviceId();

    // Get session proxy for connection state
    const auto *session = m_manager->getDeviceSession(deviceId);

    qCDebug(OathConfigLog) << "OathDeviceListModel: Device property changed:" << deviceId
                              << "Name:" << device->name()
                              << "IsConnected:" << (session ? session->isConnected() : false);

    // Find device in model
    const int row = findDeviceIndex(deviceId);
    if (row < 0) {
        qCDebug(OathConfigLog) << "OathDeviceListModel: Device not found in model, skipping update";
        return;
    }

    // Update device info from proxy (efficient single-row update)
    m_devices[row] = device->toDeviceInfo(session);

    // Notify view of changes for this row only (all roles may have changed)
    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx);

    qCDebug(OathConfigLog) << "OathDeviceListModel: Updated row" << row << "for device" << deviceId;
}

DeviceInfo* OathDeviceListModel::findDevice(const QString &deviceId)
{
    for (DeviceInfo &device : m_devices) {
        if (device._internalDeviceId == deviceId) {
            return &device;
        }
    }
    return nullptr;
}

const DeviceInfo* OathDeviceListModel::findDevice(const QString &deviceId) const
{
    for (const DeviceInfo &device : m_devices) {
        if (device._internalDeviceId == deviceId) {
            return &device;
        }
    }
    return nullptr;
}

int OathDeviceListModel::findDeviceIndex(const QString &deviceId) const
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i)._internalDeviceId == deviceId) {
            return i;
        }
    }
    return -1;
}

} // namespace Config
} // namespace YubiKeyOath
