#include "yubikey_device_manager.h"
#include "yubikey_oath_device.h"
#include "nitrokey_oath_device.h"
#include "yk_oath_session.h"
#include "nitrokey_oath_session.h"
#include "oath_protocol.h"
#include "../logging_categories.h"
#include "../infrastructure/pcsc_worker_pool.h"
#include "../../shared/types/device_brand.h"

#include <QDebug>
#include <QMetaObject>
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

    // Register metatypes for cross-thread signal/slot connections
    qRegisterMetaType<QList<OathCredential>>("QList<OathCredential>");
    qCDebug(YubiKeyDeviceManagerLog) << "Registered QList<OathCredential> metatype for cross-thread signals";

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

    const LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &m_context);
    if (result != SCARD_S_SUCCESS) {
        qCDebug(YubiKeyDeviceManagerLog) << "Failed to establish PC/SC context:" << result;
        const QString error = tr("Failed to establish PC/SC context: %1").arg(result);
        Q_EMIT errorOccurred(error);
        return Result<void>::error(error);
    }

    qCDebug(YubiKeyDeviceManagerLog) << "PC/SC context established successfully";
    m_initialized = true;

    qCInfo(YubiKeyDeviceManagerLog) << "initialize() completed - PC/SC context ready";
    qCInfo(YubiKeyDeviceManagerLog) << "NOTE: Reader monitoring NOT started - call startMonitoring() after D-Bus is ready";

    // NOTE: Monitoring and device enumeration are deferred to startMonitoring()
    // which should be called after D-Bus interface is fully initialized

    return Result<void>::success();
}

