#include "yubikey_device_manager.h"
#include "oath_session.h"
#include "oath_protocol.h"
#include "../logging_categories.h"

#include <QDebug>
#include <QDateTime>
#include <QSet>
#include <QMutexLocker>

extern "C" {
#include <winscard.h>
#include <string.h>
}

// PC/SC error codes not always defined in winscard.h
#ifndef SCARD_E_NO_READERS_AVAILABLE
#define SCARD_E_NO_READERS_AVAILABLE ((LONG)0x8010002E)
#endif
#ifndef SCARD_E_NO_SERVICE
#define SCARD_E_NO_SERVICE ((LONG)0x8010001D)
#endif
#ifndef SCARD_W_REMOVED_CARD
#define SCARD_W_REMOVED_CARD ((LONG)0x80100069)
#endif
#ifndef SCARD_E_NO_SMARTCARD
#define SCARD_E_NO_SMARTCARD ((LONG)0x8010000C)
#endif
#ifndef SCARD_W_RESET_CARD
#define SCARD_W_RESET_CARD ((LONG)0x80100068)
#endif

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

YubiKeyDeviceManager::YubiKeyDeviceManager(QObject* parent)
    : QObject(parent)
    , m_readerMonitor(new CardReaderMonitor(this))
{
    qCDebug(YubiKeyDeviceManagerLog) << "Constructor called";

    // Connect card reader monitor signals
    connect(m_readerMonitor, &CardReaderMonitor::readerListChanged,
            this, &YubiKeyDeviceManager::onReaderListChanged);
    connect(m_readerMonitor, &CardReaderMonitor::cardInserted,
            this, &YubiKeyDeviceManager::onCardInserted);
    connect(m_readerMonitor, &CardReaderMonitor::cardRemoved,
            this, &YubiKeyDeviceManager::onCardRemoved);

    // Connect async credential cache fetching
    connect(this, &YubiKeyDeviceManager::credentialCacheFetchedForDevice,
            this, &YubiKeyDeviceManager::onCredentialCacheFetchedForDevice);
}

YubiKeyDeviceManager::~YubiKeyDeviceManager() {
    cleanup();
}

Result<void> YubiKeyDeviceManager::initialize() {
    qCDebug(YubiKeyDeviceManagerLog) << "initialize() called";
    if (m_initialized) {
        qCDebug(YubiKeyDeviceManagerLog) << "Already initialized";
        return Result<void>::success();
    }

    LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &m_context);
    if (result != SCARD_S_SUCCESS) {
        qCDebug(YubiKeyDeviceManagerLog) << "Failed to establish PC/SC context:" << result;
        QString error = tr("Failed to establish PC/SC context: %1").arg(result);
        Q_EMIT errorOccurred(error);
        return Result<void>::error(error);
    }

    qCDebug(YubiKeyDeviceManagerLog) << "PC/SC context established successfully";
    m_initialized = true;

    // Start card reader monitor
    qCDebug(YubiKeyDeviceManagerLog) << "Starting card reader monitor";
    m_readerMonitor->startMonitoring(m_context);

    // NEW: Check for existing readers at startup (CardReaderMonitor only detects changes)
    qCDebug(YubiKeyDeviceManagerLog) << "Checking for existing YubiKey readers";
    DWORD readersLen = 0;
    result = SCardListReaders(m_context, nullptr, nullptr, &readersLen);

    if (result == SCARD_S_SUCCESS && readersLen > 0) {
        QByteArray readersBuffer(readersLen, '\0');
        result = SCardListReaders(m_context, nullptr, readersBuffer.data(), &readersLen);

        if (result == SCARD_S_SUCCESS) {
            // Parse reader names (null-terminated strings)
            const char* readerName = readersBuffer.constData();
            while (*readerName) {
                QString reader = QString::fromUtf8(readerName);
                qCDebug(YubiKeyDeviceManagerLog) << "Found reader:" << reader;

                // Check if it's a YubiKey reader
                if (reader.contains(QStringLiteral("Yubico"), Qt::CaseInsensitive) ||
                    reader.contains(QStringLiteral("YubiKey"), Qt::CaseInsensitive)) {
                    qCDebug(YubiKeyDeviceManagerLog) << "Detected YubiKey reader - connecting:" << reader;

                    // Try to connect to this device
                    QString deviceId = connectToDevice(reader);
                    if (!deviceId.isEmpty()) {
                        qCDebug(YubiKeyDeviceManagerLog) << "Successfully connected to existing device" << deviceId;
                        // Credential fetching will be triggered by onDeviceConnectedInternal in YubiKeyDBusService
                    }
                }

                // Move to next reader name
                readerName += strlen(readerName) + 1;
            }
        } else {
            qCDebug(YubiKeyDeviceManagerLog) << "SCardListReaders failed:" << QString::number(result, 16);
        }
    } else if (result == SCARD_E_NO_READERS_AVAILABLE) {
        qCDebug(YubiKeyDeviceManagerLog) << "No readers available at startup";
    } else {
        qCDebug(YubiKeyDeviceManagerLog) << "SCardListReaders failed:" << QString::number(result, 16);
    }

    // Future device connections are handled by CardReaderMonitor via onCardInserted signal
    return Result<void>::success();
}

