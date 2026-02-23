#include "oath_device_manager.h"
#include "yubikey_oath_device.h"
#include "nitrokey_oath_device.h"
#include "yk_oath_session.h"
#include "nitrokey_oath_session.h"
#include "oath_protocol.h"
#include "../logging_categories.h"
#include "../infrastructure/pcsc_worker_pool.h"
#include "../infrastructure/device_reconnect_coordinator.h"
#include "../../shared/types/device_brand.h"
#include "../../shared/config/configuration_provider.h"

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

OathDeviceManager::OathDeviceManager(QObject* parent)
    : QObject(parent)
    , m_readerMonitor(new CardReaderMonitor(this))
    , m_reconnectCoordinator(std::make_unique<DeviceReconnectCoordinator>(this))
{
    qCDebug(OathDeviceManagerLog) << "Constructor called";

    // Register metatypes for cross-thread signal/slot connections
    qRegisterMetaType<QList<OathCredential>>("QList<OathCredential>");
    qCDebug(OathDeviceManagerLog) << "Registered QList<OathCredential> metatype for cross-thread signals";

    // Connect card reader monitor signals
    connect(m_readerMonitor, &CardReaderMonitor::readerListChanged,
            this, &OathDeviceManager::onReaderListChanged);
    connect(m_readerMonitor, &CardReaderMonitor::cardInserted,
            this, &OathDeviceManager::onCardInserted);
    connect(m_readerMonitor, &CardReaderMonitor::cardRemoved,
            this, &OathDeviceManager::onCardRemoved);
    connect(m_readerMonitor, &CardReaderMonitor::pcscServiceLost,
            this, &OathDeviceManager::handlePcscServiceLost);

    // Connect async credential cache fetching
    connect(this, &OathDeviceManager::credentialCacheFetchedForDevice,
            this, &OathDeviceManager::onCredentialCacheFetchedForDevice);

    // Forward reconnect coordinator signals
    connect(m_reconnectCoordinator.get(), &DeviceReconnectCoordinator::reconnectStarted,
            this, &OathDeviceManager::reconnectStarted);
    connect(m_reconnectCoordinator.get(), &DeviceReconnectCoordinator::reconnectCompleted,
            this, &OathDeviceManager::reconnectCompleted);
}

OathDeviceManager::~OathDeviceManager() {
    cleanup();
}

void OathDeviceManager::setConfiguration(Shared::ConfigurationProvider *config)
{
    m_config = config;
    qCDebug(OathDeviceManagerLog) << "Configuration provider set, pcscRateLimitMs:"
                                     << (m_config ? m_config->pcscRateLimitMs() : -1);
}

Result<void> OathDeviceManager::initialize() {
    qCDebug(OathDeviceManagerLog) << "initialize() called";
    if (m_initialized) {
        qCDebug(OathDeviceManagerLog) << "Already initialized";
        return Result<void>::success();
    }

    const LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &m_context);
    if (result != SCARD_S_SUCCESS) {
        qCDebug(OathDeviceManagerLog) << "Failed to establish PC/SC context:" << result;
        const QString error = tr("Failed to establish PC/SC context: %1").arg(result);
        Q_EMIT errorOccurred(error);
        return Result<void>::error(error);
    }

    qCDebug(OathDeviceManagerLog) << "PC/SC context established successfully";
    m_initialized = true;

    qCInfo(OathDeviceManagerLog) << "initialize() completed - PC/SC context ready";
    qCInfo(OathDeviceManagerLog) << "NOTE: Reader monitoring NOT started - call startMonitoring() after D-Bus is ready";

    // NOTE: Monitoring and device enumeration are deferred to startMonitoring()
    // which should be called after D-Bus interface is fully initialized

    return Result<void>::success();
}

