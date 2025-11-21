# Daemon Improvement Plan
## Generated: 2025-11-21

This document outlines the comprehensive improvement plan for the YubiKey OATH daemon based on code review findings.

---

## Executive Summary

**Review Scope:** Complete daemon codebase (~15,000 lines)
**Issues Found:** 140+ items across 7 categories
**Overall Code Quality:** B+ (Good, with improvement opportunities)
**Estimated Effort:** 16-20 hours total

### Review Scores:
- ✅ Architecture: 9/10 (Excellent)
- ✅ Security: 8/10 (Strong)
- ⚠️ SOLID/OOP: 7/10 (Good, needs refinement)
- ⚠️ Translations: 5/10 (60 missing i18n calls)
- ⚠️ Performance: 7/10 (12 issues found)
- ✅ Code Duplication: 8/10 (143 lines, 6 patterns)
- ⚠️ Documentation: 6/10 (Outdated naming, inaccurate line counts)

---

## Phase 1: Critical Fixes (Must Do) - 8 hours

### 1.1 Fix Translations (Priority: CRITICAL)
**Effort:** 2 hours
**Files:** 6 files, 60 changes

Replace Qt's `tr()` with KDE's `i18n()` for all user-facing strings:

| File | Issues | Lines to Change |
|------|--------|-----------------|
| `yk_oath_session.cpp` | 41 tr() calls | 147, 159, 229, 239, 278, 288, 300, 319, 353, 395, 434, 471, 475, 494, 504, 519, 524, 529, 534, 545, 552, 562, 577, 582, 587, 592, 602, 607, 617, 630, 652, 662, 674, 677, 679, 692, 715, 730, 734, 749 |
| `nitrokey_oath_session.cpp` | 10 tr() calls | 44, 54, 64, 70, 95, 108, 114, 129, 139, 163 |
| `oath_service.cpp` | 6 tr() calls | 262, 263, 294, 295, 349, 352 |
| `oath_device_manager.cpp` | 2 tr() calls | 82, 879 |
| `oath_credential_object.cpp` | 2 QStringLiteral | 205, 225 |

**Steps:**
1. Add `#include <KLocalizedString>` to files using tr()
2. Replace `tr("text")` → `i18n("text")`
3. Replace `tr("text %1")` → `i18n("text %1")`
4. Verify no QT_NO_CAST_FROM_ASCII violations
5. Test with Polish locale: `LANG=pl_PL.UTF-8`

**Validation:**
```bash
grep -r 'tr("' src/daemon/oath/*.cpp src/daemon/services/*.cpp
# Should return 0 results in user-facing code
```

---

### 1.2 Fix Code Duplication (Priority: HIGH)
**Effort:** 3 hours
**Lines Eliminated:** ~143 lines

#### Issue #1: CardTransaction Validation (6 instances, 30 lines)
**File:** `src/daemon/oath/oath_device.cpp`
**Lines:** 185-189, 222-226, 246-250, 283-287, 319-323, 444-448

**Solution:** Extract private helper method:
```cpp
// In oath_device.h (private section):
Result<void> validateCardTransaction(const CardTransaction& transaction);

// In oath_device.cpp:
Result<void> OathDevice::validateCardTransaction(const CardTransaction& transaction) {
    if (!transaction.isValid()) {
        qCWarning(YubiKeyOathDeviceLog) << "Transaction failed:" << transaction.errorMessage();
        return Result<void>::error(transaction.errorMessage());
    }
    return Result<void>::success();
}

// Usage (replace 6 blocks with):
const CardTransaction transaction(m_cardHandle, m_session.get());
auto validation = validateCardTransaction(transaction);
if (validation.isError()) {
    return Result<T>::error(validation.error());
}
```

#### Issue #2: Password Authentication (3 instances, 24 lines)
**File:** `src/daemon/oath/oath_device.cpp`
**Lines:** 192-199, 253-260, 290-297

**Solution:** Extract private helper method:
```cpp
// In oath_device.h (private section):
Result<void> authenticateIfNeeded();

// In oath_device.cpp:
Result<void> OathDevice::authenticateIfNeeded() {
    if (m_password.isEmpty()) {
        return Result<void>::success();
    }

    qCDebug(YubiKeyOathDeviceLog) << "Authenticating within transaction";
    auto authResult = m_session->authenticate(m_password, m_deviceId);
    if (authResult.isError()) {
        qCWarning(YubiKeyOathDeviceLog) << "Authentication failed:" << authResult.error();
        return Result<void>::error(i18n("Authentication failed"));
    }
    return Result<void>::success();
}

// Usage (replace 3 blocks with):
auto authResult = authenticateIfNeeded();
if (authResult.isError()) {
    return Result<T>::error(authResult.error());
}
```