void YubiKeyDeviceManager::cleanup() {
    qCDebug(YubiKeyDeviceManagerLog) << "cleanup() - stopping card reader monitor";
    m_readerMonitor->stopMonitoring();

    // Disconnect all devices
    QStringList deviceIds;
    {
        QMutexLocker locker(&m_devicesMutex);
        for (const auto &devicePair : m_devices) {
            deviceIds.append(devicePair.first);
        }
    }

    for (const QString &deviceId : deviceIds) {
        disconnectDevice(deviceId);  // disconnectDevice has its own lock
    }

    if (m_initialized) {
        SCardReleaseContext(m_context);
        m_context = 0;
        m_initialized = false;
    }
}

bool YubiKeyDeviceManager::hasConnectedDevices() const {
    QMutexLocker locker(&m_devicesMutex);
    // Return true if ANY device is connected
    bool anyConnected = !m_devices.empty();
    qCDebug(YubiKeyDeviceManagerLog) << "hasConnectedDevices() - connected devices:" << m_devices.size()
             << "returning:" << anyConnected;
    return anyConnected;
}

QString YubiKeyDeviceManager::connectToDevice(const QString &readerName) {
    qCDebug(YubiKeyDeviceManagerLog) << "connectToDevice() called for reader:" << readerName;

    if (!m_initialized) {
        qCDebug(YubiKeyDeviceManagerLog) << "Not initialized, cannot connect";
        return QString();
    }

    // Connect to card
    SCARDHANDLE cardHandle = 0;
    DWORD protocol = 0;

    QByteArray readerBytes = readerName.toUtf8();
    LONG result = SCardConnect(m_context, readerBytes.constData(),
                              SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
                              &cardHandle, &protocol);

    qCDebug(YubiKeyDeviceManagerLog) << "SCardConnect result:" << result << "protocol:" << protocol;

    if (result != SCARD_S_SUCCESS) {
        qCDebug(YubiKeyDeviceManagerLog) << "Failed to connect to reader" << readerName << ", error code:" << result;
        Q_EMIT errorOccurred(tr("Failed to connect to YubiKey reader %1: %2").arg(readerName).arg(result));
        return QString();
    }

    qCDebug(YubiKeyDeviceManagerLog) << "Successfully connected to PC/SC reader, cardHandle:" << cardHandle;

    // Select OATH application to get device ID using OathSession
    QByteArray challenge;
    QString deviceId;

    {
        // Create temporary OathSession for initial SELECT
        OathSession tempSession(cardHandle, protocol, QString(), this);
        auto selectResult = tempSession.selectOathApplication(challenge);

        if (selectResult.isError()) {
            qCDebug(YubiKeyDeviceManagerLog) << "Failed to select OATH application:" << selectResult.error();
            SCardDisconnect(cardHandle, SCARD_LEAVE_CARD);
            Q_EMIT errorOccurred(tr("Failed to select OATH application: %1").arg(selectResult.error()));
            return QString();
        }

        // Get device ID from session
        deviceId = tempSession.deviceId();
    }

    if (deviceId.isEmpty()) {
        qCDebug(YubiKeyDeviceManagerLog) << "No device ID from SELECT, disconnecting";
        SCardDisconnect(cardHandle, SCARD_LEAVE_CARD);
        return QString();
    }

    qCDebug(YubiKeyDeviceManagerLog) << "Got device ID:" << deviceId << "from SELECT response";

    // Check if this device is already connected (without lock to avoid deadlock with disconnectDevice)
    bool needsDisconnect = false;
    {
        QMutexLocker locker(&m_devicesMutex);
        needsDisconnect = m_devices.count(deviceId);
    }

    if (needsDisconnect) {
        qCDebug(YubiKeyDeviceManagerLog) << "Device" << deviceId << "is already connected, disconnecting old connection";
        disconnectDevice(deviceId);  // disconnectDevice has its own lock
    }

    // Create YubiKeyOathDevice instance
    auto devicePtr = std::make_unique<YubiKeyOathDevice>(
        deviceId,
        readerName,
        cardHandle,
        protocol,
        challenge,
        m_context,
        this  // parent
    );

    // Get raw pointer for signal connections (before moving ownership to map)
    YubiKeyOathDevice* device = devicePtr.get();

    // Connect device signals - forward for multi-device aggregation
    connect(device, &YubiKeyOathDevice::touchRequired,
            this, &YubiKeyDeviceManager::touchRequired);
    connect(device, &YubiKeyOathDevice::errorOccurred,
            this, &YubiKeyDeviceManager::errorOccurred);
    connect(device, &YubiKeyOathDevice::credentialsChanged,
            this, &YubiKeyDeviceManager::credentialsChanged);
    connect(device, &YubiKeyOathDevice::credentialCacheFetched,
            this, [this, deviceId](const QList<OathCredential> &credentials) {
                Q_EMIT credentialCacheFetchedForDevice(deviceId, credentials);
            });

    // Critical section: add to device map
    {
        QMutexLocker locker(&m_devicesMutex);
        m_devices[deviceId] = std::move(devicePtr);  // Move ownership to map
        qCDebug(YubiKeyDeviceManagerLog) << "Added device" << deviceId << "to map, total devices:" << m_devices.size();
    }

    // Emit device connected signal
    Q_EMIT deviceConnected(deviceId);
    qCDebug(YubiKeyDeviceManagerLog) << "Emitted deviceConnected signal for" << deviceId;

    return deviceId;
}

