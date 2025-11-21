# Performance Review: src/daemon/

## Executive Summary
Found 12 significant performance issues affecting:
- D-Bus method handlers (GetManagedObjects, addDeviceWithStatus)
- OATH protocol operations (credential iteration, device lookups)
- Credential list operations (repeated calls in loops)
- Notification updates (excessive logging on timers)

**Critical Issues:** 3 (N+1 patterns, unnecessary copies)
**High Priority:** 5 (hot path logging, inefficient conversions)
**Medium Priority:** 4 (contains+value pattern, redundant calls)

---

## CRITICAL ISSUES

### Issue 1: N+1 Query Pattern - Device List Lookup
**File:** `src/daemon/dbus/oath_manager_object.cpp`
**Lines:** 243, 54-71 (similar in oath_device_object.cpp)
**Severity:** CRITICAL - Called on D-Bus method

```cpp
// oath_manager_object.cpp:243-250
const auto devices = m_service->listDevices();  // Full list query
for (const auto& devInfo : devices) {
    if (devInfo._internalDeviceId == deviceId) {
        serialNumber = devInfo.serialNumber;
        break;
    }
}

// oath_device_object.cpp:53-71
const auto devices = m_service->listDevices();  // Full list query
for (const auto &devInfo : devices) {
    if (devInfo._internalDeviceId == m_deviceId) {
        // Extract single device info...
    }
}
```

**Problem:** Called in two hot paths:
1. `addDeviceWithStatus()` - executed when device connects (potentially 10+ devices)
2. `OathDeviceObject` constructor - called for every D-Bus device object

**Impact:** For 5 devices, `listDevices()` is called 5 times; each may query database/service

**Suggestion:** Add single-device lookup method to OathService:
```cpp
// oath_service.h
DeviceInfo getDeviceInfo(const QString &deviceId);

// oath_service.cpp
DeviceInfo OathService::getDeviceInfo(const QString &deviceId) {
    const QList<DeviceInfo> allDevices = m_deviceLifecycleService->listDevices();
    for (const auto &dev : allDevices) {
        if (dev._internalDeviceId == deviceId) return dev;
    }
    return DeviceInfo();
}
```

---

### Issue 2: Unnecessary QString by Value Parameters
**File:** `src/daemon/services/credential_service.cpp`
**Lines:** 154-162, 243-252
**Severity:** CRITICAL - API hot path, 8 QString parameters

```cpp
// credential_service.cpp:154-162
AddCredentialResult CredentialService::addCredential(
    const QString &deviceId,    // ✓ Correct
    const QString &name,        // ✓ Correct
    const QString &secret,      // ✓ Correct
    const QString &type,        // ✓ Correct
    const QString &algorithm,   // ✓ Correct
    int digits,
    int period,
    int counter,
    bool requireTouch)
```

**Also affects:** 55 total occurrences across daemon (see grep results)
- `src/daemon/dbus/oath_device_object.cpp:28-32` (4 QString params by value)
- `src/daemon/dbus/oath_manager_object.cpp:28-31` (3 QString params by value)

**Problem:** QString by value = deep copy allocation. In D-Bus methods, strings are already copied during unmarshaling; second copy is wasteful.

**Impact:** 
- Additional heap allocation per call
- Cache misses on large credential lists
- Affects D-Bus method handlers (frequent calls from KRunner)

---

### Issue 3: Repeated Conversion from QVariant in Loop
**File:** `src/daemon/dbus/oath_manager_object.cpp`
**Lines:** 108-169 (GetManagedObjects method)
**Severity:** CRITICAL - Called frequently by KRunner

```cpp
// Line 123-128: First loop
const QVariantMap deviceInterfacesVariant = deviceObj->getManagedObjectData();
InterfacePropertiesMap deviceInterfaces;
for (auto devIfaceIt = deviceInterfacesVariant.constBegin(); 
     devIfaceIt != deviceInterfacesVariant.constEnd(); ++devIfaceIt) {
    deviceInterfaces.insert(devIfaceIt.key(), devIfaceIt.value().toMap());
    //                                          ^^^^^^^^^ CONVERSION
}

// Line 145-150: Identical loop for credentials
for (auto credIfaceIt = credInterfacesVariant.constBegin(); 
     credIfaceIt != credInterfacesVariant.constEnd(); ++credIfaceIt) {
    credInterfaces.insert(credIfaceIt.key(), credIfaceIt.value().toMap());
    //                                        ^^^^^^^^^ CONVERSION
}
```

**Pattern:** 
- `getManagedObjectData()` returns `QVariantMap` (where values are `QVariant(QVariantMap)`)
- Loop converts back to `QVariantMap` with `.toMap()`
- Happens 3× per GetManagedObjects call (device + credentials in two places)

**Problem:** 
- Unnecessary conversion between types
- Should either return correct type or avoid loop

**Suggestion:** Refactor `getManagedObjectData()` to return `InterfacePropertiesMap`:
```cpp
InterfacePropertiesMap OathDeviceObject::getManagedObjectData() const;
// No loop conversion needed
```