void OathDeviceManager::cleanup() {
    qCDebug(OathDeviceManagerLog) << "cleanup() - stopping card reader monitor";
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

void OathDeviceManager::startMonitoring()
{
    if (!m_initialized || !m_context) {
        qCCritical(OathDeviceManagerLog) << "startMonitoring() failed - PC/SC context not initialized. Call initialize() first.";
        return;
    }

    qCInfo(OathDeviceManagerLog) << "startMonitoring() - Starting PC/SC reader monitoring and device enumeration";

    // Start reader monitoring event loop (polls every 500ms for card insertion/removal)
    qCDebug(OathDeviceManagerLog) << "Starting card reader monitor";
    m_readerMonitor->startMonitoring(m_context);

    // ASYNC: Enumerate existing readers in background to avoid blocking
    // This will connect to all currently inserted cards
    qCDebug(OathDeviceManagerLog) << "Scheduling async device enumeration (non-blocking)";
    QTimer::singleShot(0, this, &OathDeviceManager::enumerateAndConnectDevicesAsync);

    qCInfo(OathDeviceManagerLog) << "startMonitoring() completed - monitoring active, async enumeration in progress";
    // Future device connections are handled by CardReaderMonitor via onCardInserted signal
}

bool OathDeviceManager::hasConnectedDevices() const {
    QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
    // Return true if ANY device is connected
    const bool anyConnected = !m_devices.empty();
    qCDebug(OathDeviceManagerLog) << "hasConnectedDevices() - connected devices:" << m_devices.size()
             << "returning:" << anyConnected;
    return anyConnected;
}

QString OathDeviceManager::connectToDevice(const QString &readerName) {
    qCDebug(OathDeviceManagerLog) << "=== connectToDevice() START ===" << readerName;

    if (!m_initialized) {
        qCDebug(OathDeviceManagerLog) << "Not initialized, cannot connect";
        return {};
    }

    qCDebug(OathDeviceManagerLog) << "Step 1: Attempting PC/SC connection to reader:" << readerName;

    // Connect to card
    SCARDHANDLE cardHandle = 0;
    DWORD protocol = 0;

    const QByteArray readerBytes = readerName.toUtf8();
    const LONG result = SCardConnect(m_context, readerBytes.constData(),
                              SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
                              &cardHandle, &protocol);

    qCDebug(OathDeviceManagerLog) << "SCardConnect result:" << result << "protocol:" << protocol;

    if (result != SCARD_S_SUCCESS) {
        qCDebug(OathDeviceManagerLog) << "Could not connect to reader" << readerName << "- error code:" << result << "(this is normal if no card is present)";
        return {};  // Silently return - this is expected when no card is present
    }

    qCDebug(OathDeviceManagerLog) << "Successfully connected to PC/SC reader, cardHandle:" << cardHandle;

    qCDebug(OathDeviceManagerLog) << "Step 2: Attempting to SELECT OATH application";

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

        qCDebug(OathDeviceManagerLog) << "Preliminary brand detection:" << brandName(preliminaryBrand)
                                         << "(based on reader name:" << readerName << ")";

        // Create brand-specific session for initial SELECT
        auto tempSession = createSession(preliminaryBrand, cardHandle, protocol, QString());

        const auto selectResult = tempSession->selectOathApplication(challenge, firmwareVersion);

        if (selectResult.isError()) {
            qCDebug(OathDeviceManagerLog) << "Card does not support OATH application:" << selectResult.error() << "- this is normal for non-OATH cards";
            SCardDisconnect(cardHandle, SCARD_LEAVE_CARD);
            return {};  // Silently return - this is expected for non-OATH cards
        }

        // Get device ID and password requirement from session
        deviceId = tempSession->deviceId();
        requiresPassword = tempSession->requiresPassword();
        hasSelectSerial = tempSession->selectSerialNumber() != 0;  // Check if serial in SELECT
    }

    if (deviceId.isEmpty()) {
        qCDebug(OathDeviceManagerLog) << "No device ID from SELECT, disconnecting";
        SCardDisconnect(cardHandle, SCARD_LEAVE_CARD);
        return {};
    }

    qCDebug(OathDeviceManagerLog) << "Got device ID:" << deviceId << "from SELECT response";

    // Check if this device is already connected (without lock to avoid deadlock with disconnectDevice)
    bool needsDisconnect = false;
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
        needsDisconnect = m_devices.contains(deviceId);
    }

    if (needsDisconnect) {
        qCDebug(OathDeviceManagerLog) << "Device" << deviceId << "is already connected, disconnecting old connection";
        disconnectDevice(deviceId);  // disconnectDevice has its own lock
    }

    // Final brand detection with all available information
    using namespace YubiKeyOath::Shared;
    const DeviceBrand finalBrand = detectBrand(readerName, firmwareVersion, hasSelectSerial);

    qCDebug(OathDeviceManagerLog) << "Final brand detection:" << brandName(finalBrand)
                                     << "(reader:" << readerName
                                     << ", firmware:" << firmwareVersion.toString()
                                     << ", hasSelectSerial:" << hasSelectSerial << ")";

    // Create brand-specific device instance using factory
    auto devicePtr = createDevice(finalBrand, deviceId, readerName, cardHandle, protocol, challenge, requiresPassword);

    // Get raw pointer for signal connections (before moving ownership to map)
    OathDevice* const device = devicePtr.get();

    // Connect device signals - forward for multi-device aggregation
    connect(device, &OathDevice::touchRequired,
            this, &OathDeviceManager::touchRequired);
    connect(device, &OathDevice::errorOccurred,
            this, &OathDeviceManager::errorOccurred);
    connect(device, &OathDevice::credentialsChanged,
            this, &OathDeviceManager::credentialsChanged);

    const bool connected = connect(device, &OathDevice::credentialCacheFetched,
            this, [this, deviceId](const QList<OathCredential> &credentials) {
                qWarning() << "OathDeviceManager: >>> credentialCacheFetched lambda CALLED for device:" << deviceId
                           << "credentials count:" << credentials.size();
                Q_EMIT credentialCacheFetchedForDevice(deviceId, credentials);
                qWarning() << "OathDeviceManager: >>> credentialCacheFetchedForDevice signal EMITTED";
            }, Qt::QueuedConnection);

    qWarning() << "OathDeviceManager: credentialCacheFetched connection" << (connected ? "SUCCEEDED" : "FAILED")
               << "for device:" << deviceId << "device ptr:" << device;

    // Connect reconnect signals for card reset handling
    connect(device, &OathDevice::needsReconnect,
            this, &OathDeviceManager::reconnectDeviceAsync);
    connect(this, &OathDeviceManager::reconnectCompleted,
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
        qCDebug(OathDeviceManagerLog) << "Added device" << deviceId << "to map, total devices:" << m_devices.size();
    }

    // Emit device connected signal
    Q_EMIT deviceConnected(deviceId);
    qCDebug(OathDeviceManagerLog) << "Emitted deviceConnected signal for" << deviceId;

    // Register reader as in use to prevent duplicate connections
    m_readerToDeviceMap.insert(readerName, deviceId);
    qCDebug(OathDeviceManagerLog) << "Registered reader" << readerName << "for device" << deviceId;

    qCDebug(OathDeviceManagerLog) << "=== connectToDevice() SUCCESS ===" << deviceId << "on reader:" << readerName;

    return deviceId;
}