void YubiKeyDeviceManager::cleanup() {
    qCDebug(YubiKeyDeviceManagerLog) << "cleanup() - stopping card reader monitor";
    m_readerMonitor->stopMonitoring();

    // Disconnect all devices
    QStringList deviceIds;
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
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

void YubiKeyDeviceManager::startMonitoring()
{
    if (!m_initialized || !m_context) {
        qCCritical(YubiKeyDeviceManagerLog) << "startMonitoring() failed - PC/SC context not initialized. Call initialize() first.";
        return;
    }

    qCInfo(YubiKeyDeviceManagerLog) << "startMonitoring() - Starting PC/SC reader monitoring and device enumeration";

    // Start reader monitoring event loop (polls every 500ms for card insertion/removal)
    qCDebug(YubiKeyDeviceManagerLog) << "Starting card reader monitor";
    m_readerMonitor->startMonitoring(m_context);

    // ASYNC: Enumerate existing readers in background to avoid blocking
    // This will connect to all currently inserted cards
    qCDebug(YubiKeyDeviceManagerLog) << "Scheduling async device enumeration (non-blocking)";
    QTimer::singleShot(0, this, &YubiKeyDeviceManager::enumerateAndConnectDevicesAsync);

    qCInfo(YubiKeyDeviceManagerLog) << "startMonitoring() completed - monitoring active, async enumeration in progress";
    // Future device connections are handled by CardReaderMonitor via onCardInserted signal
}

bool YubiKeyDeviceManager::hasConnectedDevices() const {
    QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
    // Return true if ANY device is connected
    const bool anyConnected = !m_devices.empty();
    qCDebug(YubiKeyDeviceManagerLog) << "hasConnectedDevices() - connected devices:" << m_devices.size()
             << "returning:" << anyConnected;
    return anyConnected;
}

QString YubiKeyDeviceManager::connectToDevice(const QString &readerName) {
    qCDebug(YubiKeyDeviceManagerLog) << "=== connectToDevice() START ===" << readerName;

    if (!m_initialized) {
        qCDebug(YubiKeyDeviceManagerLog) << "Not initialized, cannot connect";
        return {};
    }

    qCDebug(YubiKeyDeviceManagerLog) << "Step 1: Attempting PC/SC connection to reader:" << readerName;

    // Connect to card
    SCARDHANDLE cardHandle = 0;
    DWORD protocol = 0;

    const QByteArray readerBytes = readerName.toUtf8();
    const LONG result = SCardConnect(m_context, readerBytes.constData(),
                              SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
                              &cardHandle, &protocol);

    qCDebug(YubiKeyDeviceManagerLog) << "SCardConnect result:" << result << "protocol:" << protocol;

    if (result != SCARD_S_SUCCESS) {
        qCDebug(YubiKeyDeviceManagerLog) << "Could not connect to reader" << readerName << "- error code:" << result << "(this is normal if no card is present)";
        return {};  // Silently return - this is expected when no card is present
    }

    qCDebug(YubiKeyDeviceManagerLog) << "Successfully connected to PC/SC reader, cardHandle:" << cardHandle;

    qCDebug(YubiKeyDeviceManagerLog) << "Step 2: Attempting to SELECT OATH application";

    // Select OATH application to get device ID using brand-specific OathSession
    QByteArray challenge;
    QString deviceId;
    Version firmwareVersion;  // Firmware version from SELECT response
    bool requiresPassword = false;  // Password requirement from SELECT response
    bool hasSelectSerial = false;   // TAG_SERIAL_NUMBER present in SELECT response

    {
        // Detect brand based on reader name (fast, preliminary detection)
        // Note: Will be refined after SELECT with firmware version and serial number presence
        using namespace YubiKeyOath::Shared;
        const DeviceBrand preliminaryBrand = detectBrand(readerName, Version(), false);

        qCDebug(YubiKeyDeviceManagerLog) << "Preliminary brand detection:" << brandName(preliminaryBrand)
                                         << "(based on reader name:" << readerName << ")";

        // Create brand-specific session for initial SELECT
        auto tempSession = createSession(preliminaryBrand, cardHandle, protocol, QString());

        const auto selectResult = tempSession->selectOathApplication(challenge, firmwareVersion);

        if (selectResult.isError()) {
            qCDebug(YubiKeyDeviceManagerLog) << "Card does not support OATH application:" << selectResult.error() << "- this is normal for non-OATH cards";
            SCardDisconnect(cardHandle, SCARD_LEAVE_CARD);
            return {};  // Silently return - this is expected for non-OATH cards
        }

        // Get device ID and password requirement from session
        deviceId = tempSession->deviceId();
        requiresPassword = tempSession->requiresPassword();
        hasSelectSerial = tempSession->selectSerialNumber() != 0;  // Check if serial in SELECT
    }

    if (deviceId.isEmpty()) {
        qCDebug(YubiKeyDeviceManagerLog) << "No device ID from SELECT, disconnecting";
        SCardDisconnect(cardHandle, SCARD_LEAVE_CARD);
        return {};
    }

    qCDebug(YubiKeyDeviceManagerLog) << "Got device ID:" << deviceId << "from SELECT response";

    // Check if this device is already connected (without lock to avoid deadlock with disconnectDevice)
    bool needsDisconnect = false;
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
        needsDisconnect = m_devices.contains(deviceId);
    }

    if (needsDisconnect) {
        qCDebug(YubiKeyDeviceManagerLog) << "Device" << deviceId << "is already connected, disconnecting old connection";
        disconnectDevice(deviceId);  // disconnectDevice has its own lock
    }

    // Final brand detection with all available information
    using namespace YubiKeyOath::Shared;
    const DeviceBrand finalBrand = detectBrand(readerName, firmwareVersion, hasSelectSerial);

    qCDebug(YubiKeyDeviceManagerLog) << "Final brand detection:" << brandName(finalBrand)
                                     << "(reader:" << readerName
                                     << ", firmware:" << firmwareVersion.toString()
                                     << ", hasSelectSerial:" << hasSelectSerial << ")";

    // Create brand-specific device instance using factory
    auto devicePtr = createDevice(finalBrand, deviceId, readerName, cardHandle, protocol, challenge, requiresPassword);

    // Get raw pointer for signal connections (before moving ownership to map)
    OathDevice* const device = devicePtr.get();

    // Connect device signals - forward for multi-device aggregation
    connect(device, &OathDevice::touchRequired,
            this, &YubiKeyDeviceManager::touchRequired);
    connect(device, &OathDevice::errorOccurred,
            this, &YubiKeyDeviceManager::errorOccurred);
    connect(device, &OathDevice::credentialsChanged,
            this, &YubiKeyDeviceManager::credentialsChanged);

    const bool connected = connect(device, &OathDevice::credentialCacheFetched,
            this, [this, deviceId](const QList<OathCredential> &credentials) {
                qWarning() << "YubiKeyDeviceManager: >>> credentialCacheFetched lambda CALLED for device:" << deviceId
                           << "credentials count:" << credentials.size();
                Q_EMIT credentialCacheFetchedForDevice(deviceId, credentials);
                qWarning() << "YubiKeyDeviceManager: >>> credentialCacheFetchedForDevice signal EMITTED";
            }, Qt::QueuedConnection);

    qWarning() << "YubiKeyDeviceManager: credentialCacheFetched connection" << (connected ? "SUCCEEDED" : "FAILED")
               << "for device:" << deviceId << "device ptr:" << device;

    // Connect reconnect signals for card reset handling
    connect(device, &OathDevice::needsReconnect,
            this, &YubiKeyDeviceManager::reconnectDeviceAsync);
    connect(this, &YubiKeyDeviceManager::reconnectCompleted,
            device, [device, deviceId](const QString &reconnectedDeviceId, bool success) {
        // Only forward to this device if the reconnect was for this device
        if (reconnectedDeviceId == deviceId) {
            device->onReconnectResult(success);
        }
    });

    // Critical section: add to device map
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
        m_devices[deviceId] = std::move(devicePtr);  // Move ownership to map
        qCDebug(YubiKeyDeviceManagerLog) << "Added device" << deviceId << "to map, total devices:" << m_devices.size();
    }

    // Emit device connected signal
    Q_EMIT deviceConnected(deviceId);
    qCDebug(YubiKeyDeviceManagerLog) << "Emitted deviceConnected signal for" << deviceId;

    // Register reader as in use to prevent duplicate connections
    m_readerToDeviceMap.insert(readerName, deviceId);
    qCDebug(YubiKeyDeviceManagerLog) << "Registered reader" << readerName << "for device" << deviceId;

    qCDebug(YubiKeyDeviceManagerLog) << "=== connectToDevice() SUCCESS ===" << deviceId << "on reader:" << readerName;

    return deviceId;
}