**Impact:** 
- 3N QVariant conversions per GetManagedObjects call (N=device count)
- GetManagedObjects called on every KRunner session (frequent)

---

## HIGH PRIORITY ISSUES

### Issue 4: contains() + value() Double Lookup
**File:** `src/daemon/dbus/oath_device_object.cpp`
**Lines:** 395-397, 432-437
**Severity:** HIGH - Affects credential operations

```cpp
// Line 395-397 (addCredential)
if (m_credentials.contains(credId)) {
    return m_credentials.value(credId);  // Second lookup
}

// Line 432-437 (removeCredential)
if (!m_credentials.contains(credentialId)) {
    qCWarning(...);
    return;
}
OathCredentialObject *const credObj = m_credentials.value(credentialId);
```

**Problem:** Two hash map lookups where one would suffice

**Suggestion:** Use `find()` or `value()` with default:
```cpp
// addCredential:
auto it = m_credentials.find(credId);
if (it != m_credentials.end()) {
    return it.value();
}

// removeCredential:
auto it = m_credentials.find(credentialId);
if (it == m_credentials.end()) {
    qCWarning(...);
    return;
}
OathCredentialObject *const credObj = it.value();
```

---

### Issue 5: Excessive Debug Logging in Timer Updates
**File:** `src/daemon/workflows/notification_orchestrator.cpp`
**Lines:** 56-59, 109-112, 186, 215, 245 (updateCodeNotification, updateTouchNotification timers)
**Severity:** HIGH - Called every 1 second per notification

```cpp
// notification_orchestrator.cpp:56-59 (showCodeNotification)
qCDebug(NotificationOrchestratorLog) << "Showing code notification for:" << credentialName
         << "expiration:" << expirationSeconds << "seconds"
         << "brand:" << brandName(deviceModel.brand)
         << "model:" << deviceModel.modelString;
```

**Problem:** 
- Called every 1000ms timer tick (line 98: `m_codeUpdateTimer->start(1000)`)
- Generates 86,400 log entries per device per day
- Even with debug logging disabled, string formatting isn't free

**Similar Issues:** Lines 245 (closeNotification), 186 (showSimpleNotification)

**Suggestion:** Use `qCDebug` only for entry/exit, not per-update:
```cpp
void NotificationOrchestrator::updateCodeNotification() {
    // ... calculations ...
    // Only log significant state changes:
    if (m_codeExpirationTime <= QDateTime::currentDateTime()) {
        qCDebug(NotificationOrchestratorLog) << "Code notification expired";
        // ... close notification ...
    }
}
```

---

### Issue 6: Repeated Credential List Fetch in Loop
**File:** `src/daemon/services/credential_service.cpp`
**Lines:** 134-141, 337-343, 689-696
**Severity:** HIGH - O(n²) pattern

```cpp
// Line 134-141 (generateCode)
auto credentials = device->credentials();  // ← Full list
for (const auto &cred : credentials) {     // ← Linear search
    if (cred.originalName == credentialName) {
        period = cred.period;
        break;
    }
}

// Same pattern in: generateCodeAsync (line 337), validateCredentialBeforeSave (line 689)
```

**Problem:** Called in three places, repeated for same operation

**Suggest:** Add single-credential lookup to OathDevice:
```cpp
// oath_device.h
Result<OathCredential> getCredential(const QString &name) const;

// oath_device.cpp
Result<OathCredential> OathDevice::getCredential(const QString &name) const {
    for (const auto &cred : m_credentials) {
        if (cred.originalName == name) return Result<OathCredential>::success(cred);
    }
    return Result<OathCredential>::error("Not found");
}
```

**Usage:**
```cpp
auto credResult = device->getCredential(credentialName);
if (credResult.isSuccess()) {
    period = credResult.value().period;
}
```

---

### Issue 7: lastSeen() Called During D-Bus Property Assembly
**File:** `src/daemon/dbus/oath_device_object.cpp`
**Lines:** 226-233, 558 (used in getManagedObjectData)
**Severity:** HIGH - Called in hot path

```cpp
// Line 226-233
qint64 OathDeviceObject::lastSeen() const {
    const QDateTime lastSeenDateTime = m_service->getDeviceLastSeen(m_deviceId);
    // ↑ Service call (may query database)
    if (lastSeenDateTime.isValid()) {
        return lastSeenDateTime.toMSecsSinceEpoch();
    }
    return 0;
}

// Line 558 (called in getManagedObjectData, used in GetManagedObjects)
sessionProps.insert(QLatin1String("LastSeen"), lastSeen());
```

**Problem:** 
- `getDeviceLastSeen()` may query database for each device
- Called via GetManagedObjects (D-Bus hot path)
- For 5 devices = 5 database queries per GetManagedObjects call

**Suggest:** Cache LastSeen value in OathDeviceObject

---

### Issue 8: Credential Name Encoding Hash Calculation
**File:** `src/daemon/dbus/oath_device_object.cpp`
**Lines:** 671-673
**Severity:** MEDIUM - Rare case but CPU-intensive