void OathDeviceManager::disconnectDevice(const QString &deviceId) {
    qCDebug(OathDeviceManagerLog) << "disconnectDevice() called for device:" << deviceId;

    // Critical section: check and remove from map
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)

        if (!m_devices.contains(deviceId)) {
            qCDebug(OathDeviceManagerLog) << "Device" << deviceId << "not found in cache";
            return;
        }

        // Get reader name before deleting device
        const QString readerName = m_devices[deviceId]->readerName();

        // Device destructor will handle PC/SC disconnection
        qCDebug(OathDeviceManagerLog) << "Deleting YubiKeyOathDevice instance for" << deviceId;

        // Remove from map - unique_ptr destructor automatically deletes device
        m_devices.erase(deviceId);

        // Remove reader from mapping
        m_readerToDeviceMap.remove(readerName);
        qCDebug(OathDeviceManagerLog) << "Unregistered reader" << readerName << "for device" << deviceId;

        qCDebug(OathDeviceManagerLog) << "Removed device" << deviceId << "from map, remaining devices:" << m_devices.size();
    }
    // Lock released here, device already deleted by unique_ptr

    // Emit device disconnected signal
    Q_EMIT deviceDisconnected(deviceId);
    qCDebug(OathDeviceManagerLog) << "Emitted deviceDisconnected signal for" << deviceId;

    // Emit credentials changed if we had any credentials for this device
    Q_EMIT credentialsChanged();
}