void YubiKeyDeviceManager::disconnectDevice(const QString &deviceId) {
    qCDebug(YubiKeyDeviceManagerLog) << "disconnectDevice() called for device:" << deviceId;

    // Critical section: check and remove from map
    {
        QMutexLocker locker(&m_devicesMutex);

        if (!m_devices.count(deviceId)) {
            qCDebug(YubiKeyDeviceManagerLog) << "Device" << deviceId << "not found in cache";
            return;
        }

        // Device destructor will handle PC/SC disconnection
        qCDebug(YubiKeyDeviceManagerLog) << "Deleting YubiKeyOathDevice instance for" << deviceId;

        // Remove from map - unique_ptr destructor automatically deletes device
        m_devices.erase(deviceId);

        qCDebug(YubiKeyDeviceManagerLog) << "Removed device" << deviceId << "from map, remaining devices:" << m_devices.size();
    }
    // Lock released here, device already deleted by unique_ptr

    // Emit device disconnected signal
    Q_EMIT deviceDisconnected(deviceId);
    qCDebug(YubiKeyDeviceManagerLog) << "Emitted deviceDisconnected signal for" << deviceId;

    // Emit credentials changed if we had any credentials for this device
    Q_EMIT credentialsChanged();
}

QList<OathCredential> YubiKeyDeviceManager::getCredentials() {
    qCDebug(YubiKeyDeviceManagerLog) << "getCredentials() called";

    // NEW: Multi-device aggregation
    // Aggregate credentials from all connected devices
    QList<OathCredential> aggregatedCredentials;

    // Copy device list under lock to avoid holding lock during credential fetching
    QList<YubiKeyOathDevice*> devices;
    {
        QMutexLocker locker(&m_devicesMutex);
        qCDebug(YubiKeyDeviceManagerLog) << "Aggregating credentials from" << m_devices.size() << "devices";
        devices.reserve(m_devices.size());
        for (const auto &devicePair : std::as_const(m_devices)) {
            devices.append(devicePair.second.get());
        }
    }

    for (YubiKeyOathDevice* device : devices) {
        QString deviceId = device->deviceId();

        qCDebug(YubiKeyDeviceManagerLog) << "Processing device" << deviceId
                 << "- has" << device->credentials().size() << "credentials"
                 << "- updateInProgress:" << device->isUpdateInProgress();

        // Skip devices that are currently updating
        if (device->isUpdateInProgress()) {
            qCDebug(YubiKeyDeviceManagerLog) << "Skipping device" << deviceId << "- update in progress";
            continue;
        }

        // Add credentials from this device to aggregated list
        for (const auto &credential : device->credentials()) {
            aggregatedCredentials.append(credential);
            qCDebug(YubiKeyDeviceManagerLog) << "Added credential from device" << deviceId << ":" << credential.name;
        }
    }

    qCDebug(YubiKeyDeviceManagerLog) << "Returning" << aggregatedCredentials.size() << "aggregated credentials from all devices";

    return aggregatedCredentials;
}