```cpp
// Line 671-673 (only for credentials > 200 chars)
const QByteArray hash = QCryptographicHash::hash(credentialName.toUtf8(),
                                                  QCryptographicHash::Sha256);
encoded = QString::fromLatin1("cred_") + QString::fromLatin1(hash.toHex().left(16));
```

**Problem:** SHA256 hash on every credential lookup for long names

**Suggest:** Cache result in credential objects or use faster hash

---

## MEDIUM PRIORITY ISSUES

### Issue 9: QSet Construction from QHash Keys
**File:** `src/daemon/dbus/oath_device_object.cpp`
**Lines:** 482-483
**Severity:** MEDIUM

```cpp
// Line 482-483 (updateCredentials)
const QSet<QString> existingCredIds = QSet<QString>(m_credentials.keyBegin(),
                                                     m_credentials.keyEnd());
```

**Problem:** Creates new QSet from iterator range, then converts to QSet operations

**Suggest:** Use QHash directly or convert more efficiently:
```cpp
QSet<QString> existingCredIds = m_credentials.keys().toSet();
```

---

### Issue 10: String Operations in Loops
**File:** `src/daemon/services/credential_service.cpp`
**Lines:** 243-247
**Severity:** MEDIUM

```cpp
// Line 243-247 (addCredential)
if (data.name.contains(QStringLiteral(":"))) {
    QStringList parts = data.name.split(QStringLiteral(":"));
    if (parts.size() >= 2) {
        data.issuer = parts[0];
        data.account = parts.mid(1).join(QStringLiteral(":"));
    }
}
```

**Problem:** `split()` allocates QStringList; `mid().join()` allocates again

**Suggest:** Use indexOf/mid on QString directly to avoid allocations

---

### Issue 11: Database Query in Loop (getAvailableDevices)
**File:** `src/daemon/services/credential_service.cpp`
**Lines:** 645-664
**Severity:** MEDIUM - Loop with potential function calls

```cpp
// Line 650
deviceInfo.deviceName = DeviceNameFormatter::getDeviceDisplayName(record.deviceId, m_database);
```

**Problem:** Called inside loop; `getDeviceDisplayName` may query database

**Suggest:** Batch query device names before loop

---

### Issue 12: Repeated .keys() Call
**File:** `src/daemon/dbus/oath_manager_object.cpp`
**Lines:** 131, 153
**Severity:** MEDIUM

```cpp
// Line 131
qCDebug(OathDaemonLog) << "...interfaces:" << deviceInterfaces.keys();

// Line 153
qCDebug(OathDaemonLog) << "...interfaces:" << credInterfaces.keys();
```

**Problem:** Debug logging calls `.keys()` to create QStringList, then discards it

**Suggest:** Either:
1. Remove debug logging (logging should be performance-neutral)
2. Check debug enabled first: `if (QLoggingCategory::isDebugEnabled())`

---

## SUMMARY TABLE

| Issue | File:Line | Category | Frequency | Impact |
|-------|-----------|----------|-----------|--------|
| 1 | oath_manager_object.cpp:243 | N+1 Query | Per device connect | 5N database queries |
| 2 | credential_service.cpp:154 | API Signature | Per D-Bus call | Heap allocations |
| 3 | oath_manager_object.cpp:123-128 | Hot Loop | Per GetManagedObjects | 3N QVariant conversions |
| 4 | oath_device_object.cpp:395 | Double Lookup | Per credential op | 2 hash lookups |
| 5 | notification_orchestrator.cpp:56 | Excessive Logging | Every 1s timer | 86k logs/day/device |
| 6 | credential_service.cpp:134 | Repeated Call | Per generateCode | Linear search 3× |
| 7 | oath_device_object.cpp:558 | Hot Path DB | Per GetManagedObjects | N database queries |
| 8 | oath_device_object.cpp:671 | Crypto Loop | Rare (>200 char) | SHA256 per lookup |
| 9 | oath_device_object.cpp:482 | Inefficient Type | Per credential update | QSet conversion |
| 10 | credential_service.cpp:243 | String Alloc | Per addCredential | 2 allocations |
| 11 | credential_service.cpp:650 | Loop DB Call | Per device | Potential N queries |
| 12 | oath_manager_object.cpp:131 | Debug Logging | Per GetManagedObjects | Unused .keys() |

---

## RECOMMENDATIONS

### Tier 1 (Implement ASAP)
1. **Add single-device lookup** to OathService (Issues 1, 7)
2. **Fix API signatures** to use const& for QString (Issue 2)
3. **Refactor GetManagedObjects** conversion loop (Issue 3)

### Tier 2 (Next Sprint)
4. Replace contains() + value() with find() (Issue 4)
5. Remove or guard verbose timer logging (Issue 5)
6. Add device.getCredential() method (Issue 6)
7. Cache LastSeen in OathDeviceObject (Issue 7)

### Tier 3 (Future Optimization)
8. Cache credential name encoding (Issue 8)
9. Optimize QSet construction (Issue 9)
10. Optimize string split operations (Issue 10)
11. Batch database queries (Issue 11)
12. Guard debug logging with QLoggingCategory checks (Issue 12)

