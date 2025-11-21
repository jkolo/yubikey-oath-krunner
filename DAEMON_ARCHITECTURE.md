# YubiKey OATH Daemon - Architecture Analysis

**Repository:** `/home/user/yubikey-oath-krunner/src/daemon/`

**Total Components:** 47 classes across 16 layers, ~22,605 lines of code

---

## 1. OVERALL ARCHITECTURE ORGANIZATION

### Directory Structure & Layer Hierarchy

```
src/daemon/
├── [TOP] D-Bus Entry Point
│   ├── main.cpp                          - Application entry point, D-Bus registration
│   └── oath_dbus_service.{h,cpp}        - Thin marshaling layer for D-Bus <-> business logic
│
├── [PRESENTATION] D-Bus Object Hierarchy
│   └── dbus/
│       ├── oath_manager_object.{h,cpp}  - Root object, ObjectManager pattern
│       ├── oath_device_object.{h,cpp}   - Device + DeviceSession interfaces
│       └── oath_credential_object.{h,cpp} - Credential operations
│
├── [BUSINESS LOGIC] Service Layer
│   └── services/
│       ├── oath_service.{h,cpp}                - Aggregate service, D-Bus facade
│       ├── password_service.{h,cpp}            - Password validation & management
│       ├── device_lifecycle_service.{h,cpp}    - Device connection/initialization
│       └── credential_service.{h,cpp}          - Credential operations (async)
│
├── [DOMAIN] OATH Protocol & Device Management
│   └── oath/
│       ├── oath_device.{h,cpp}                 - Abstract OATH device base
│       ├── yubikey_oath_device.{h,cpp}         - YubiKey implementation
│       ├── nitrokey_oath_device.{h,cpp}        - Nitrokey implementation
│       ├── yk_oath_session.{h,cpp}             - YubiKey protocol handler
│       ├── nitrokey_oath_session.{h,cpp}       - Nitrokey protocol handler
│       ├── yk_oath_protocol.{h,cpp}            - YubiKey-specific commands
│       ├── nitrokey_secrets_oath_protocol.{h,cpp} - Nitrokey-specific commands
│       ├── oath_protocol.{h,cpp}               - OATH common utilities & TLV parsing
│       ├── oath_error_codes.h                  - Error code constants (i18n-safe)
│       ├── management_protocol.{h,cpp}         - YubiKey Management API
│       ├── nitrokey_model_detector.{h,cpp}     - Firmware-based device detection
│       └── oath_device_manager.{h,cpp}         - Multi-device management & hot-plug
│
├── [INFRASTRUCTURE] PC/SC & Async
│   ├── pcsc/
│   │   ├── card_transaction.{h,cpp}       - RAII PC/S transaction + SELECT OATH
│   │   ├── i_oath_selector.h              - Interface for OATH selection (DIP)
│   │   └── card_reader_monitor.cpp/h      - Hot-plug detection, PC/SC context
│   │
│   ├── infrastructure/
│   │   └── pcsc_worker_pool.{h,cpp}       - Thread pool for async PC/S operations
│   │
│   ├── storage/
│   │   ├── oath_database.{h,cpp}          - SQLite device/credential persistence
│   │   ├── secret_storage.{h,cpp}         - KWallet password storage
│   │   └── transaction_guard.{h,cpp}      - RAII SQLite transaction guard
│   │
│   └── config/
│       └── daemon_configuration.{h,cpp}   - KConfig settings (auto-clear timeout, etc.)
│
├── [USER INTERACTION] Workflows & Actions
│   ├── workflows/
│   │   ├── touch_workflow_coordinator.{h,cpp}    - Touch-required credential flow
│   │   ├── touch_handler.{h,cpp}                 - Touch detection polling
│   │   ├── reconnect_workflow_coordinator.{h,cpp} - Device reconnection flow
│   │   ├── notification_orchestrator.{h,cpp}     - Notification lifecycle
│   │   ├── notification_helper.{h,cpp}           - Notification utilities
│   │   └── notification_utils.{h,cpp}            - Urgency & formatting
│   │
│   ├── actions/
│   │   ├── oath_action_coordinator.{h,cpp}  - Copy/type/delete coordination
│   │   └── action_executor.{h,cpp}          - Execute actions (copy, type, delete)
│   │
│   ├── clipboard/
│   │   └── clipboard_manager.{h,cpp}        - Secure clipboard with auto-clear
│   │
│   ├── input/
│   │   ├── text_input_factory.{h,cpp}       - Factory for Portal/X11 text input
│   │   ├── text_input_provider.h            - Interface for text input
│   │   ├── portal_text_input.{h,cpp}        - XDG Portal text input (Wayland)
│   │   ├── x11_text_input.{h,cpp}           - X11 text input
│   │   └── modifier_key_checker.{h,cpp}     - Modifier key state detection
│   │
│   ├── notification/
│   │   └── dbus_notification_manager.{h,cpp} - freedesktop notifications
│   │
│   └── ui/
│       ├── add_credential_dialog.{h,cpp}    - OATH credential input dialog
│       └── processing_overlay.{h,cpp}       - Loading overlay animation
│
├── [UTILITIES]
│   ├── utils/
│   │   ├── secure_memory.{h,cpp}            - Secure string wiping
│   │   ├── qr_code_parser.{h,cpp}           - QR code decoding (ZXing)
│   │   ├── otpauth_uri_parser.{h,cpp}       - otpauth:// URI parsing
│   │   ├── screenshot_capturer.{h,cpp}      - Wayland screenshot
│   │   └── async_waiter.{h,cpp}             - QFuture waiters
│   │
│   ├── formatting/
│   │   └── code_validator.{h,cpp}           - OATH code format validation
│   │
│   ├── cache/
│   │   └── credential_cache_searcher.{h,cpp} - Offline device search
│   │
│   └── logging_categories.{h,cpp}           - Qt logging categories
```

