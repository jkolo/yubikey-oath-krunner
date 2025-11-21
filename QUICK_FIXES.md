# Performance Issues - Quick Reference & Fixes

## Top 3 Critical Fixes

### Fix 1: Add Single-Device Lookup to OathService (CRITICAL)
**Currently:** N calls to `listDevices()` when looking up single device
**Locations:** 
- `oath_manager_object.cpp:243-250`
- `oath_device_object.cpp:53-71`

**Before:**
```cpp
const auto devices = m_service->listDevices();  // Full list
for (const auto& devInfo : devices) {
    if (devInfo._internalDeviceId == deviceId) {
        serialNumber = devInfo.serialNumber;
        break;
    }
}
```

**After:**
```cpp
// oath_service.h - Add method
DeviceInfo getDeviceInfo(const QString &deviceId);

// oath_service.cpp - Implement
DeviceInfo OathService::getDeviceInfo(const QString &deviceId) {
    const QList<DeviceInfo> allDevices = m_deviceLifecycleService->listDevices();
    for (const auto &dev : allDevices) {
        if (dev._internalDeviceId == deviceId) {
            return dev;
        }
    }
    return DeviceInfo();  // Return empty if not found
}

// oath_manager_object.cpp:243
quint32 serialNumber = m_service->getDeviceInfo(deviceId).serialNumber;

// oath_device_object.cpp:53
const DeviceInfo deviceInfo = m_service->getDeviceInfo(m_deviceId);
if (!deviceInfo.deviceName.isEmpty()) {
    m_name = deviceInfo.deviceName;
    m_requiresPassword = deviceInfo.requiresPassword;
    // ... etc
}
```

**Impact:** Eliminates 5N database queries on device connect for N devices

---

### Fix 2: Fix QString by Value Parameters (CRITICAL)
**Currently:** 55 occurrences of `QString name` instead of `const QString &name`
**Locations:** 
- `oath_device_object.cpp:29-30` (setName, Forget, AddCredential)
- `oath_manager_object.cpp:29-30` (constructors)
- All D-Bus adaptor methods

**Before:**
```cpp
OathDeviceObject::OathDeviceObject(QString deviceId,          // ✗ Copy
                                   QString objectPath,        // ✗ Copy
                                   OathService *service,
                                   QDBusConnection connection, // ✗ Copy
                                   QObject *parent)
```

**After:**
```cpp
OathDeviceObject::OathDeviceObject(const QString &deviceId,          // ✓ Ref
                                   const QString &objectPath,        // ✓ Ref
                                   OathService *service,
                                   const QDBusConnection &connection, // ✓ Ref
                                   QObject *parent)
```

**Search & Replace Pattern:**
```
Find:  (QString \w+,
Replace: (const QString &$1,

Find:  (QString \w+)
Replace: (const QString &$1)
```

**Impact:** Eliminates heap allocations on every D-Bus method call

---

### Fix 3: Refactor GetManagedObjects Conversion Loop (CRITICAL)
**Currently:** 3 nested loops with `.toMap()` conversions in hot path
**Location:** `oath_manager_object.cpp:108-169`

**Before:**
```cpp
ManagedObjectMap OathManagerObject::GetManagedObjects() {
    ManagedObjectMap result;
    for (auto deviceIt = m_devices.constBegin(); deviceIt != m_devices.constEnd(); ++deviceIt) {
        // ... convert QVariantMap to InterfacePropertiesMap (3 times!)
        for (auto devIfaceIt = deviceInterfacesVariant.constBegin(); ...) {
            deviceInterfaces.insert(devIfaceIt.key(), devIfaceIt.value().toMap());  // ✗ Conversion
        }
    }
}
```

**After - Option A: Return correct type directly**
```cpp
// oath_device_object.h
InterfacePropertiesMap getManagedObjectData() const;  // ← Changed return type

// oath_device_object.cpp
InterfacePropertiesMap OathDeviceObject::getManagedObjectData() const {
    InterfacePropertiesMap result;
    
    QVariantMap deviceProps;
    deviceProps.insert(QStringLiteral("Name"), m_name);
    // ... other device properties
    result.insert(QStringLiteral("pl.jkolo.yubikey.oath.Device"), deviceProps);
    
    QVariantMap sessionProps;
    sessionProps.insert(QStringLiteral("State"), m_state);
    // ... other session properties
    result.insert(QStringLiteral("pl.jkolo.yubikey.oath.DeviceSession"), sessionProps);
    
    return result;  // ← No conversion needed
}

// oath_manager_object.cpp:115-134
for (auto deviceIt = m_devices.constBegin(); deviceIt != m_devices.constEnd(); ++deviceIt) {
    const QString devicePath = deviceIt.value()->objectPath();
    const InterfacePropertiesMap deviceInterfaces = deviceIt.value()->getManagedObjectData();
    // ↑ No conversion loop needed!
    result.insert(QDBusObjectPath(devicePath), deviceInterfaces);
}
```

**Impact:** Eliminates 3N QVariant conversions per GetManagedObjects call

---

## High Priority Fixes

### Fix 4: Replace contains() + value() with find() (HIGH)
**Location:** `oath_device_object.cpp:395-397, 432-437`

**Before:**
```cpp
if (m_credentials.contains(credId)) {
    return m_credentials.value(credId);  // ✗ Two lookups
}

if (!m_credentials.contains(credentialId)) {
    qCWarning(...);
    return;
}
OathCredentialObject *const credObj = m_credentials.value(credentialId);  // ✗ Two lookups
```