void YubiKeyDeviceManager::onReaderListChanged()
{
    qCDebug(YubiKeyDeviceManagerLog) << "onReaderListChanged() - reader list changed";

    // Get current list of PC/SC readers
    DWORD readersLen = 0;
    LONG result = SCardListReaders(m_context, nullptr, nullptr, &readersLen);

    QSet<QString> currentReaders;

    if (result == SCARD_S_SUCCESS && readersLen > 0) {
        QByteArray readersBuffer(readersLen, '\0');
        result = SCardListReaders(m_context, nullptr, readersBuffer.data(), &readersLen);

        if (result == SCARD_S_SUCCESS) {
            // Parse reader names into a set for fast lookup
            const char* readerName = readersBuffer.constData();
            while (*readerName) {
                currentReaders.insert(QString::fromUtf8(readerName));
                readerName += strlen(readerName) + 1;
            }
            qCDebug(YubiKeyDeviceManagerLog) << "Current readers:" << currentReaders;
        }
    } else if (result == SCARD_E_NO_READERS_AVAILABLE) {
        qCDebug(YubiKeyDeviceManagerLog) << "No readers available";
    } else if (result != SCARD_S_SUCCESS) {
        qCDebug(YubiKeyDeviceManagerLog) << "SCardListReaders failed:" << QString::number(result, 16);
    }

    // Check each connected device - disconnect if its reader no longer exists
    QStringList devicesToDisconnect;
    {
        QMutexLocker locker(&m_devicesMutex);
        for (const auto &devicePair : m_devices) {
            QString deviceReaderName = devicePair.second->readerName();
            if (!currentReaders.contains(deviceReaderName)) {
                qCDebug(YubiKeyDeviceManagerLog) << "Device" << devicePair.first
                         << "reader" << deviceReaderName << "no longer exists - will disconnect";
                devicesToDisconnect.append(devicePair.first);
            }
        }
    }

    // Disconnect devices outside the lock to avoid deadlock
    for (const QString &deviceId : devicesToDisconnect) {
        qCDebug(YubiKeyDeviceManagerLog) << "Disconnecting device" << deviceId << "- reader removed";
        disconnectDevice(deviceId);
        // disconnectDevice() will automatically emit:
        // - deviceDisconnected(deviceId)
        // - credentialsChanged()
    }

    // Check for new YubiKey readers and connect to them
    // Get set of reader names from currently connected devices
    QSet<QString> connectedReaderNames;
    {
        QMutexLocker locker(&m_devicesMutex);
        for (const auto &devicePair : m_devices) {
            connectedReaderNames.insert(devicePair.second->readerName());
        }
    }

    // Find new readers (present in currentReaders but not in connectedReaderNames)
    for (const QString &readerName : currentReaders) {
        if (!connectedReaderNames.contains(readerName)) {
            // New reader detected - check if it's a YubiKey
            if (readerName.contains(QStringLiteral("Yubico"), Qt::CaseInsensitive) ||
                readerName.contains(QStringLiteral("YubiKey"), Qt::CaseInsensitive)) {
                qCDebug(YubiKeyDeviceManagerLog) << "Detected new YubiKey reader - connecting:" << readerName;

                // Try to connect to this device
                QString deviceId = connectToDevice(readerName);
                if (!deviceId.isEmpty()) {
                    qCDebug(YubiKeyDeviceManagerLog) << "Successfully connected to device" << deviceId << "on new reader" << readerName;
                    // Credential fetching will be triggered by onDeviceConnectedInternal in YubiKeyDBusService
                }
            }
        }
    }
}

void YubiKeyDeviceManager::onCardInserted(const QString &readerName)
{
    qCDebug(YubiKeyDeviceManagerLog) << "onCardInserted() - reader:" << readerName;

    // NEW: Multi-device support - connect to specific device
    QString deviceId = connectToDevice(readerName);

    if (!deviceId.isEmpty()) {
        qCDebug(YubiKeyDeviceManagerLog) << "Successfully connected to device" << deviceId << "on reader" << readerName;
        // Credential fetching will be triggered by onDeviceConnectedInternal in YubiKeyDBusService
    } else {
        qCDebug(YubiKeyDeviceManagerLog) << "Failed to connect to device on reader" << readerName;
    }

}