---

## 2. KEY CLASSES & RESPONSIBILITIES

### Core Service Interfaces

| Class | Lines | Responsibility | Dependencies |
|-------|-------|-----------------|--------------|
| **OathService** | ~350 | Service aggregate, D-Bus facade | All services, device manager |
| **OathDeviceManager** | ~400 | Multi-device mgmt, hot-plug detection | PC/S, storage, services |
| **OathDevice** (base) | ~550 | Abstract OATH device ops | YkOathSession (polymorphic) |
| **YubiKeyOathDevice** | Derived | YubiKey-specific impl | YubiKeyOathSession |
| **NitrokeyOathDevice** | Derived | Nitrokey-specific impl | NitrokeyOathSession |

### OATH Protocol Layers

| Class | Responsibility |
|-------|-----------------|
| **YkOathSession** | YubiKey OATH protocol handler, implements IOathSelector |
| **NitrokeyOathSession** | Nitrokey OATH protocol handler, implements IOathSelector |
| **OathProtocol** | OATH command utilities, TLV parsing |
| **YkOathProtocol** | YubiKey-specific commands (CALCULATE_ALL 0xA4) |
| **NitrokeySecretsOathProtocol** | Nitrokey-specific commands (LIST 0xA1 + CALCULATE 0xA2) |
| **ManagementProtocol** | YubiKey Management API for device info |

### PC/SC Layer

| Class | Responsibility |
|-------|-----------------|
| **CardTransaction** | RAII: BEGIN_TRANSACTION + SELECT OATH + END_TRANSACTION |
| **IOathSelector** | Interface for OATH selection (Dependency Inversion) |
| **CardReaderMonitor** | PC/S context mgmt, hot-plug detection, service recovery |
| **PcscWorkerPool** | Thread pool, per-device rate limiting (50ms), priority queuing |

### Services Layer

| Class | Responsibility |
|-------|-----------------|
| **PasswordService** | Password validation, PBKDF2 verification |
| **DeviceLifecycleService** | Device connection, initialization, state transitions |
| **CredentialService** | Credential ops, async code generation, caching |
| **OathActionCoordinator** | Coordinates copy/type/delete with touch/reconnect flows |