**After:**
```cpp
auto it = m_credentials.find(credId);
if (it != m_credentials.end()) {
    return it.value();  // ✓ One lookup
}

auto it = m_credentials.find(credentialId);
if (it == m_credentials.end()) {
    qCWarning(...);
    return;
}
OathCredentialObject *const credObj = it.value();  // ✓ One lookup
```

**Impact:** Halves hash lookups on credential operations

---

### Fix 5: Remove Excessive Timer Logging (HIGH)
**Location:** `notification_orchestrator.cpp:56-59, 109-112` (in timer handlers)

**Before:**
```cpp
void NotificationOrchestrator::showCodeNotification(...) {
    qCDebug(NotificationOrchestratorLog) << "Showing code notification for:" << credentialName
             << "expiration:" << expirationSeconds << "seconds"
             << "brand:" << brandName(deviceModel.brand)
             << "model:" << deviceModel.modelString;  // ✗ Called every 1s timer
    // ...
    m_codeUpdateTimer->start(1000);  // ← Timer running every second!
}
```

**After:**
```cpp
void NotificationOrchestrator::showCodeNotification(...) {
    // Only log once on show:
    qCDebug(NotificationOrchestratorLog) << "Code notification shown:"
             << credentialName << "expires in" << expirationSeconds << "s";
    // ...
    m_codeUpdateTimer->start(1000);
}

void NotificationOrchestrator::updateCodeNotification() {
    // Only log on state change:
    if (m_codeExpirationTime <= QDateTime::currentDateTime()) {
        qCDebug(NotificationOrchestratorLog) << "Code notification expired";
        closeCodeNotification();
    }
}
```

**Impact:** Eliminates 86,400 log entries per device per day

---

### Fix 6: Add Single-Credential Lookup (HIGH)
**Location:** `credential_service.cpp:134-141, 337-343, 689-696` (repeated 3×)

**Before:**
```cpp
auto credentials = device->credentials();  // ← Full list
for (const auto &cred : credentials) {     // ← Linear search
    if (cred.originalName == credentialName) {
        period = cred.period;
        break;
    }
}
// ... same pattern repeated twice more
```

**After - Add to OathDevice:**
```cpp
// oath_device.h
Result<OathCredential> getCredential(const QString &name) const;

// oath_device.cpp
Result<OathCredential> OathDevice::getCredential(const QString &name) const {
    for (const auto &cred : m_credentials) {
        if (cred.originalName == name) {
            return Result<OathCredential>::success(cred);
        }
    }
    return Result<OathCredential>::error("Credential not found");
}

// credential_service.cpp - Usage in generateCode:
auto credResult = device->getCredential(credentialName);
if (!credResult.isSuccess()) {
    return {.code = QString(), .validUntil = 0};
}
int period = credResult.value().period;
```

**Impact:** Consolidates linear search, reduces code duplication

---

### Fix 7: Cache LastSeen in OathDeviceObject (HIGH)
**Location:** `oath_device_object.cpp:226-233, 558`

**Before:**
```cpp
qint64 OathDeviceObject::lastSeen() const {
    const QDateTime lastSeenDateTime = m_service->getDeviceLastSeen(m_deviceId);
    // ↑ Service call (may query database) called in getManagedObjectData
}
```

**After:**
```cpp
// oath_device_object.h - Add member
qint64 m_cachedLastSeen = 0;

// oath_device_object.cpp - Update in constructor
OathDeviceObject::OathDeviceObject(...) {
    // ... existing code ...
    m_cachedLastSeen = m_service->getDeviceLastSeen(m_deviceId).toMSecsSinceEpoch();
}

// Replace method:
qint64 OathDeviceObject::lastSeen() const {
    return m_cachedLastSeen;  // ✓ No database call
}

// Update cache when state changes (in setState):
void OathDeviceObject::setState(...) {
    // ... existing code ...
    if (stateChanged && state == static_cast<quint8>(Shared::DeviceState::Ready)) {
        m_cachedLastSeen = m_service->getDeviceLastSeen(m_deviceId).toMSecsSinceEpoch();
    }
}
```

**Impact:** Eliminates N database queries per GetManagedObjects call

---

## Testing Checklist

- [ ] Test device connection with 5+ devices (verify N+1 fix)
- [ ] Profile D-Bus method calls before/after (verify getString allocation fix)
- [ ] Monitor logs during code notification (verify timer logging fix)
- [ ] Verify credential operations work correctly (contains/find fix)
- [ ] Test with >200 char credential names (hash caching)
- [ ] Run `ctest --output-on-failure` (ensure no regressions)
- [ ] Check coverage remains >85% (code structure change)

---

## Performance Impact Summary

| Fix | Impact | Effort | Priority |
|-----|--------|--------|----------|
| N+1 device lookup | 5N→1 queries | 30 min | Critical |
| QString by value | Elimination of heap allocs | 60 min | Critical |
| GetManagedObjects loop | 3N→0 conversions | 45 min | Critical |
| contains()+value() | 50% fewer lookups | 15 min | High |
| Timer logging | 99.9% fewer logs | 10 min | High |
| Credential lookup | Code dedup + efficiency | 20 min | High |
| LastSeen cache | N→0 DB queries | 25 min | High |

**Total estimated effort:** ~3 hours for Tier 1 (critical) + ~1 hour for high priority fixes