QList<OathCredential> OathDeviceManager::getCredentials() {
    qCDebug(OathDeviceManagerLog) << "getCredentials() called";

    // NEW: Multi-device aggregation
    // Aggregate credentials from all connected devices
    QList<OathCredential> aggregatedCredentials;

    // Copy device list under lock to avoid holding lock during credential fetching
    QList<OathDevice*> devices;
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
        qCDebug(OathDeviceManagerLog) << "Aggregating credentials from" << m_devices.size() << "devices";
        devices.reserve(static_cast<qsizetype>(m_devices.size()));
        for (const auto &devicePair : std::as_const(m_devices)) {
            devices.append(devicePair.second.get());
        }
    }

    for (const OathDevice* const device : devices) {
        const QString deviceId = device->deviceId();

        qCDebug(OathDeviceManagerLog) << "Processing device" << deviceId
                 << "- has" << device->credentials().size() << "credentials"
                 << "- updateInProgress:" << device->isUpdateInProgress();

        // Skip devices that are currently updating
        if (device->isUpdateInProgress()) {
            qCDebug(OathDeviceManagerLog) << "Skipping device" << deviceId << "- update in progress";
            continue;
        }

        // Add credentials from this device to aggregated list
        for (const auto &credential : device->credentials()) {
            aggregatedCredentials.append(credential);
            qCDebug(OathDeviceManagerLog) << "Added credential from device" << deviceId << ":" << credential.originalName;
        }
    }

    qCDebug(OathDeviceManagerLog) << "Returning" << aggregatedCredentials.size() << "aggregated credentials from all devices";

    return aggregatedCredentials;
}

void OathDeviceManager::onReaderListChanged()
{
    qCDebug(OathDeviceManagerLog) << "onReaderListChanged() - reader list changed";

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
            qCDebug(OathDeviceManagerLog) << "Current readers:" << currentReaders;
        }
    } else if (result == SCARD_E_NO_READERS_AVAILABLE) {
        qCDebug(OathDeviceManagerLog) << "No readers available";
    } else if (result != SCARD_S_SUCCESS) {
        qCDebug(OathDeviceManagerLog) << "SCardListReaders failed:" << QString::number(result, 16);
    }

    // Check each connected device - disconnect if its reader no longer exists
    QStringList devicesToDisconnect;
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
        for (const auto &devicePair : m_devices) {
            const QString deviceReaderName = devicePair.second->readerName();
            if (!currentReaders.contains(deviceReaderName)) {
                qCDebug(OathDeviceManagerLog) << "Device" << devicePair.first
                         << "reader" << deviceReaderName << "no longer exists - will disconnect";
                devicesToDisconnect.append(devicePair.first);
            }
        }
    }

    // Disconnect devices outside the lock to avoid deadlock
    for (const QString &deviceId : devicesToDisconnect) {
        qCDebug(OathDeviceManagerLog) << "Disconnecting device" << deviceId << "- reader removed";
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
            qCDebug(OathDeviceManagerLog) << "Attempting to connect to new reader:" << readerName;

            const QString deviceId = connectToDevice(readerName);
            if (!deviceId.isEmpty()) {
                qCDebug(OathDeviceManagerLog) << "Successfully connected to YubiKey device" << deviceId << "on new reader" << readerName;
                // Credential fetching will be triggered by onDeviceConnectedInternal in OathDBusService
            }
        }
    }
}

void OathDeviceManager::onCardInserted(const QString &readerName)
{
    qCDebug(OathDeviceManagerLog) << "onCardInserted() - reader:" << readerName;

    // Check if reader is already in use to prevent duplicate connections
    if (m_readerToDeviceMap.contains(readerName)) {
        const QString existingDeviceId = m_readerToDeviceMap.value(readerName);
        qCDebug(OathDeviceManagerLog) << "Reader" << readerName << "already in use by device"
                                         << existingDeviceId << "- ignoring duplicate cardInserted signal";
        return;
    }

    // ASYNC: Connect to device asynchronously to avoid blocking main thread
    connectToDeviceAsync(readerName);
    // Result will be signaled via deviceConnected() and deviceStateChanged()

}