### Workflow Layer

| Class | Responsibility |
|-------|-----------------|
| **TouchWorkflowCoordinator** | Orchestrates touch workflow (notify → poll → act) |
| **TouchHandler** | Polls for touch completion, timeout handling |
| **ReconnectWorkflowCoordinator** | Handles device disconnection & reconnection |
| **NotificationOrchestrator** | Notification lifecycle, countdown, progress |

### Storage Layer

| Class | Responsibility |
|-------|-----------------|
| **OathDatabase** | SQLite persistence: devices, credentials (CASCADE FK) |
| **SecretStorage** | KWallet per-device passwords, SecureString memory wiping |

### Input/Output Layer

| Class | Responsibility |
|-------|-----------------|
| **TextInputFactory** | Factory creating Portal or X11 text input provider |
| **PortalTextInput** | XDG Portal RemoteDesktop (Wayland) text input |
| **X11TextInput** | XTest extension text input |
| **ClipboardManager** | Secure clipboard with KSystemClipboard (Wayland) |
| **DBusNotificationManager** | freedesktop.org notifications |

---

## 3. ARCHITECTURAL CONCERNS & LAYER VIOLATIONS

### ✅ POSITIVE PATTERNS DETECTED

#### 1. **Dependency Inversion Principle (IOathSelector)**
- **Pattern:** CardTransaction (pcsc/) → IOathSelector (interface in pcsc/)
- **Benefit:** Breaks circular dependency between pcsc/ and oath/ layers
- **Implementation:** YkOathSession implements IOathSelector
- **Status:** ✅ **EXCELLENT DESIGN**

#### 2. **Template Method Pattern (OathDevice)**
- **Base class:** OathDevice (550 lines impl)
- **Polymorphic member:** `m_session` (std::unique_ptr<YkOathSession>)
- **Benefit:** Eliminates 550+ lines of YubiKey/Nitrokey duplication
- **Status:** ✅ **EXCELLENT - REDUCES CODE DUPLICATION**

#### 3. **RAII Pattern (CardTransaction)**
- **Guarantee:** SCardEndTransaction always called via destructor
- **Exception-safe:** Works correctly during stack unwinding
- **Rate-limiting:** 50ms between operations per device
- **Status:** ✅ **EXCELLENT - CRITICAL FOR PC/S RELIABILITY**

#### 4. **Async Architecture (PcscWorkerPool)**
- **Thread pool:** 4 workers, per-device rate limiting
- **Priority queuing:** Background < Normal < UserInteraction
- **Benefit:** Non-blocking daemon startup + hot-plug
- **Status:** ✅ **GOOD - IMPROVES RESPONSIVENESS**

#### 5. **Result<T> Pattern**
- **Feature:** [[nodiscard]] attributes force error handling
- **Prevents:** Silent failures and bugs
- **Status:** ✅ **EXCELLENT - COMPILE-TIME SAFETY**

#### 6. **Secure Memory (SecureMemory::SecureString)**
- **Usage:** OathDevice::m_password automatic memory wiping
- **Benefit:** Defense-in-depth password security
- **Status:** ✅ **EXCELLENT - SECURITY-FOCUSED**

---

### ⚠️ ARCHITECTURAL CONCERNS IDENTIFIED

#### 1. **OathActionCoordinator Dependencies (MODERATE CONCERN)**
```cpp
// In oath_action_coordinator.h
OathActionCoordinator::OathActionCoordinator(
    OathService *service,                    // ← Circular via serviceManager
    OathDeviceManager *deviceManager,        // ← Stored member
    OathDatabase *database,                  // ← Stored member
    SecretStorage *secretStorage,            // ← Stored member
    DaemonConfiguration *config,
    QObject *parent)
```
- **Issue:** Aggregates 5+ dependencies (high coupling)
- **Location:** `src/daemon/actions/oath_action_coordinator.h:59-64`
- **Impact:** Changes to any dependency ripple through coordinator
- **Current State:** Functional but coupling-heavy
- **Recommendation:** Consider breaking into separate components per workflow type