#### Issue #3: D-Bus Registration (3 classes, 82 lines)
**Files:** `oath_manager_object.cpp`, `oath_device_object.cpp`, `oath_credential_object.cpp`

**Solution:** Create base class template (new file):
```cpp
// src/daemon/dbus/dbus_object_base.h
template <typename LogCategory>
class DBusObjectBase {
protected:
    DBusObjectBase(const QDBusConnection& connection, const QString& objectPath)
        : m_connection(connection), m_objectPath(objectPath), m_registered(false) {}

    bool registerObject(QObject* obj) {
        if (m_registered) {
            qCWarning(LogCategory()) << "Already registered:" << m_objectPath;
            return true;
        }

        if (!m_connection.registerObject(m_objectPath, obj,
                                        QDBusConnection::ExportAdaptors)) {
            qCCritical(LogCategory()) << "Failed to register:" << m_objectPath;
            return false;
        }

        m_registered = true;
        qCInfo(LogCategory()) << "Registered successfully:" << m_objectPath;
        return true;
    }

    void unregisterObject() {
        if (!m_registered) {
            return;
        }
        m_connection.unregisterObject(m_objectPath);
        m_registered = false;
        qCDebug(LogCategory()) << "Unregistered:" << m_objectPath;
    }

private:
    QDBusConnection m_connection;
    QString m_objectPath;
    bool m_registered;
};
```

#### Issue #4: Notification Availability (7 instances, 15 lines)
**File:** `src/daemon/workflows/notification_orchestrator.cpp`
**Lines:** 52, 105, 182, 211, 251, 309, 331

**Solution:** Extract private helper:
```cpp
// In notification_orchestrator.h (private section):
bool shouldShowNotifications() const;

// In notification_orchestrator.cpp:
bool NotificationOrchestrator::shouldShowNotifications() const {
    return m_config->showNotifications()
        && m_notificationManager
        && m_notificationManager->isAvailable();
}

// Usage (replace 7 guards with):
if (!shouldShowNotifications()) {
    return;
}
```

**Files to Modify:**
- `src/daemon/oath/oath_device.{h,cpp}`
- `src/daemon/dbus/dbus_object_base.h` (new)
- `src/daemon/dbus/oath_manager_object.{h,cpp}`
- `src/daemon/dbus/oath_device_object.{h,cpp}`
- `src/daemon/dbus/oath_credential_object.{h,cpp}`
- `src/daemon/workflows/notification_orchestrator.{h,cpp}`

---

### 1.3 Fix Logging Categories (Priority: HIGH)
**Effort:** 30 minutes
**Files:** 5 files, 11 changes

Replace raw `qWarning()`/`qDebug()` with categorized logging:

| File | Lines | Change |
|------|-------|--------|
| `oath_device_manager.cpp` | 262, 265, 268 | `qWarning()` → `qCWarning(OathDaemonLog)` |
| `oath_service.cpp` | 209 | `qWarning()` → `qCWarning(OathDaemonLog)` |
| `main.cpp` | 35-37 | `qWarning()` → `qCWarning(OathDaemonLog)` |
| `daemon_configuration.cpp` | 28, 104 | `qDebug()` → `qCDebug(OathDaemonLog)` |
| `oath_protocol.cpp` | 162, 635 | `qWarning()` → `qCWarning(OathProtocolLog)` |

**Validation:**
```bash
grep -rn 'qWarning()' src/daemon/ | grep -v 'qCWarning'
grep -rn 'qDebug()' src/daemon/ | grep -v 'qCDebug'
# Should return 0 results
```

---

### 1.4 Add [[nodiscard]] Attributes (Priority: HIGH)
**Effort:** 1 hour
**Impact:** Compile-time error detection for ignored Result<T> returns

**Files to modify:**
- `src/daemon/oath/oath_device.h` - 13 methods
- `src/daemon/oath/oath_device_manager.h` - 8 methods
- `src/daemon/services/credential_service.h` - 12 methods
- `src/daemon/storage/oath_database.h` - 26 methods

**Pattern:**
```cpp
// Before:
Result<QString> generateCode(const QString& name, int period);

// After:
[[nodiscard]] Result<QString> generateCode(const QString& name, int period);
```