void OathDeviceManager::onCardRemoved(const QString &readerName)
{
    qCDebug(OathDeviceManagerLog) << "onCardRemoved() - reader:" << readerName;

    // NEW: Multi-device support - find and disconnect specific device by reader name
    QString deviceIdToRemove;
    for (const auto &[deviceId, device] : m_devices) {
        if (device->readerName() == readerName) {
            deviceIdToRemove = deviceId;
            break;
        }
    }

    if (!deviceIdToRemove.isEmpty()) {
        qCDebug(OathDeviceManagerLog) << "Found device" << deviceIdToRemove << "on reader" << readerName << "- disconnecting";
        disconnectDevice(deviceIdToRemove);
        // credentialsChanged() signal is emitted automatically by disconnectDevice()
    } else {
        qCDebug(OathDeviceManagerLog) << "No device found for reader" << readerName;
    }

}

QStringList OathDeviceManager::getConnectedDeviceIds() const {
    QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
    QStringList deviceIds;
    for (const auto &devicePair : m_devices) {
        deviceIds.append(devicePair.first);
    }
    return deviceIds;
}

void OathDeviceManager::onCredentialCacheFetchedForDevice(const QString &deviceId, const QList<OathCredential> &credentials) {
    qCDebug(OathDeviceManagerLog) << "onCredentialCacheFetchedForDevice() called for device" << deviceId
             << "with" << credentials.size() << "credentials";

    // Device has already updated its internal credential cache
    // Just emit the manager-level signal for any listeners
    Q_EMIT credentialsChanged();
}

OathDevice* OathDeviceManager::getDevice(const QString &deviceId)
{
    QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)
    const auto it = m_devices.find(deviceId);
    if (it != m_devices.end()) {
        return it->second.get();  // Return raw pointer from unique_ptr
    }
    return nullptr;
}

OathDevice* OathDeviceManager::getDeviceOrFirst(const QString &deviceId)
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

void OathDeviceManager::removeDeviceFromMemory(const QString &deviceId)
{
    qCDebug(OathDeviceManagerLog) << "removeDeviceFromMemory() called for device:" << deviceId;

    bool wasInCache = false;
    int remainingDevices = 0;

    // Critical section: check and remove from map
    {
        QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness)

        if (!m_devices.contains(deviceId)) {
            qCDebug(OathDeviceManagerLog) << "Device" << deviceId << "not found in cache (likely disconnected)";
            wasInCache = false;
        } else {
            // Device destructor will handle PC/SC disconnection
            qCDebug(OathDeviceManagerLog) << "Removing YubiKeyOathDevice instance for" << deviceId << "from memory";

            // Remove from map - unique_ptr destructor automatically deletes device
            m_devices.erase(deviceId);
            remainingDevices = static_cast<int>(m_devices.size());
            wasInCache = true;

            qCDebug(OathDeviceManagerLog) << "Removed device" << deviceId << "from memory, remaining devices:" << remainingDevices;
        }
    }
    // Lock released here, device already deleted by unique_ptr (if was in cache)

    if (wasInCache) {
        qCDebug(OathDeviceManagerLog) << "Device" << deviceId << "successfully removed from memory";
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
    qCDebug(OathDeviceManagerLog) << "Emitted deviceForgotten signal for" << deviceIdCopy
                                      << (wasInCache ? "(was in cache)" : "(was NOT in cache - disconnected)");

    // Emit credentials changed since this device's credentials are now gone
    Q_EMIT credentialsChanged();
}