#### 2. **OathService as Central Hub (MODERATE CONCERN)**
```cpp
// In OathService::OathService() constructor - line 35-44
m_deviceManager         // ← Creates
m_database              // ← Creates
m_secretStorage         // ← Creates
m_config                // ← Creates
m_actionCoordinator     // ← Creates (depends on above)
m_passwordService       // ← Creates (depends on above)
m_deviceLifecycleService // ← Creates (depends on above)
m_credentialService     // ← Creates (depends on above)
```
- **Issue:** "God Object" - owns 8 major components
- **Location:** `src/daemon/services/oath_service.{h,cpp}:35-44, 366-373`
- **Current State:** Functional but High Cohesion could be better
- **Trade-off:** Acceptable for daemon use (single-threaded event loop)
- **Recommendation:** If adding more services, consider layered creation (factory pattern)

#### 3. **Workflows Depend on Multiple Services (LOW CONCERN)**
```
TouchWorkflowCoordinator depends on:
├── OathDeviceManager (device ops)
├── OathDatabase (device info)
├── OathActionCoordinator (execute actions)
├── TouchHandler (polling)
└── NotificationOrchestrator (UI feedback)

ReconnectWorkflowCoordinator depends on:
├── OathService (credentials)
├── OathDeviceManager (device states)
├── SecretStorage (password recovery)
└── NotificationOrchestrator (UI feedback)
```
- **Issue:** Workflows are integration points, inherent coupling
- **Current State:** Acceptable for workflow orchestration
- **Benefit:** Keeps workflow logic isolated despite dependencies

#### 4. **UI Components in Daemon (LOW CONCERN)**
```
src/daemon/ui/add_credential_dialog.{h,cpp}
src/daemon/ui/processing_overlay.{h,cpp}
```
- **Issue:** Dialog is owned by OathService, displayed from action handler
- **Location:** `src/daemon/ui/` included by `oath_action_coordinator.cpp`
- **Current State:** Functional but unconventional (daemon showing UI)
- **Trade-off:** Required for add-credential workflow (QR → dialog → device)
- **Recommendation:** Consider moving to separate UI daemon, but keep current approach for now

#### 5. **Notification Manager Uses D-Bus (LOW CONCERN)**
```cpp
// Potential issue: Using D-Bus notifications from daemon's own D-Bus service
DBusNotificationManager::notify() uses freedesktop.org notifications
```
- **Current State:** Works correctly (separate D-Bus call)
- **No violation:** Notifications on org.freedesktop.Notifications, daemon on pl.jkolo.yubikey.oath.daemon

#### 6. **Config Dependency Pattern (LOW CONCERN)**
```cpp
// Multiple services depend on DaemonConfiguration
PasswordService(... DaemonConfiguration *config)
DeviceLifecycleService(... DaemonConfiguration *config)
CredentialService(... DaemonConfiguration *config)
OathActionCoordinator(... DaemonConfiguration *config)
NotificationOrchestrator(... DaemonConfiguration *config)
```
- **Current State:** Clean but coupling to config
- **Justification:** Configuration changes affect multiple services
- **Recommendation:** Monitor for cross-service config dependencies

---

### ✅ NO CIRCULAR DEPENDENCIES DETECTED

**Verified:**
- CardTransaction (pcsc/) → IOathSelector (pcsc/) ✅
- OathDevice (oath/) → YkOathSession (oath/) ✅
- Services (services/) → OathDeviceManager (oath/) ✅
- OathService (services/) → OathDatabase (storage/) ✅
- Workflows (workflows/) → Services (services/) ✅

**Clean separation:** No layer depends on higher layers

---

## 4. FILES OUT OF PLACE OR REORGANIZATION OPPORTUNITIES

### ✅ WELL-PLACED (NO ISSUES)