void YubiKeyDeviceManager::disconnectDevice(const QString &deviceId) {
    qCDebug(YubiKeyDeviceManagerLog) << "disconnectDevice() called for device:" << deviceId;

    // Critical section: check and remove from map
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)

        if (!m_devices.contains(deviceId)) {
            qCDebug(YubiKeyDeviceManagerLog) << "Device" << deviceId << "not found in cache";
            return;
        }

        // Get reader name before deleting device
        const QString readerName = m_devices[deviceId]->readerName();

        // Device destructor will handle PC/SC disconnection
        qCDebug(YubiKeyDeviceManagerLog) << "Deleting YubiKeyOathDevice instance for" << deviceId;

        // Remove from map - unique_ptr destructor automatically deletes device
        m_devices.erase(deviceId);

        // Remove reader from mapping
        m_readerToDeviceMap.remove(readerName);
        qCDebug(YubiKeyDeviceManagerLog) << "Unregistered reader" << readerName << "for device" << deviceId;

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
    QList<OathDevice*> devices;
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
        qCDebug(YubiKeyDeviceManagerLog) << "Aggregating credentials from" << m_devices.size() << "devices";
        devices.reserve(static_cast<qsizetype>(m_devices.size()));
        for (const auto &devicePair : std::as_const(m_devices)) {
            devices.append(devicePair.second.get());
        }
    }

    for (const OathDevice* const device : devices) {
        const QString deviceId = device->deviceId();

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
            qCDebug(YubiKeyDeviceManagerLog) << "Added credential from device" << deviceId << ":" << credential.originalName;
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
        QByteArray readersBuffer(static_cast<qsizetype>(readersLen), '\0');
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
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
        for (const auto &devicePair : m_devices) {
            const QString deviceReaderName = devicePair.second->readerName();
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
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
        for (const auto &devicePair : m_devices) {
            connectedReaderNames.insert(devicePair.second->readerName());
        }
    }

    // Find new readers (present in currentReaders but not in connectedReaderNames)
    for (const QString &readerName : currentReaders) {
        if (!connectedReaderNames.contains(readerName)) {
            // Try to connect to this reader (will succeed if it contains a YubiKey with OATH support)
            qCDebug(YubiKeyDeviceManagerLog) << "Attempting to connect to new reader:" << readerName;

            const QString deviceId = connectToDevice(readerName);
            if (!deviceId.isEmpty()) {
                qCDebug(YubiKeyDeviceManagerLog) << "Successfully connected to YubiKey device" << deviceId << "on new reader" << readerName;
                // Credential fetching will be triggered by onDeviceConnectedInternal in YubiKeyDBusService
            }
        }
    }
}