void OathDeviceManager::reconnectDeviceAsync(const QString &deviceId, const QString &readerName, const QByteArray &command)
{
    qCDebug(OathDeviceManagerLog) << "reconnectDeviceAsync() called for device" << deviceId
             << "reader:" << readerName << "command length:" << command.length();

    // Set up reconnect function that will be called by coordinator
    m_reconnectCoordinator->setReconnectFunction(
        [this, deviceId](const QString &reader) -> Result<void> {
            auto *const device = getDevice(deviceId);
            if (!device) {
                qCWarning(OathDeviceManagerLog) << "Device" << deviceId << "no longer exists";
                return Result<void>::error(QStringLiteral("Device no longer exists"));
            }
            return device->reconnectCardHandle(reader);
        });

    // Start reconnection (coordinator handles timing and signals)
    m_reconnectCoordinator->startReconnect(deviceId, readerName, command);
}

// Factory Methods (private)

std::unique_ptr<YkOathSession> OathDeviceManager::createSession(
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

std::unique_ptr<OathDevice> OathDeviceManager::createDevice(
    Shared::DeviceBrand brand,
    const QString &deviceId,
    const QString &readerName,
    SCARDHANDLE cardHandle,
    DWORD protocol,
    const QByteArray &challenge,
    bool requiresPassword)
{
    using namespace YubiKeyOath::Shared;

    std::unique_ptr<OathDevice> device;
    switch (brand) {
    case DeviceBrand::Nitrokey:
        device = std::make_unique<NitrokeyOathDevice>(
            deviceId, readerName, cardHandle, protocol,
            challenge, requiresPassword, m_context, this);
        break;

    case DeviceBrand::YubiKey:
    case DeviceBrand::Unknown:
    default:
        device = std::make_unique<YubiKeyOathDevice>(
            deviceId, readerName, cardHandle, protocol,
            challenge, requiresPassword, m_context, this);
        break;
    }

    // Apply configuration to newly created device
    if (device && m_config) {
        const int rateLimitMs = m_config->pcscRateLimitMs();
        if (rateLimitMs > 0) {
            qCDebug(OathDeviceManagerLog) << "Setting session rate limit to" << rateLimitMs << "ms for device" << deviceId;
        }
        device->setSessionRateLimitMs(rateLimitMs);
    }

    return device;
}

void OathDeviceManager::enumerateAndConnectDevicesAsync()
{
    qCDebug(OathDeviceManagerLog) << "=== enumerateAndConnectDevicesAsync() START ===";

    if (!m_initialized) {
        qCWarning(OathDeviceManagerLog) << "Cannot enumerate devices - manager not initialized";
        return;
    }

    qCDebug(OathDeviceManagerLog) << "Checking for existing PC/SC readers";
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

            qCDebug(OathDeviceManagerLog) << "Found" << readers.size() << "readers:" << readers;

            // Connect to each reader asynchronously
            for (const QString &reader : readers) {
                qCDebug(OathDeviceManagerLog) << "Scheduling async connection to reader:" << reader;
                connectToDeviceAsync(reader);
            }
        } else {
            qCWarning(OathDeviceManagerLog) << "SCardListReaders failed:" << QString::number(startupResult, 16);
        }
    } else if (startupResult == SCARD_E_NO_READERS_AVAILABLE) {
        qCDebug(OathDeviceManagerLog) << "No PC/SC readers available";
    } else {
        qCWarning(OathDeviceManagerLog) << "SCardListReaders failed:" << QString::number(startupResult, 16);
    }

    qCDebug(OathDeviceManagerLog) << "=== enumerateAndConnectDevicesAsync() END ===";
}

void OathDeviceManager::connectToDeviceAsync(const QString &readerName)
{
    qCDebug(OathDeviceManagerLog) << "connectToDeviceAsync() - scheduling async connection to" << readerName;

    // Use PcscWorkerPool to execute connection asynchronously
    // Note: We need to capture 'this' and 'readerName' for the operation
    // The operation will run on a worker thread and emit signals back to main thread

    using namespace YubiKeyOath::Shared;

    // Submit to worker pool with Normal priority (startup initialization)
    PcscWorkerPool::instance().submit(
        readerName,  // Use reader name as device ID for rate limiting
        [this, readerName]() {
            // This lambda runs on worker thread - PC/SC operations safe here
            qCDebug(OathDeviceManagerLog) << "[Worker] Connecting to device on reader:" << readerName;

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
                    qCDebug(OathDeviceManagerLog) << "Async connection succeeded for device" << deviceId;
                    // deviceConnected signal already emitted by connectToDevice()
                    // Emit Ready state
                    Q_EMIT deviceStateChanged(deviceId, DeviceState::Ready);
                } else {
                    qCDebug(OathDeviceManagerLog) << "Async connection failed for reader" << readerName;
                    // Emit Error state with reader name (no device ID available)
                    Q_EMIT deviceStateChanged(readerName, DeviceState::Error);
                }
            }, Qt::QueuedConnection);
        },
        PcscOperationPriority::Normal
    );

    qCDebug(OathDeviceManagerLog) << "connectToDeviceAsync() - task queued for" << readerName;
}