| File | Current Location | Reason |
|------|------------------|--------|
| `oath_error_codes.h` | `oath/` | Domain-specific constants ✅ |
| `oath_protocol.h` | `oath/` | Shared OATH utilities ✅ |
| `secure_memory.h` | `utils/` | Utility, not domain-specific ✅ |
| `qr_code_parser.h` | `utils/` | Add-credential helper ✅ |
| `async_waiter.h` | `utils/` | Infrastructure utility ✅ |

### ⚠️ MINOR REORGANIZATION OPPORTUNITIES

#### 1. **screenshot_capturer.h (OPTIONAL MOVE)**
```
Current:  src/daemon/utils/screenshot_capturer.{h,cpp}
Issue:    Only used by AddCredentialDialog (UI layer)
Suggestion: Could move to src/daemon/ui/ since it's UI-specific
Impact:   Minimal - would clarify intent but current location acceptable
Status:   MINOR - Keep current location for now
```

#### 2. **credential_cache_searcher.h (GOOD LOCATION)**
```
Current:  src/daemon/cache/credential_cache_searcher.{h,cpp}
Location: Correctly separated into "cache" layer
Uses:     OathDatabase (storage layer) - correct dependency
Status:   ✅ GOOD LOCATION
```

#### 3. **notification_utils.h (CONSIDER CONSOLIDATION)**
```
Current:  src/daemon/workflows/notification_utils.{h,cpp}
Current:  src/daemon/workflows/notification_helper.{h,cpp}
Issue:    Both serve similar purposes (notification utilities)
Suggestion: Consolidate into single file if <200 lines total
Impact:   Reduce files, clarify responsibilities
Status:   OPTIONAL - Current split is acceptable
```

#### 4. **code_validator.h (CONSIDER MOVE)**
```
Current:  src/daemon/formatting/code_validator.{h,cpp}
Issue:    Only validates OATH code format, not part of credential formatting
Alternative: Could move to src/daemon/utils/ or src/daemon/oath/
Current Status: Works fine in formatting/ - related to output format
Status:   KEEP AS-IS - Current location is acceptable
```

---

## 5. LAYER DEPENDENCY DIAGRAM