void YubiKeyDeviceManager::onCardInserted(const QString &readerName)
{
    qCDebug(YubiKeyDeviceManagerLog) << "onCardInserted() - reader:" << readerName;

    // Check if reader is already in use to prevent duplicate connections
    if (m_readerToDeviceMap.contains(readerName)) {
        const QString existingDeviceId = m_readerToDeviceMap.value(readerName);
        qCDebug(YubiKeyDeviceManagerLog) << "Reader" << readerName << "already in use by device"
                                         << existingDeviceId << "- ignoring duplicate cardInserted signal";
        return;
    }

    // ASYNC: Connect to device asynchronously to avoid blocking main thread
    connectToDeviceAsync(readerName);
    // Result will be signaled via deviceConnected() and deviceStateChanged()

}

void YubiKeyDeviceManager::onCardRemoved(const QString &readerName)
{
    qCDebug(YubiKeyDeviceManagerLog) << "onCardRemoved() - reader:" << readerName;

    // NEW: Multi-device support - find and disconnect specific device by reader name
    QString deviceIdToRemove;
    for (const auto &[deviceId, device] : m_devices) {
        if (device->readerName() == readerName) {
            deviceIdToRemove = deviceId;
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
    QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
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

OathDevice* YubiKeyDeviceManager::getDevice(const QString &deviceId)
{
    QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
    const auto it = m_devices.find(deviceId);
    if (it != m_devices.end()) {
        return it->second.get();  // Return raw pointer from unique_ptr
    }
    return nullptr;
}

OathDevice* YubiKeyDeviceManager::getDeviceOrFirst(const QString &deviceId)
{
    if (!deviceId.isEmpty()) {
        // Specific device requested
        return getDevice(deviceId);
    }

    // Get first available device
    const QStringList connectedIds = getConnectedDeviceIds();
    if (connectedIds.isEmpty()) {
        return nullptr;
    }

    return getDevice(connectedIds.first());
}

void YubiKeyDeviceManager::removeDeviceFromMemory(const QString &deviceId)
{
    qCDebug(YubiKeyDeviceManagerLog) << "removeDeviceFromMemory() called for device:" << deviceId;

    bool wasInCache = false;
    int remainingDevices = 0;

    // Critical section: check and remove from map
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)

        if (!m_devices.contains(deviceId)) {
            qCDebug(YubiKeyDeviceManagerLog) << "Device" << deviceId << "not found in cache (likely disconnected)";
            wasInCache = false;
        } else {
            // Device destructor will handle PC/SC disconnection
            qCDebug(YubiKeyDeviceManagerLog) << "Removing YubiKeyOathDevice instance for" << deviceId << "from memory";

            // Remove from map - unique_ptr destructor automatically deletes device
            m_devices.erase(deviceId);
            remainingDevices = static_cast<int>(m_devices.size());
            wasInCache = true;

            qCDebug(YubiKeyDeviceManagerLog) << "Removed device" << deviceId << "from memory, remaining devices:" << remainingDevices;
        }
    }
    // Lock released here, device already deleted by unique_ptr (if was in cache)

    if (wasInCache) {
        qCDebug(YubiKeyDeviceManagerLog) << "Device" << deviceId << "successfully removed from memory";
    }

    // CRITICAL: Make a local copy of deviceId before emitting signals
    // The deviceForgotten signal triggers D-Bus object deletion (OathManagerObject::removeDevice())
    // which may invalidate the deviceId reference if it points to the D-Bus object's member variable
    // Using the reference after object deletion would cause use-after-delete (segfault)
    const QString deviceIdCopy = deviceId;  // NOLINT(performance-unnecessary-copy-initialization) - Intentional copy to prevent use-after-delete

    // CRITICAL: ALWAYS emit deviceForgotten signal, even if device wasn't in cache
    // This is necessary because:
    // - D-Bus objects exist for BOTH connected AND disconnected devices
    // - deviceForgotten signal triggers D-Bus object removal via OathManagerObject::removeDevice()
    // - Disconnected devices need D-Bus cleanup too (they're not in cache but still in D-Bus tree)
    Q_EMIT deviceForgotten(deviceIdCopy);
    qCDebug(YubiKeyDeviceManagerLog) << "Emitted deviceForgotten signal for" << deviceIdCopy
                                      << (wasInCache ? "(was in cache)" : "(was NOT in cache - disconnected)");

    // Emit credentials changed since this device's credentials are now gone
    Q_EMIT credentialsChanged();
}

void YubiKeyDeviceManager::reconnectDeviceAsync(const QString &deviceId, const QString &readerName, const QByteArray &command)
{
    qCDebug(YubiKeyDeviceManagerLog) << "reconnectDeviceAsync() called for device" << deviceId
             << "reader:" << readerName << "command length:" << command.length();

    // Stop any existing reconnect operation
    if (m_reconnectTimer) {
        qCDebug(YubiKeyDeviceManagerLog) << "Stopping existing reconnect timer";
        m_reconnectTimer->stop();
        delete m_reconnectTimer;
        m_reconnectTimer = nullptr;
    }

    // CRITICAL FIX: Copy parameters to local variables BEFORE device operations
    // because references may point to fields of the device object
    const QString deviceIdCopy = deviceId;  // NOLINT(performance-unnecessary-copy-initialization) - Intentional copy for safety
    const QString readerNameCopy = readerName;  // NOLINT(performance-unnecessary-copy-initialization) - Intentional copy for safety
    const QByteArray commandCopy = command;  // NOLINT(performance-unnecessary-copy-initialization) - Intentional copy for safety

    // Store reconnect parameters (using copies)
    m_reconnectDeviceId = deviceIdCopy;
    m_reconnectReaderName = readerNameCopy;
    m_reconnectCommand = commandCopy;

    // NEW APPROACH: Reconnect card handle WITHOUT destroying device object
    // This avoids race condition with background threads using the device
    qCDebug(YubiKeyDeviceManagerLog) << "Starting async reconnect for device" << deviceIdCopy;

    // Emit signal that reconnect started (for notification display)
    Q_EMIT reconnectStarted(deviceIdCopy);

    // Use Qt's async mechanism to avoid blocking main thread
    // reconnectCardHandle() has exponential backoff built-in
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &YubiKeyDeviceManager::onReconnectTimer);

    qCDebug(YubiKeyDeviceManagerLog) << "Starting reconnect with 10ms initial delay";
    m_reconnectTimer->start(10);  // Small delay to let ykman release the card
}

