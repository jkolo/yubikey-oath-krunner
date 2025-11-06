/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "daemon/storage/yubikey_database.h"
#include "types/oath_credential.h"
#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <optional>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of YubiKeyDatabase for testing
 *
 * In-memory implementation without actual SQL database
 */
class MockYubiKeyDatabase : public QObject
{
    Q_OBJECT

public:
    explicit MockYubiKeyDatabase(QObject *parent = nullptr)
        : QObject(parent)
        , m_initializeResult(true)
    {}

    ~MockYubiKeyDatabase() override = default;

    // ========== YubiKeyDatabase Interface ==========

    bool initialize()
    {
        return m_initializeResult;
    }

    bool addDevice(const QString &deviceId, const QString &name, bool requiresPassword)
    {
        if (m_devices.contains(deviceId)) {
            return false; // Device already exists
        }

        YubiKeyDatabase::DeviceRecord record;
        record.deviceId = deviceId;
        record.deviceName = name;
        record.requiresPassword = requiresPassword;
        record.lastSeen = QDateTime::currentDateTime();
        record.createdAt = QDateTime::currentDateTime();

        m_devices[deviceId] = record;
        return true;
    }

    bool updateDeviceName(const QString &deviceId, const QString &name)
    {
        if (!m_devices.contains(deviceId)) {
            return false;
        }

        m_devices[deviceId].deviceName = name;
        return true;
    }

    bool updateLastSeen(const QString &deviceId)
    {
        if (!m_devices.contains(deviceId)) {
            return false;
        }

        m_devices[deviceId].lastSeen = QDateTime::currentDateTime();
        return true;
    }

    bool removeDevice(const QString &deviceId)
    {
        if (!m_devices.contains(deviceId)) {
            return false;
        }

        m_devices.remove(deviceId);
        m_credentials.remove(deviceId);
        return true;
    }

    std::optional<YubiKeyDatabase::DeviceRecord> getDevice(const QString &deviceId)
    {
        if (m_devices.contains(deviceId)) {
            return m_devices[deviceId];
        }
        return std::nullopt;
    }

    QList<YubiKeyDatabase::DeviceRecord> getAllDevices()
    {
        return m_devices.values();
    }

    bool setRequiresPassword(const QString &deviceId, bool requiresPassword)
    {
        if (!m_devices.contains(deviceId)) {
            return false;
        }

        m_devices[deviceId].requiresPassword = requiresPassword;
        return true;
    }

    bool requiresPassword(const QString &deviceId)
    {
        if (!m_devices.contains(deviceId)) {
            return false;
        }

        return m_devices[deviceId].requiresPassword;
    }

    bool hasDevice(const QString &deviceId)
    {
        return m_devices.contains(deviceId);
    }

    bool saveCredentials(const QString &deviceId, const QList<Shared::OathCredential> &credentials)
    {
        m_credentials[deviceId] = credentials;
        return true;
    }

    QList<Shared::OathCredential> getCredentials(const QString &deviceId)
    {
        if (m_credentials.contains(deviceId)) {
            return m_credentials[deviceId];
        }
        return QList<Shared::OathCredential>();
    }

    bool clearAllCredentials()
    {
        m_credentials.clear();
        return true;
    }

    bool clearDeviceCredentials(const QString &deviceId)
    {
        m_credentials.remove(deviceId);
        return true;
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Sets return value for initialize()
     */
    void setInitializeResult(bool result)
    {
        m_initializeResult = result;
    }

    /**
     * @brief Clears all stored data
     */
    void reset()
    {
        m_devices.clear();
        m_credentials.clear();
        m_initializeResult = true;
    }

    /**
     * @brief Gets number of stored devices
     */
    int deviceCount() const
    {
        return m_devices.size();
    }

    /**
     * @brief Gets number of credentials for device
     */
    int credentialCount(const QString &deviceId) const
    {
        if (m_credentials.contains(deviceId)) {
            return m_credentials[deviceId].size();
        }
        return 0;
    }

private:
    bool m_initializeResult;
    QMap<QString, YubiKeyDatabase::DeviceRecord> m_devices;
    QMap<QString, QList<Shared::OathCredential>> m_credentials;
};

} // namespace Daemon
} // namespace YubiKeyOath