**Validation:**
```bash
# Intentionally ignore a Result<T> return - should fail compilation
Result<QString> foo = device->generateCode("test", 30);
device->generateCode("test", 30);  // Should error: ignoring [[nodiscard]] return
```

---

### 1.5 Fix Critical Performance Issues (Priority: HIGH)
**Effort:** 1.5 hours

#### Issue #1: N+1 Device Lookup
**File:** `src/daemon/dbus/oath_manager_object.cpp:243`

**Before:**
```cpp
for (const auto& deviceId : deviceIds) {
    auto device = m_oathService->getDevice(deviceId);  // O(n) lookup
}
```

**After:**
```cpp
const auto allDevices = m_oathService->getAllDevices();  // Single call
QMap<QString, OathDevice*> deviceMap;
for (auto* device : allDevices) {
    deviceMap.insert(device->id(), device);
}
for (const auto& deviceId : deviceIds) {
    auto device = deviceMap.value(deviceId);  // O(1) lookup
}
```

#### Issue #2: QString by Value (55 occurrences)
**Files:** Multiple files

**Pattern:**
```cpp
// Before:
void setName(QString name);
QString formatCode(QString code);

// After:
void setName(const QString& name);
QString formatCode(const QString& code);
```

**Validation:**
```bash
# Find remaining by-value QString parameters
grep -rn 'void.*QString [^&]' src/daemon/ | grep -v 'const QString&'
```

---

## Phase 2: Important Improvements (Should Do) - 6 hours

### 2.1 SOLID Principle Fixes

#### Issue #1: Extract Strategy Pattern for Device Info Retrieval
**File:** `src/daemon/oath/yk_oath_session.cpp:766-1077` (312 lines)

**Current:** Single method with 4 serial retrieval strategies
**Goal:** Extract `DeviceInfoStrategy` interface with 4 implementations

**New files:**
- `src/daemon/oath/device_info_strategy.h` (interface)
- `src/daemon/oath/serial_from_select_strategy.cpp`
- `src/daemon/oath/serial_from_management_strategy.cpp`
- `src/daemon/oath/serial_from_piv_strategy.cpp`
- `src/daemon/oath/serial_from_recovery_strategy.cpp`

**Effort:** 3 hours (complex refactoring)

#### Issue #2: Segregate OathProtocol Interface
**File:** `src/daemon/oath/oath_protocol.{h,cpp}`

**Current:** 76 static methods + 2 virtual methods (fat interface)
**Goal:** Split into:
- `OathCommandBuilder` - static utilities (command construction)
- `OathProtocolParser` - virtual methods (brand-specific parsing)

**Effort:** 2 hours

#### Issue #3: Extract PortalTextInput Session Management
**File:** `src/daemon/input/portal_text_input.cpp` (579 lines)

**Goal:** Extract `PortalRemoteDesktopSession` class
**Effort:** 1 hour

---

### 2.2 Database Split (OathDatabase → Repository + Cache)
**File:** `src/daemon/storage/oath_database.{h,cpp}` (881 lines, 26 methods)

**Current:** Single class handling device metadata CRUD + credential caching + schema migration
**Goal:** Split into:
- `OathDeviceRepository` - device metadata CRUD
- `OathCredentialCache` - credential caching
- Keep migration in `oath_database.cpp` as factory

**Effort:** 2 hours (requires test updates)

---

## Phase 3: Documentation Updates - 2 hours

### 3.1 Update CLAUDE.md with Correct Class Names
**Effort:** 1 hour

Global replacements (30+ instances):
- `YubiKeyRunner` → `OathRunner`
- `YubiKeyService` → `OathService`
- `YubiKeyDBusService` → `OathDBusService`
- `YubiKeyDatabase` → `OathDatabase`
- `YubiKeyDeviceManager` → `OathDeviceManager`
- `YubiKeyConfig` → `OathConfig`
- `YubiKeyDeviceModel` → `OathDeviceListModel`
- `YubiKeyConfigIconResolver` → `OathConfigIconResolver`
- `YubiKeyActionCoordinator` → `OathActionCoordinator`

Update file path references:
- `src/krunner/yubikeyrunner.{h,cpp}` → `oathrunner.{h,cpp}`
- `src/daemon/oath/yubikey_device_manager.{h,cpp}` → `oath_device_manager.{h,cpp}`
- `src/daemon/storage/yubikey_database.{h,cpp}` → `oath_database.{h,cpp}`
- `src/config/yubikey_config.{h,cpp}` → `oath_config.{h,cpp}`