void YubiKeyDeviceManager::onReconnectTimer()
{
    qCDebug(YubiKeyDeviceManagerLog) << "onReconnectTimer() for device" << m_reconnectDeviceId
             << "reader:" << m_reconnectReaderName;

    // Get device instance (without destroying it)
    auto *const device = getDevice(m_reconnectDeviceId);
    if (!device) {
        qCWarning(YubiKeyDeviceManagerLog) << "Device" << m_reconnectDeviceId << "no longer exists";

        // Stop timer and cleanup
        if (m_reconnectTimer) {
            delete m_reconnectTimer;
            m_reconnectTimer = nullptr;
        }

        // Emit failure signal
        Q_EMIT reconnectCompleted(m_reconnectDeviceId, false);

        // Clear reconnect state
        m_reconnectDeviceId.clear();
        m_reconnectReaderName.clear();
        m_reconnectCommand.clear();

        return;
    }

    // Try to reconnect card handle (has exponential backoff built-in)
    qCDebug(YubiKeyDeviceManagerLog) << "Calling reconnectCardHandle() on device" << m_reconnectDeviceId;
    const auto result = device->reconnectCardHandle(m_reconnectReaderName);

    // Stop timer and cleanup
    if (m_reconnectTimer) {
        delete m_reconnectTimer;
        m_reconnectTimer = nullptr;
    }

    if (result.isSuccess()) {
        // Success!
        qCInfo(YubiKeyDeviceManagerLog) << "Reconnect successful for device" << m_reconnectDeviceId;

        // Emit success signal
        Q_EMIT reconnectCompleted(m_reconnectDeviceId, true);
    } else {
        // Failed after all retry attempts
        qCWarning(YubiKeyDeviceManagerLog) << "Reconnect failed for device" << m_reconnectDeviceId
                 << "error:" << result.error();

        // Emit failure signal
        Q_EMIT reconnectCompleted(m_reconnectDeviceId, false);
    }

    // Clear reconnect state
    m_reconnectDeviceId.clear();
    m_reconnectReaderName.clear();
    m_reconnectCommand.clear();
}