```
┌─────────────────────────────────────────────────────────────────┐
│                     PRESENTATION LAYER                          │
├──────────────────────────────────────────────────────────────────┤
│  OathDBusService (marshaling)                                   │
│      ↓ delegates                                                │
│  OathManagerObject (root)                                       │
│      ↓ owns                                                     │
│  OathDeviceObject + OathCredentialObject                        │
└────────────────────────────↓──────────────────────────────────┘
                             │
┌────────────────────────────↓──────────────────────────────────┐
│                    BUSINESS LOGIC LAYER                        │
├──────────────────────────────────────────────────────────────┤
│  OathService (aggregate)                                       │
│  ├─ PasswordService                                            │
│  ├─ DeviceLifecycleService                                    │
│  ├─ CredentialService                                         │
│  └─ OathActionCoordinator                                     │
└────────────────────────────↓──────────────────────────────────┘
                             │
┌────────────────────────────↓──────────────────────────────────┐
│                      DOMAIN LAYER                              │
├──────────────────────────────────────────────────────────────┤
│  OathDeviceManager                                             │
│  ├─ OathDevice (base)                                         │
│  │  ├─ YubiKeyOathDevice  ──→ YkOathSession                  │
│  │  └─ NitrokeyOathDevice ──→ NitrokeyOathSession            │
│  │                 ↓                 ↓                        │
│  │          OathProtocol    YkOathProtocol                   │
│  │                       NitrokeySecretsOathProtocol         │
│  │                       ManagementProtocol                  │
│  └─ CardReaderMonitor                                        │
└────────────────────────────↓──────────────────────────────────┘
                             │
┌────────────────────────────↓──────────────────────────────────┐
│                  INFRASTRUCTURE LAYER                          │
├──────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────┐                                 │
│  │  PC/S Layer             │                                 │
│  ├─────────────────────────┤                                 │
│  │ CardTransaction (RAII)  │                                 │
│  │     ↓ uses              │                                 │
│  │ IOathSelector (iface)   │                                 │
│  │     ↓ impl by           │                                 │
│  │ YkOathSession           │                                 │
│  └─────────────────────────┘                                 │
│                                                              │
│  ┌─────────────────────────┐                                 │
│  │  Storage Layer          │                                 │
│  ├─────────────────────────┤                                 │
│  │ OathDatabase (SQLite)   │                                 │
│  │ SecretStorage (KWallet) │                                 │
│  └─────────────────────────┘                                 │
│                                                              │
│  ┌─────────────────────────┐                                 │
│  │  Async Layer            │                                 │
│  ├─────────────────────────┤                                 │
│  │ PcscWorkerPool (QThread)│                                 │
│  └─────────────────────────┘                                 │
└────────────────────────────↓──────────────────────────────────┘
                             │
┌────────────────────────────↓──────────────────────────────────┐
│                  USER INTERACTION LAYER                        │
├──────────────────────────────────────────────────────────────┤
│  Workflows:                          IO:                      │
│  ├─ TouchWorkflowCoordinator        ├─ TextInputFactory      │
│  ├─ ReconnectWorkflowCoordinator    ├─ PortalTextInput       │
│  └─ ActionExecutor (copy/type/del)  ├─ X11TextInput          │
│                                      ├─ ClipboardManager      │
│  Notifications:                      └─ DBusNotificationManager
│  ├─ NotificationOrchestrator                                │
│  ├─ NotificationHelper                                       │
│  └─ NotificationUtils                                        │
│                                                              │
│  UI:                                                         │
│  ├─ AddCredentialDialog                                      │
│  └─ ProcessingOverlay                                        │
└────────────────────────────↓──────────────────────────────────┘
                             │
┌────────────────────────────↓──────────────────────────────────┐
│                    UTILITY LAYER                              │
├──────────────────────────────────────────────────────────────┤
│  ├─ SecureMemory::SecureString                               │
│  ├─ QRCodeParser                                             │
│  ├─ OtpAuthUriParser                                         │
│  ├─ CodeValidator                                            │
│  ├─ CredentialCacheSearcher                                  │
│  └─ DaemonConfiguration (KConfig)                            │
└────────────────────────────────────────────────────────────────┘

LEGEND:
─ ─ → Depends on (allowed)
→ → Cannot depend on (forbidden)
✅ All dependencies go DOWNWARD (acyclic)
```

---

## 6. SUMMARY & RECOMMENDATIONS

### Overall Assessment: ✅ **CLEAN & WELL-STRUCTURED**

**Strengths:**
1. ✅ **No circular dependencies** - All layers properly separated
2. ✅ **Dependency Inversion** - IOathSelector breaks layering issues
3. ✅ **RAII patterns** - CardTransaction ensures PC/S cleanup
4. ✅ **Async architecture** - PcscWorkerPool prevents blocking
5. ✅ **Security-focused** - SecureMemory, Result<T>, error codes
6. ✅ **Multi-brand support** - Template Method reduces duplication
7. ✅ **Clear responsibility separation** - Each layer has single purpose

**Moderate Concerns (No immediate action needed):**
1. ⚠️ OathActionCoordinator high coupling (5+ dependencies)
2. ⚠️ OathService "God Object" (owns 8 components)
3. ⚠️ UI components in daemon (unconventional but justified)

**Recommendations:**
1. **Monitor OathActionCoordinator** - If adding more workflows, consider splitting by workflow type
2. **Document OathService** - Large hub is acceptable for daemon, but document dependency order
3. **Consider moving screenshot_capturer** - If it grows, move to `ui/` directory
4. **Profile PcscWorkerPool** - Verify 50ms rate limiting is sufficient for all devices
5. **Test Nitrokey support** - Complex protocol differences (0x6982 vs 0x6985, TAG_VALUE, etc.)

**File Organization Score: 9/10**
- Well-organized layers with clear separation
- Minor opportunities for refinement (optional, not necessary)
- Current structure supports testing, maintenance, and extension