### 3.2 Recalculate Line Counts
**Effort:** 30 minutes

Script to generate accurate line counts:
```bash
#!/bin/bash
for file in src/daemon/oath/*.{h,cpp}; do
    if [ -f "$file" ]; then
        base=$(basename "$file" | sed 's/\..*//')
        h_lines=$(wc -l "src/daemon/oath/${base}.h" 2>/dev/null | awk '{print $1}')
        cpp_lines=$(wc -l "src/daemon/oath/${base}.cpp" 2>/dev/null | awk '{print $1}')
        total=$((h_lines + cpp_lines))
        echo "- **${base}**: ~${total} lines (${h_lines}h + ${cpp_lines}cpp)"
    fi
done
```

### 3.3 Add Missing Sections
**Effort:** 30 minutes

Document:
- Recent refactoring (commit 89d50cb)
- Pending TODOs in `oath_credential_object.cpp`
- Config module details (`OathConfig`, `OathDeviceListModel`)

---

## Phase 4: Optional Enhancements (Nice to Have) - 4 hours

### 4.1 Additional Performance Optimizations
- Fix double hash lookups (5 instances)
- Reduce excessive timer logging (86,400 entries/day → 288 with sampling)
- Cache repeated credential lookups
- Optimize database queries in hot paths

**Effort:** 2 hours

### 4.2 Protected Member Refactoring
**File:** `src/daemon/oath/oath_device.h`

Replace 16 protected members with protected getters/setters to prevent Liskov Substitution Principle violations.

**Effort:** 2 hours

---

## Implementation Order

### Week 1 (Critical - 8 hours)
1. **Day 1-2:** Fix translations (2h) + Fix logging (0.5h) = 2.5 hours
2. **Day 3:** Fix code duplication = 3 hours
3. **Day 4:** Add [[nodiscard]] (1h) + Critical performance (1.5h) = 2.5 hours

### Week 2 (Important - 6 hours)
4. **Day 5-6:** SOLID fixes = 6 hours

### Week 3 (Documentation - 2 hours)
5. **Day 7:** Update CLAUDE.md = 2 hours

---

## Success Criteria

### Phase 1 Completion:
- ✅ All 60 `tr()` calls replaced with `i18n()`
- ✅ 143 lines of duplicated code eliminated
- ✅ All 11 uncategorized logging calls fixed
- ✅ [[nodiscard]] added to 59+ Result<T> methods
- ✅ N+1 lookup fixed, QString by-value eliminated

### Phase 2 Completion:
- ✅ Strategy Pattern implemented for device info
- ✅ OathProtocol interface segregated
- ✅ Database split into repository + cache

### Phase 3 Completion:
- ✅ CLAUDE.md has accurate class names (0 YubiKey* references)
- ✅ CLAUDE.md has accurate line counts (±10% tolerance)
- ✅ All sections reference correct file paths

### Testing:
- ✅ All 34 tests pass (33/34 currently passing)
- ✅ Code coverage remains ≥85%
- ✅ clang-tidy passes with zero errors
- ✅ Application builds without warnings

---

## Risk Assessment

### Low Risk (Safe):
- Translation fixes (Phase 1.1)
- Logging category fixes (Phase 1.3)
- [[nodiscard]] additions (Phase 1.4)
- Documentation updates (Phase 3)

### Medium Risk (Test carefully):
- Code duplication fixes (Phase 1.2) - changes 6 files
- Performance fixes (Phase 1.5) - algorithmic changes
- SOLID refactoring (Phase 2.1) - large structural changes

### High Risk (Defer if time-constrained):
- Database split (Phase 2.2) - affects storage layer, needs test updates

---

## Rollback Plan

For each phase:
1. Create feature branch: `fix/phase-N-description`
2. Commit atomically after each file change
3. Run full test suite: `ctest --output-on-failure`
4. If tests fail, revert last commit: `git revert HEAD`
5. Merge to main only after all tests pass

---

## Conclusion

This plan addresses all major code quality issues found in the comprehensive review while maintaining the daemon's excellent architectural foundation. The phased approach allows for incremental improvements with clear validation at each step.

**Total Estimated Effort:** 16-20 hours
**Priority:** Focus on Phase 1 (critical fixes) for immediate impact
**Timeline:** 1-3 weeks depending on available development time