// Factory Methods (private)

std::unique_ptr<YkOathSession> YubiKeyDeviceManager::createSession(
    Shared::DeviceBrand brand,
    SCARDHANDLE cardHandle,
    DWORD protocol,
    const QString &deviceId)
{
    using namespace YubiKeyOath::Shared;

    switch (brand) {
    case DeviceBrand::Nitrokey:
        return std::make_unique<NitrokeyOathSession>(cardHandle, protocol, deviceId, this);

    case DeviceBrand::YubiKey:
    case DeviceBrand::Unknown:
    default:
        return std::make_unique<YkOathSession>(cardHandle, protocol, deviceId, this);
    }
}

std::unique_ptr<OathDevice> YubiKeyDeviceManager::createDevice(
    Shared::DeviceBrand brand,
    const QString &deviceId,
    const QString &readerName,
    SCARDHANDLE cardHandle,
    DWORD protocol,
    const QByteArray &challenge,
    bool requiresPassword)
{
    using namespace YubiKeyOath::Shared;

    switch (brand) {
    case DeviceBrand::Nitrokey:
        return std::make_unique<NitrokeyOathDevice>(
            deviceId, readerName, cardHandle, protocol,
            challenge, requiresPassword, m_context, this);

    case DeviceBrand::YubiKey:
    case DeviceBrand::Unknown:
    default:
        return std::make_unique<YubiKeyOathDevice>(
            deviceId, readerName, cardHandle, protocol,
            challenge, requiresPassword, m_context, this);
    }
}