void YubiKeyDeviceManager::onCardRemoved(const QString &readerName)
{
    qCDebug(YubiKeyDeviceManagerLog) << "onCardRemoved() - reader:" << readerName;

    // NEW: Multi-device support - find and disconnect specific device by reader name
    QString deviceIdToRemove;
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it->second->readerName() == readerName) {
            deviceIdToRemove = it->first;
            break;
        }
    }

    if (!deviceIdToRemove.isEmpty()) {
        qCDebug(YubiKeyDeviceManagerLog) << "Found device" << deviceIdToRemove << "on reader" << readerName << "- disconnecting";
        disconnectDevice(deviceIdToRemove);
        // credentialsChanged() signal is emitted automatically by disconnectDevice()
    } else {
        qCDebug(YubiKeyDeviceManagerLog) << "No device found for reader" << readerName;
    }

}

QStringList YubiKeyDeviceManager::getConnectedDeviceIds() const {
    QMutexLocker locker(&m_devicesMutex);
    QStringList deviceIds;
    for (const auto &devicePair : m_devices) {
        deviceIds.append(devicePair.first);
    }
    return deviceIds;
}

void YubiKeyDeviceManager::onCredentialCacheFetchedForDevice(const QString &deviceId, const QList<OathCredential> &credentials) {
    qCDebug(YubiKeyDeviceManagerLog) << "onCredentialCacheFetchedForDevice() called for device" << deviceId
             << "with" << credentials.size() << "credentials";

    // Device has already updated its internal credential cache
    // Just emit the manager-level signal for any listeners
    Q_EMIT credentialsChanged();
}

YubiKeyOathDevice* YubiKeyDeviceManager::getDevice(const QString &deviceId)
{
    QMutexLocker locker(&m_devicesMutex);
    auto it = m_devices.find(deviceId);
    if (it != m_devices.end()) {
        return it->second.get();  // Return raw pointer from unique_ptr
    }
    return nullptr;
}

YubiKeyOathDevice* YubiKeyDeviceManager::getDeviceOrFirst(const QString &deviceId)
{
    if (!deviceId.isEmpty()) {
        // Specific device requested
        return getDevice(deviceId);
    }

    // Get first available device
    QStringList connectedIds = getConnectedDeviceIds();
    if (connectedIds.isEmpty()) {
        return nullptr;
    }

    return getDevice(connectedIds.first());
}

void YubiKeyDeviceManager::removeDeviceFromMemory(const QString &deviceId)
{
    qCDebug(YubiKeyDeviceManagerLog) << "removeDeviceFromMemory() called for device:" << deviceId;

    int remainingDevices = 0;

    // Critical section: check and remove from map
    {
        QMutexLocker locker(&m_devicesMutex);

        if (!m_devices.count(deviceId)) {
            qCDebug(YubiKeyDeviceManagerLog) << "Device" << deviceId << "not found in cache - nothing to remove";
            return;
        }

        // Device destructor will handle PC/SC disconnection
        qCDebug(YubiKeyDeviceManagerLog) << "Removing YubiKeyOathDevice instance for" << deviceId << "from memory";

        // Remove from map - unique_ptr destructor automatically deletes device
        m_devices.erase(deviceId);
        remainingDevices = m_devices.size();

        qCDebug(YubiKeyDeviceManagerLog) << "Removed device" << deviceId << "from memory, remaining devices:" << remainingDevices;
    }
    // Lock released here, device already deleted by unique_ptr

    qCDebug(YubiKeyDeviceManagerLog) << "Device" << deviceId << "successfully removed from memory";

    // Emit deviceForgotten signal (not deviceDisconnected)
    // deviceForgotten means "remove from D-Bus completely"
    // deviceDisconnected means "mark as IsConnected=false but keep on D-Bus"
    Q_EMIT deviceForgotten(deviceId);
    qCDebug(YubiKeyDeviceManagerLog) << "Emitted deviceForgotten signal for" << deviceId;

    // Emit credentials changed since this device's credentials are now gone
    Q_EMIT credentialsChanged();
}


} // namespace Daemon
} // namespace YubiKeyOath