void OathDeviceManager::handlePcscServiceLost()
{
    qCCritical(OathDeviceManagerLog) << "PC/SC service lost (pcscd restart detected) - recreating context";

    // Step 1: Stop monitoring
    qCDebug(OathDeviceManagerLog) << "Step 1/6: Stopping card reader monitor";
    m_readerMonitor->stopMonitoring();

    // Step 2: Disconnect all devices (card handles become invalid after pcscd restart)
    {
        const QMutexLocker locker(&m_devicesMutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks
        qCDebug(OathDeviceManagerLog) << "Step 2/6: Disconnecting" << m_devices.size() << "devices (invalid handles)";

        for (auto &device : m_devices) {
            const QString &deviceId = device.first;
            qCDebug(OathDeviceManagerLog) << "Disconnecting device:" << deviceId;
            device.second->disconnect();
        }

        m_devices.clear();
        m_readerToDeviceMap.clear();
        qCDebug(OathDeviceManagerLog) << "All devices disconnected and cleared from memory";
    }

    // Step 3: Release old PC/SC context
    if (m_context) {
        qCDebug(OathDeviceManagerLog) << "Step 3/6: Releasing old PC/SC context";
        const LONG result = SCardReleaseContext(m_context);
        if (result != SCARD_S_SUCCESS) {
            qCWarning(OathDeviceManagerLog) << "SCardReleaseContext failed:"
                                               << QStringLiteral("0x%1").arg(result, 0, 16)
                                               << "(continuing anyway)";
        }
        m_context = 0;
    }

    // Step 4: Wait for pcscd stabilization (PC/SC service needs time to fully restart)
    qCDebug(OathDeviceManagerLog) << "Step 4/6: Waiting 500ms for pcscd stabilization";
    QThread::msleep(500);

    // Step 5: Re-establish PC/SC context
    qCDebug(OathDeviceManagerLog) << "Step 5/6: Re-establishing PC/SC context";
    const LONG result = SCardEstablishContext(SCARD_SCOPE_SYSTEM, nullptr, nullptr, &m_context);

    if (result != SCARD_S_SUCCESS) {
        qCCritical(OathDeviceManagerLog) << "Failed to re-establish PC/SC context:"
                                            << QStringLiteral("0x%1").arg(result, 0, 16);
        const QString error = tr("Failed to re-establish PC/SC context after pcscd restart: %1")
                                  .arg(QStringLiteral("0x%1").arg(result, 0, 16));
        Q_EMIT errorOccurred(error);
        return;
    }

    qCInfo(OathDeviceManagerLog) << "PC/SC context re-established successfully";

    // Step 6: Reset monitor state and restart monitoring
    qCDebug(OathDeviceManagerLog) << "Step 6/6: Resetting monitor state and restarting monitoring";
    m_readerMonitor->resetPcscServiceState();
    m_readerMonitor->startMonitoring(m_context);

    qCInfo(OathDeviceManagerLog) << "PC/SC service recovery completed - monitoring restarted";

    // Re-enumerate devices after PC/SC recovery
    // Cannot rely on reader change events - if YubiKey was inserted the whole time,
    // no insertion event will fire. Must actively scan for existing readers.
    qCDebug(OathDeviceManagerLog) << "Scheduling async device re-enumeration after PC/SC recovery";
    QTimer::singleShot(0, this, &OathDeviceManager::enumerateAndConnectDevicesAsync);
}

} // namespace Daemon
} // namespace YubiKeyOath