void YubiKeyDeviceManager::enumerateAndConnectDevicesAsync()
{
    qCDebug(YubiKeyDeviceManagerLog) << "=== enumerateAndConnectDevicesAsync() START ===";

    if (!m_initialized) {
        qCWarning(YubiKeyDeviceManagerLog) << "Cannot enumerate devices - manager not initialized";
        return;
    }

    qCDebug(YubiKeyDeviceManagerLog) << "Checking for existing PC/SC readers";
    DWORD readersLen = 0;
    LONG startupResult = SCardListReaders(m_context, nullptr, nullptr, &readersLen);

    if (startupResult == SCARD_S_SUCCESS && readersLen > 0) {
        QByteArray readersBuffer(static_cast<qsizetype>(readersLen), '\0');
        startupResult = SCardListReaders(m_context, nullptr, readersBuffer.data(), &readersLen);

        if (startupResult == SCARD_S_SUCCESS) {
            // Parse reader names (null-terminated strings)
            const char* readerName = readersBuffer.constData();
            QStringList readers;
            while (*readerName) {
                readers.append(QString::fromUtf8(readerName));
                readerName += strlen(readerName) + 1;
            }

            qCDebug(YubiKeyDeviceManagerLog) << "Found" << readers.size() << "readers:" << readers;

            // Connect to each reader asynchronously
            for (const QString &reader : readers) {
                qCDebug(YubiKeyDeviceManagerLog) << "Scheduling async connection to reader:" << reader;
                connectToDeviceAsync(reader);
            }
        } else {
            qCWarning(YubiKeyDeviceManagerLog) << "SCardListReaders failed:" << QString::number(startupResult, 16);
        }
    } else if (startupResult == SCARD_E_NO_READERS_AVAILABLE) {
        qCDebug(YubiKeyDeviceManagerLog) << "No PC/SC readers available";
    } else {
        qCWarning(YubiKeyDeviceManagerLog) << "SCardListReaders failed:" << QString::number(startupResult, 16);
    }

    qCDebug(YubiKeyDeviceManagerLog) << "=== enumerateAndConnectDevicesAsync() END ===";
}

void YubiKeyDeviceManager::connectToDeviceAsync(const QString &readerName)
{
    qCDebug(YubiKeyDeviceManagerLog) << "connectToDeviceAsync() - scheduling async connection to" << readerName;

    // Use PcscWorkerPool to execute connection asynchronously
    // Note: We need to capture 'this' and 'readerName' for the operation
    // The operation will run on a worker thread and emit signals back to main thread

    using namespace YubiKeyOath::Shared;

    // Submit to worker pool with Normal priority (startup initialization)
    PcscWorkerPool::instance().submit(
        readerName,  // Use reader name as device ID for rate limiting
        [this, readerName]() {
            // This lambda runs on worker thread - PC/SC operations safe here
            qCDebug(YubiKeyDeviceManagerLog) << "[Worker] Connecting to device on reader:" << readerName;

            // Emit state change: Connecting (on main thread)
            QMetaObject::invokeMethod(this, [this, readerName]() {
                // We don't have deviceId yet, so emit with reader name as placeholder
                Q_EMIT deviceStateChanged(readerName, DeviceState::Connecting);
            }, Qt::QueuedConnection);

            // Call synchronous connectToDevice() on worker thread
            const QString deviceId = connectToDevice(readerName);

            // Emit result back to main thread
            QMetaObject::invokeMethod(this, [this, deviceId, readerName]() {
                if (!deviceId.isEmpty()) {
                    qCDebug(YubiKeyDeviceManagerLog) << "Async connection succeeded for device" << deviceId;
                    // deviceConnected signal already emitted by connectToDevice()
                    // Emit Ready state
                    Q_EMIT deviceStateChanged(deviceId, DeviceState::Ready);
                } else {
                    qCDebug(YubiKeyDeviceManagerLog) << "Async connection failed for reader" << readerName;
                    // Emit Error state with reader name (no device ID available)
                    Q_EMIT deviceStateChanged(readerName, DeviceState::Error);
                }
            }, Qt::QueuedConnection);
        },
        PcscOperationPriority::Normal
    );

    qCDebug(YubiKeyDeviceManagerLog) << "connectToDeviceAsync() - task queued for" << readerName;
}

} // namespace Daemon
} // namespace YubiKeyOath
