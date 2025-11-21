# KRunner YubiKey OATH Plugin

KDE Plasma 6 plugin: YubiKey/Nitrokey OATH TOTP/HOTP codes in KRunner via D-Bus daemon

## Build & Install

```bash
cmake --preset clang-debug
cmake --build build-clang-debug -j$(nproc)
run0 cmake --install build-clang-debug
krunner --replace  # restart (NEVER killall)
```

**Verify:** `/usr/bin/yubikey-oath-daemon`, `/usr/lib/qt6/plugins/kf6/krunner/krunner_yubikey.so`, `/usr/lib/qt6/plugins/kcm_krunner_yubikey.so`

**Debug:** `QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true" QT_LOGGING_TO_CONSOLE=1 krunner --replace 2>&1`

**Daemon:** `systemctl --user {status,restart} app-pl.jkolo.yubikey.oath.daemon.service`, `journalctl --user-unit=app-pl.jkolo.yubikey.oath.daemon.service --follow`

## Repository & Release

**Primary Repository:** GitLab at `git.kolosowscy.pl:jkolo/krunner-yubikey-oath` (private)
**Public Mirror:** GitHub at `github.com/jkolo/yubikey-oath-krunner` (automatically mirrored from GitLab)

**Release Process:**
- All development happens on GitLab (origin: `git@git.kolosowscy.pl:jkolo/krunner-yubikey-oath.git`)
- GitHub mirror is automatically synchronized from GitLab
- Public releases (for AUR package) are distributed via GitHub releases
- AUR package (`krunner-yubikey-oath`) references GitHub tarball URLs

**Tagging Convention:** `v{MAJOR}.{MINOR}.{PATCH}` (e.g., `v2.0.0`)

**Release Workflow:**
1. Update version in: CMakeLists.txt, yubikeyrunner.json, plasma-runner-yubikey.desktop, pl.po, README.md
2. Create/update CHANGELOG.md
3. Commit: `git commit -am "chore: release version X.Y.Z"`
4. Tag: `git tag -a vX.Y.Z -m "Release version X.Y.Z"`
5. Push to GitLab: `git push origin main && git push origin vX.Y.Z`
6. GitHub auto-mirrors the tag
7. Create GitHub release manually (triggers tarball generation)
8. Update AUR package with new version and tarball SHA256

## Architecture

**Source:** `src/shared/` (types, dbus, ui, utils, po, resources), `src/daemon/` (services, oath, storage, pcsc), `src/krunner/` (plugin), `src/config/` (KCM)

### D-Bus Hierarchy

```
/pl/jkolo/yubikey/oath (Manager) → /devices/<id> (Device + DeviceSession) → /credentials/<id> (Credential)
```

**Legacy:** `pl.jkolo.yubikey.oath.daemon.Device` at `/Device` (ListDevices, GetCredentials, GenerateCode, etc.)

**D-Bus Interfaces (v2.0.0+):**
One D-Bus object at `/devices/<id>` exposes **two interfaces**:
- **pl.jkolo.yubikey.oath.Device** - hardware properties + OATH application operations (ChangePassword, AddCredential, Forget)
- **pl.jkolo.yubikey.oath.DeviceSession** - connection state + daemon configuration (State, SavePassword, HasValidPassword)

**Client Proxy Objects:**
- **OathManagerProxy** (~120 lines): singleton, ObjectManager, devices(), getDeviceSession(), getAllCredentials(), signals: deviceConnected/Disconnected, credentialsChanged
- **OathDeviceProxy** (~130 lines): credentials(), changePassword(), forget(), addCredential(), setName(), requiresPassword()
- **OathDeviceSessionProxy** (~150 lines): state(), stateMessage(), hasValidPassword(), lastSeen(), isConnected(), savePassword()
- **OathCredentialProxy** (~100 lines): generateCode(), copyToClipboard(), typeCode(), deleteCredential()

**Daemon Objects:**
- **OathDeviceObject** (~280 lines): exposes both Device and DeviceSession interfaces via 2 adaptors on same D-Bus object

**Service:** YubiKeyDBusService (marshaling ~135 lines) → YubiKeyService (business logic ~430 lines) → PasswordService (~225 lines), DeviceLifecycleService (~510 lines), CredentialService (~660 lines)

**Example Usage:**
```cpp
// Client-side: Two proxies for one D-Bus object
OathManagerProxy *manager = OathManagerProxy::instance();
OathDeviceProxy *device = manager->getDevice(deviceId);
OathDeviceSessionProxy *session = manager->getDeviceSession(deviceId);

// Device operations (OATH application)
if (device->requiresPassword()) {
    device->changePassword(oldPass, newPass);  // modifies on YubiKey
}
device->addCredential(name, secret, ...);

// Session operations (daemon state)
if (session->state() == DeviceState::Ready) {
    session->savePassword(password);  // saves to KWallet
}
```

### Multi-Brand OATH (v2.0.0+)

**Supported:** YubiKey (NEO/4/5/Bio), Nitrokey (3A/3C/3Mini)

**⚠️ BREAKING CHANGES (v2.0.0):**
- **D-Bus interface split:** `pl.jkolo.yubikey.oath.Device` split into Device (hardware/OATH) + DeviceSession (connection state)
  - Device interface: removed `State`, `StateMessage`, `HasValidPassword`, `LastSeen`, `SavePassword()`
  - New DeviceSession interface: `State`, `StateMessage`, `HasValidPassword`, `LastSeen`, `SavePassword()`
  - **Client migration:** Use `OathDeviceProxy` + `OathDeviceSessionProxy` instead of single proxy
- `IDeviceIconResolver` interface changed: 2-param → 3-param (added `capabilities`)
- D-Bus proxy classes renamed: `YubiKey*Proxy` → `Oath*Proxy`
- Internal API changes for brand-agnostic device handling

**Hierarchy:**
```
OathDevice (base ~550 lines impl)
├── YubiKeyOathDevice (YkOathSession)
└── NitrokeyOathDevice (NitrokeyOathSession)

YkOathSession (base protocol)
├── YubiKeyOathSession (CALCULATE_ALL 0xA4)
└── NitrokeyOathSession (LIST 0xA1 + CALCULATE 0xA2)
```

**OathDevice** (`src/daemon/oath/oath_device.{h,cpp}`) - **Template Method + Factory**:
- Polymorphic `m_session` (std::unique_ptr<YkOathSession>)
- Pure virtual `createTempSession()` - brand-specific factory
- 13 implemented methods: connect/disconnect, password ops, credential ops, fetchCredentialsSync, reconnectCardHandle
- **Eliminated ~550 lines duplication** from derived classes

**Protocol Differences:**

| Feature | YubiKey | Nitrokey 3 |
|---------|---------|------------|
| CALCULATE | 0xA4 (ALL) | 0xA1 (LIST) + 0xA2 |
| Touch status | 0x6985 | 0x6982 |
| Serial | Mgmt API | SELECT 0x8F |
| Firmware | Mgmt/PIV | SELECT 0x79 |

**⚠️ CRITICAL:**
- **TAG_PROPERTY (0x78)** is Tag-Value NOT TLV: `78 02` correct, `78 01 02` wrong (0x6a80 error)
- **Nitrokey detection** uses firmware heuristic (≥1.6.0 → 3C variant), may need tuning - see `nitrokey_model_detector.cpp:52-62`

**Add brand:** DeviceBrand enum → detectBrand() → NewBrandOathSession → NewBrandOathDevice → factory cases

**Brand Detection & Model Support:**

- **DeviceBrand** (`src/shared/types/device_brand.{h,cpp}` ~120 lines): Enum (YubiKey/Nitrokey), detectBrand() from reader name/firmware, detectBrandFromModelString(), brandToString(), supportedBrands()
- **DeviceModel** (`src/shared/types/device_model.h` ~177 lines): Brand-agnostic model representation, unified encoding (0xSSVVPPFF for YubiKey, 0xGGVVPPFF for Nitrokey), helper methods (hasNFC(), supportsOATH(), isFIPS())
- **DeviceCapabilities** (`src/shared/types/device_capabilities.{h,cpp}` ~90 lines): Runtime capability detection, detect() from capabilities list, hasOATH(), hasNFC(), toStringList()
- **NitrokeyModelDetector** (`src/daemon/oath/nitrokey_model_detector.{h,cpp}` ~200 lines): Firmware-based detection (≥1.6.0 → NK3C heuristic with logging), NFC capability detection, generation enum (NK3A Mini/USB/NFC, NK3C), detectModel() with confidence tracking and debug logging

### Core Components

- **YubiKeyRunner** (`src/krunner/yubikeyrunner.{h,cpp}` ~200 lines): plugin entry, uses ManagerProxy
- **YubiKeyDeviceManager** (`src/daemon/oath/yubikey_device_manager.{h,cpp}` ~400 lines): multi-device, PC/SC, hot-plug, factories
- **IOathSelector** (`src/daemon/pcsc/i_oath_selector.h` ~72 lines): Interface for OATH applet selection, implements Dependency Inversion Principle
- **YkOathSession** (`src/daemon/oath/yk_oath_session.{h,cpp}` ~450 lines): OATH protocol base implementing IOathSelector, PC/SC I/O with 50ms rate limiting, PBKDF2, NOT thread-safe
- **OathProtocol** (`src/daemon/oath/oath_protocol.{h,cpp}` ~200 lines): utilities, constants, TLV parsing
- **OathErrorCodes** (`src/daemon/oath/oath_error_codes.h`): Translation-independent error constants (PASSWORD_REQUIRED, TOUCH_REQUIRED, etc.)
- **ManagementProtocol** (`src/daemon/oath/management_protocol.{h,cpp}` ~150 lines): GET DEVICE INFO, YubiKey 4.1+

### PC/SC Lock Management

**Problem:** Previous approach used `SCARD_SHARE_EXCLUSIVE` for entire device lifecycle, blocking other applications (GnuPG, ykman) from accessing card.

**Solution:** Use `SCARD_SHARE_SHARED` with per-operation exclusive transactions via RAII pattern.

**Architecture:**
- **IOathSelector** (`src/daemon/pcsc/i_oath_selector.h` ~72 lines): Interface for OATH applet selection
  - Abstracts `selectOathApplication()` operation following Dependency Inversion Principle
  - Breaks circular dependency: pcsc/ layer → IOathSelector (pcsc/) ← YkOathSession (oath/) implements
  - Allows CardTransaction (pcsc/ layer) to work with any OATH session implementation
  - Implemented by YkOathSession base class (all brands: YubiKey, Nitrokey)

- **CardTransaction** (`src/daemon/pcsc/card_transaction.{h,cpp}` ~125 lines): RAII class managing PC/SC transaction lifecycle
  - Constructor: `SCardBeginTransaction()` + automatic `SELECT OATH` applet via IOathSelector (unless `skipOathSelect=true`)
  - Destructor: `SCardEndTransaction(SCARD_LEAVE_CARD)` - automatic cleanup
  - Move semantics: Prevents double-EndTransaction via move constructor/assignment with logging
  - Error handling: `isValid()`, `errorMessage()` for transaction/SELECT failures
  - Security: Validates cardHandle (non-zero) and session pointer (non-null when SELECT required)
  - Logging: Debug messages for transaction lifecycle, warnings for errors (move assignment included)

**Transaction Scope:**
- **One transaction per D-Bus operation**: BeginTransaction → SELECT OATH → authenticate (if password required) → operation → EndTransaction
- **Managed at OathDevice level**: Each public method (`generateCode()`, `addCredential()`, `deleteCredential()`, `authenticateWithPassword()`, `changePassword()`, `fetchCredentialsSync()`) creates CardTransaction
- **Session methods are pure protocol**: YkOathSession/NitrokeyOathSession methods (`calculateCode()`, `calculateAll()`, etc.) no longer manage transactions - they only send APDUs

**Benefits:**
1. **Cooperative multi-access**: GnuPG can access card between OATH operations (no blocking)
2. **Atomic operations**: Each D-Bus call is fully isolated transaction (SELECT + auth + operation)
3. **Automatic cleanup**: RAII ensures EndTransaction even on exceptions/early returns
4. **Simplified error handling**: Transaction/SELECT errors caught before operation starts
5. **Touch notification timing**: Preemptive signal emission allows UI updates before blocking operations

**Touch-Required Credentials:**
- **Challenge**: CardTransaction's BEGIN→SELECT→AUTH→CALCULATE flow blocks thread during CALCULATE (waiting for user touch, status word 0x6985/0x6982)
- **Solution**: OathDevice::generateCode() emits touchRequired() signal BEFORE calling session's calculateCode() method (oath_device.cpp:201-206)
- **Benefit**: Notification appears immediately instead of being queued behind blocked operation, improving UX

**Example Flow (generateCode):**
```cpp
// OathDevice::generateCode() - manages transaction
// m_session is std::unique_ptr<YkOathSession> which implements IOathSelector
CardTransaction transaction(m_cardHandle, m_session.get());  // Polymorphic: YkOathSession* → IOathSelector*
if (!transaction.isValid()) {
    return Result<QString>::error(transaction.errorMessage());  // Validation + SELECT errors
}

// Authenticate if password required
if (!m_password.isEmpty()) {
    auto authResult = m_session->authenticate(m_password, m_deviceId);  // No SELECT - already done
    if (authResult.isError()) {
        return Result<QString>::error(i18n("Authentication failed"));  // KDE i18n, NOT Qt tr()
    }
}

// Calculate code
auto result = m_session->calculateCode(name, period);  // Pure protocol - no transaction management

// Destructor automatically calls SCardEndTransaction()
return result;
```

**skipOathSelect Parameter:**
- Used when method needs manual SELECT control (e.g., `getExtendedDeviceInfo()` doing Management API SELECT)
- Default: `false` (automatic SELECT OATH)

**Workflows:**
- NotificationOrchestrator: notifications, countdown, progress
- TouchWorkflowCoordinator: touch workflow, polling
- ActionExecutor: copy/type execution
- YubiKeyActionCoordinator: daemon-side action handling
- ActionManager: KRunner actions
- MatchBuilder: QueryMatch creation
- TouchHandler: touch detection
- ClipboardManager: secure clipboard, auto-clear

### Async Architecture (v2.0.0+)

**Problem:** Daemon startup blocked D-Bus interface for 10-30 seconds during device initialization, device hot-plug caused UI freezes

**Solution:** Full async architecture with state machine, worker pool, and non-blocking D-Bus API

**Goal:** D-Bus available in <100ms, zero blocking during hot-plug

**Infrastructure:**

- **PcscWorkerPool** (`src/daemon/infrastructure/pcsc_worker_pool.{h,cpp}` ~190 lines):
  - Singleton thread pool (default 4 workers) for all PC/SC operations
  - Per-device rate limiting (50ms minimum interval) prevents communication errors
  - Priority-based queuing: Background < Normal < UserInteraction
  - Thread-safe with QMutex protection
  - **Usage:** `PcscWorkerPool::instance().submit(deviceId, operation, priority)`

- **DeviceState** (`src/shared/types/device_state.{h,cpp}` ~115 lines):
  - Enum: Disconnected, Connecting, Authenticating, FetchingCredentials, Ready, Error
  - Helper functions: `deviceStateToString()`, `isDeviceStateTransitional()`, `isDeviceStateReady()`
  - D-Bus marshaling operators for state propagation
  - Registered with Qt metatype system for signals/slots

- **AsyncResult<T>** (`src/shared/common/async_result.h` ~145 lines):
  - Template wrapper for async operations: `operationId` (UUID) + `QFuture<Result<T>>`
  - Auto-generates unique operation IDs for tracking
  - Specialization for `void` results (operations without return value)
  - **Usage:** `auto async = AsyncResult<QString>::create(future)`

**State Machine (OathDevice):**

- 6 states with transitions: Disconnected → Connecting → Authenticating → FetchingCredentials → Ready ↔ Error
- `setState()` method emits D-Bus signals on state changes via PropertiesChanged
- All state transitions happen on worker threads, signals propagate to D-Bus

**Async Service Layer:**

- **YubiKeyDeviceManager**: Device detection and initialization fully async
  - `initializeDeviceAsync()` returns `AsyncResult<void>`
  - Hot-plug events processed on worker threads
  - State changes broadcast via D-Bus ObjectManager signals

- **DeviceLifecycleService**: All operations async
  - `initializeAsync()`, `reconnectAsync()`, `changePasswordAsync()`
  - Uses `PcscWorkerPool` for all PC/SC operations
  - Returns `AsyncResult<T>` for tracking

- **CredentialService**: Credential operations async
  - `generateCodeAsync()`, `deleteAsync()`, `addCredentialAsync()`
  - Results delivered via D-Bus signals: `CodeGenerated`, `Deleted`
  - NoReply D-Bus methods (fire-and-forget pattern)

**D-Bus API Extensions:**

- **Device interface** (`pl.jkolo.yubikey.oath.Device.xml`):
  - Properties: `State` (byte), `StateMessage` (string)
  - State exposed to clients via PropertiesChanged signal for UI feedback

- **Credential interface** (`pl.jkolo.yubikey.oath.Credential.xml`):
  - Async methods: `GenerateCodeAsync()` (NoReply), `DeleteAsync()` (NoReply)
  - Result signals: `CodeGenerated(string code, int64 validUntil, string error)`, `Deleted(bool success, string error)`
  - Fire-and-forget pattern: call method, wait for signal

**Client-Side Proxies:**

- **OathDeviceProxy**:
  - Tracks device state locally: `state()`, `stateMessage()` getters
  - Emits Qt signals: `stateChanged()`, `stateMessageChanged()`
  - Auto-updates on D-Bus PropertiesChanged

- **OathCredentialProxy**:
  - Async methods: `generateCodeAsync()`, `deleteAsync()`
  - Qt signals: `codeGenerated()`, `deleted()`
  - Code caching for performance (eliminates redundant D-Bus calls)

- **OathManagerProxy**:
  - Bulk state query: `getDeviceStates()` → `QMap<QString, DeviceState>`
  - Single D-Bus call instead of N per-device queries

**KRunner Integration:**

- **YubiKeyRunner**: Filters devices by state in `match()` method
  - Skips devices in transitional states (Connecting, Authenticating, FetchingCredentials)
  - Only shows credentials from Ready devices
  - Displays "X device(s) initializing" instead of stale data
  - Logs state changes for debugging

**Performance:**

- Daemon starts in <100ms (D-Bus interface immediately available)
- Device initialization runs async on worker threads (no blocking)
- Hot-plug detection non-blocking (processed via worker pool)
- State visibility allows UI to show progress instead of freezing

**Testing:**

- **test_async_result** (9 tests): AsyncResult<T> template, operation IDs, void specialization
- **test_pcsc_worker_pool** (10 tests): Rate limiting, priority queuing, thread pool management, concurrent devices

### Domain Models (v2.0.0+)

**OathCredential** (`src/shared/types/oath_credential.{h,cpp}`) - **Rich Domain Model**:
- Data: originalName, issuer, account, code, validUntil, requiresTouch, isTotp, deviceId, digits, period, algorithm, type
- Behavior (Tell, Don't Ask): getDisplayName(options), getDisplayNameWithCode(code, touch, options), matches(name, deviceId), isExpired(), needsRegeneration(threshold)
- **Eliminated ~50 lines** from utility classes

**CredentialFormatter** (`src/shared/formatting/credential_formatter.{h,cpp}`): Facade delegating to OathCredential (50 → 10 lines)

**CredentialFinder** (`src/shared/utils/credential_finder.{h,cpp}` ~25 lines): findCredential() using OathCredential::matches()

**YubiKeyIconResolver** (`src/shared/utils/yubikey_icon_resolver.{h,cpp}` ~175 lines): Returns hicolor theme icon names (e.g., "yubikey-5c-nfc"), fallback hierarchy, icons installed in `/usr/share/icons/hicolor/`, FIPS same as non-FIPS, generic fallback "yubikey-oath"

### UI Components

**DeviceDelegate** (`src/config/device_delegate.{h,cpp}` ~425 lines): QStyledItemDelegate, styled cards, IDeviceIconResolver interface, buttons, inline edit

**IDeviceIconResolver** (`src/config/i_device_icon_resolver.h`): 3-param interface `getModelIcon(modelString, modelCode, capabilities)` - KCM context without PC/SC

**YubiKeyConfigIconResolver** (~60 lines): adapter, reconstructs DeviceModel, uses detectBrandFromModelString()

**DeviceCardLayout** (~140 lines), **DeviceCardPainter** (~255 lines), **RelativeTimeFormatter** (~100 lines): SRP-extracted utilities

### Storage

**YubiKeyDatabase** (`src/daemon/storage/yubikey_database.{h,cpp}`): SQLite `~/.local/share/krunner-yubikey/devices.db`, tables: devices, credentials (CASCADE FK), TransactionGuard RAII

**PasswordStorage** (`src/daemon/storage/secret_storage.{h,cpp}`): KWallet per-device, folder "YubiKey OATH Application", uses SecureMemory::SecureString for automatic memory wiping

**CredentialCacheSearcher** (~100 lines): SRP-extracted, offline device search

### Security Features (v2.0.0+)

**Error Handling:**
- **OathErrorCodes** (`src/daemon/oath/oath_error_codes.h`): Translation-independent error constants prevent i18n bugs where `result.error() == tr("Password required")` breaks in non-English locales
- **Result<T>** (`src/shared/common/result.h`): Rust-inspired monad with [[nodiscard]] attributes on all methods, enforcing compile-time error handling checks

**Memory Security:**
- **SecureMemory::SecureString** (`src/daemon/utils/secure_memory.h`): Used for OathDevice::m_password (line 226), automatic memory wiping on destruction, defense-in-depth protection

**PC/SC Reliability:**
- **Rate Limiting** (YkOathSession): 50ms minimum interval between PC/SC operations prevents communication errors with slower readers or during high-frequency operations (TOTP auto-refresh)
- **Automatic PC/SC Service Recovery** (v2.4.0+): Daemon automatically recovers from pcscd.service restarts without manual intervention
  - **Detection**: CardReaderMonitor detects SCARD_E_NO_SERVICE (0x8010001D) errors in monitoring loops
  - **Signal**: Emits `pcscServiceLost()` signal once when service unavailability is detected (guarded by flag to prevent spam)
  - **Recovery Process** (6 steps in `YubiKeyDeviceManager::handlePcscServiceLost()`):
    1. Stop card reader monitoring
    2. Disconnect all devices (card handles become invalid after pcscd restart)
    3. Release old PC/SC context (SCardReleaseContext)
    4. Wait 2 seconds for pcscd stabilization
    5. Re-establish PC/SC context (SCardEstablishContext)
    6. Reset monitor state and restart monitoring
  - **Device Re-enumeration**: After recovery, devices are automatically re-detected via explicit enumeration (critical for devices that remained connected during pcscd restart)
  - **Implementation**: card_reader_monitor.cpp:267-378 (detection), yubikey_device_manager.cpp:832-899 (recovery + re-enumeration)

### Input System

TextInputFactory → Portal (libportal, xdp_session_keyboard_key, all Wayland) → X11 (XTest extension)

**Portal Session Lifecycle (v2.5.0+):**
- **Session-Per-Operation**: Portal RemoteDesktop session is created and destroyed for EACH typeText() operation (not persisted across daemon lifecycle)
- **Benefits**: Faster daemon startup (~30s improvement), no session state management, cleaner resource usage
- **Portal Handle Reuse**: XdpPortal handle is cached and reused across operations (only session is recreated)
- **Restore Token**: Saved in KWallet - user approves permission once, subsequent sessions auto-approve without dialog
- **Timing**: Session creation ~1-2s with restore token (vs ~30s on first permission), typing ~10-50ms, session close ~5-10ms
- **Logging**: QElapsedTimer tracks portal init, session create/close, and typing operations (visible with `QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true"`)
- **Error Handling**: Session creation failure triggers automatic fallback to clipboard with notification "Could not type code (portal session error). Code copied to clipboard instead."

**Implementation:**
- `PortalTextInput::typeText()` (portal_text_input.cpp:45-118): Creates session → types → closes session
- `PortalTextInput::closeSession()` (portal_text_input.cpp:504-518): Closes session, keeps portal handle
- `PortalTextInput::cleanup()` (portal_text_input.cpp:520-533): Full cleanup (session + portal) on destructor
- No `preInitialize()` call - daemon starts immediately, session created on-demand

### KCM

**YubiKeyConfig** (`src/config/yubikey_config.{h,cpp}`): KCModule, `kcm_krunner_yubikey.so`, ManagerProxy, YubiKeyDeviceModel, IDeviceIconResolver adapter

**YubiKeyDeviceModel**: QAbstractListModel, v2.0.0 roles: DeviceModelStringRole, CapabilitiesRole

**yubikey_config.ui**: Qt Widgets (NOT QML)

## Design Patterns

### Template Method + Factory (v2.0.0)
**Problem:** 76% duplication (~550 lines) YubiKeyOathDevice ↔ NitrokeyOathDevice

**Solution:** OathDevice base implements ops, derived classes provide `createTempSession()` factory

**Result:** ~550 lines eliminated, single source of truth, polymorphic m_session, easy brand addition

### Rich Domain Model (v2.0.0)
**Problem:** Anemic OathCredential, logic in utilities (Ask anti-pattern)

**Solution:** Move behavior to domain object (Tell, Don't Ask)

**Result:** ~50 lines eliminated, better encapsulation, co-located logic

### FormatOptionsBuilder (v2.0.0)
Fluent API: `FormatOptionsBuilder().withUsername().withDevice(name).withDeviceCount(count).onlyWhenMultipleDevices().build()`

### Dependency Inversion Principle (v2.4.0+)
**Problem:** CardTransaction (pcsc/ layer) depended on YkOathSession (oath/ layer) - violates layering and prevents reuse

**Solution:** Extract IOathSelector interface, move to pcsc/ layer, YkOathSession implements it

**Result:** Clean layer separation, polymorphism (YkOathSession* → IOathSelector*), breaks circular dependency

**Pattern:**
```
BEFORE: CardTransaction → YkOathSession (WRONG: low-level depends on high-level)
AFTER:  CardTransaction → IOathSelector ← YkOathSession implements (CORRECT: both depend on abstraction)
```

### Notification Management

**NotificationUrgency** (`src/daemon/workflows/notification_utils.h`): Low=0, Normal=1, Critical=2 (follows freedesktop.org spec)

**Critical notifications bypass "Do Not Disturb" mode:**
- Touch request (`showTouchNotification`) - requires physical user interaction
- Device reconnect (`showReconnectNotification`) - requires device insertion
- TOTP code display (`showCodeNotification`) - time-sensitive (30s window)

**Device-specific icons in notifications:**
- Uses `image-path` hint with theme icon names (e.g., "yubikey-5c-nfc")
- Automatically displays device model icon alongside notification text
- Fallback to "yubikey-oath" if specific model icon unavailable

**Implementation:** `timeout=0` (manual countdown QTimer 1s), updateNotification() NOT showNotification(), proper D-Bus type handling (uchar for urgency via `QVariant::fromValue()`)

### Touch Workflow
1. Show notification + countdown, 2. Poll 500ms, 3. Success: close, execute, show code | Timeout: cancel

## i18n & Logging

**i18n:** KDE i18n() NOT Qt tr() - `i18n("text")`, `i18n("Device %1", name)`, `i18np("1 item", "%1 items", n)` - Polish 144 messages 100%, `src/shared/po/`

**Logging:** Qt categories per-module (YubiKeyDaemonLog, YubiKeyRunnerLog, etc.) with "Log" suffix to avoid conflicts

## Testing

**Suites:** 34 tests (v2.4.0+) - unit tests (28) + service tests (3) + storage tests (2) + E2E tests (1), **33/34 passing (97%)** ✅

**Run:** `ctest --preset clang-debug --output-on-failure` or `cd build-clang-debug && ctest --output-on-failure`

**Coverage:** ~85% lines, ~87% functions ✅ **TARGET ACHIEVED & VERIFIED**
- Verified with instrumented build: `cmake -DENABLE_COVERAGE=ON`
- 196 coverage data files generated, 33/34 tests passing
- Phase 1+2+E2E complete, Phase 3 strategically skipped

**📋 Implementation Plan:** See [TEST_IMPLEMENTATION.md](TEST_IMPLEMENTATION.md) for comprehensive testing strategy, roadmap (6 phases), and progress tracking

**🐳 Containerized Testing:** See [CONTAINERIZED_TESTING.md](CONTAINERIZED_TESTING.md) for isolated test execution in containers (Docker or Podman)

### Containerized Testing (v2.5.0+)

**Goal:** Complete isolation from host system - no interference with D-Bus, KWallet, SQLite, or running daemons

**Quick Start:**
```bash
./scripts/test-in-container.sh test           # Run all tests
./scripts/test-in-container.sh coverage       # With coverage report
./scripts/test-in-container.sh shell          # Interactive debugging
```

**Runtime Support:**
- **Docker** - Mature ecosystem, BuildKit, wide CI/CD support
- **Podman** - Rootless by default, no daemon, better security, OCI compliant
- Scripts auto-detect and use whichever is available

**Architecture:**
- **Dockerfile.test** - Arch Linux base with Qt 6.7+, KDE Frameworks 6.0+, PC/SC Lite
- **run-tests-container.sh** - Sets up isolated XDG dirs, private D-Bus session, runs tests
- **docker-compose.test.yml** - Service definitions (tests, coverage, release, shell)
- **test-in-container.sh** - Host-side wrapper with auto-detection (Docker/Podman)

**Isolation:**
- XDG directories: `/tmp/test-home/.local/share`, `.config`, `.cache`, `.runtime`
- D-Bus: Private session bus at `${XDG_RUNTIME_DIR}/dbus-session`
- KWallet: Test mode (`KWALLETD_TESTMODE=1`), isolated storage
- Qt: Offscreen platform (`QT_QPA_PLATFORM=offscreen`), no X11 required

**Benefits:**
- ✅ No interference with host system (safe to run on production machines)
- ✅ Reproducible builds (consistent environment across machines)
- ✅ Clean state (fresh environment for each run)
- ✅ CI/CD ready (portable, easy GitHub Actions/GitLab CI integration)
- ✅ Rootless support (Podman default, enhanced security)

**Comparison:**
- **Host testing:** Fast iteration, direct debugging, but can affect production daemon
- **Container testing:** Full isolation, reproducible, CI/CD friendly, rootless option (Podman)

**CI/CD Integration:**
- **GitHub Actions** - 4 automated workflows in `.github/workflows/`
  - `test.yml` - Continuous testing (Debug + Release)
  - `coverage.yml` - Coverage reports → Codecov
  - `pr-checks.yml` - Fast PR validation (lint, syntax, tests)
  - `release.yml` - Release validation (≥85% coverage threshold)
- **Documentation:** `.github/workflows/README.md`
- **Status badges:** In README.md

### Test Strategy (v2.1.0+)

**Framework:** Qt Test + Virtual Device Emulators + dbus-run-session isolation

**Test Categories:**

1. **Unit Tests (28 tests)** - Test individual components in isolation
   - Result<T>, AsyncResult<T>, PcscWorkerPool
   - OATH protocol, brand detection, device capabilities
   - Workflows, formatters, utilities

2. **Service Layer Tests (3 tests)** - Test business logic and coordination ✅
   - test_password_service - Password management and validation
   - test_device_lifecycle_service - Device connection and initialization
   - test_credential_service - Credential operations and code generation

3. **Storage Layer Tests (2 tests - 29 test cases)** - Test data persistence and security ✅
   - **test_yubikey_database** (19 test cases) - SQLite operations, device metadata, credential caching
   - **test_secret_storage** (10 test cases) - KWallet API, SecureString memory wiping, UTF-8 encoding

4. **E2E Tests (1 test - 7 test cases)** - Test full system with virtual devices
   - **test_e2e_device_lifecycle** (7 test cases: initTestCase, testDeviceDetection, testDeviceStateTransitions, testCredentialList, testGenerateCode, testMultiDevice, cleanupTestCase)
   - Tests device lifecycle: detection → SELECT → LIST → CALCULATE_ALL → multi-device scenarios
   - Isolated D-Bus session (dbus-run-session wrapper)
   - Virtual YubiKey/Nitrokey emulators
   - **Status:** ✅ All 7 test cases passing

**Virtual Device Emulators:**

Located in `tests/mocks/`:
- **VirtualOathDevice** - Base class for OATH device emulation (pure virtual APDU handlers)
  - OATH AID: `A0 00 00 05 27 21 01` (7 bytes, matches OathProtocol::OATH_AID)
  - Serial number parsing: hex string → quint32 (e.g., "12345678" → 0x12345678)
- **VirtualYubiKey** - Emulates YubiKey 5C NFC behavior:
  - CALCULATE_ALL (0xA4) for bulk code generation
  - Touch required via 0x6985 status word
  - LIST bug emulation (spurious 0x6985 errors)
  - Serial via Management API (not in SELECT)
- **VirtualNitrokey** - Emulates Nitrokey 3C behavior:
  - Individual CALCULATE (0xA2) only
  - Touch required via 0x6982 (not 0x6985)
  - LIST v1 with properties byte
  - TAG_PROPERTY (0x78) as Tag-Value (NOT TLV: "78 02" correct, "78 01 02" wrong)
  - TAG_SERIAL_NUMBER (0x8F) in SELECT response

**D-Bus Test Isolation:**

- **TestDbusSession** helper (`tests/helpers/test_dbus_session.{h,cpp}`)
  - Manages isolated D-Bus session for E2E tests
  - RAII cleanup (bus and daemon killed on destruction)
  - Prevents interference with production daemon
- **CMake wrapper:** `dbus-run-session -- $<TARGET_FILE:test_e2e>` (automatic isolation)

**Usage Example:**
```cpp
// E2E test with virtual device
TestDbusSession testBus;
testBus.start();

VirtualYubiKey yubikey("12345678", Version(5,4,2), "YubiKey 5C NFC");
yubikey.addCredential(OathCredential("GitHub:user", "JBSWY3DPEHPK3PXP"));

testBus.startDaemon("/usr/bin/yubikey-oath-daemon");
OathManagerProxy manager(testBus.createConnection());
// ... test operations
```

**APDU Emulation:**
- Full OATH protocol support (SELECT, LIST, CALCULATE, CALCULATE_ALL, PUT, DELETE, VALIDATE, SET_CODE)
- Password protection (PBKDF2 derivation)
- Touch simulation (pending/simulate methods)
- Brand-specific quirks (YubiKey LIST bug, Nitrokey protocol differences)

**Completed Phases:**
1. ✅ **Phase 1:** Service layer tests (PasswordService, DeviceLifecycleService, CredentialService) - 3 tests, ~85% coverage
2. ✅ **Phase 2:** Storage tests (YubiKeyDatabase 19 cases, SecretStorage 10 cases) - 29 test cases, ~95% coverage
3. ⏭️ **Phase 3:** D-Bus object tests - **STRATEGICALLY SKIPPED** (covered via test_e2e_device_lifecycle, ~90% D-Bus coverage)

**Strategic Skip - Phase 3:**
- D-Bus objects (OathManagerObject, OathDeviceObject, OathCredentialObject) are thin marshaling layers
- Require concrete YubiKeyService (not interface) - refactoring cost >> value
- **Alternative:** test_e2e_device_lifecycle provides comprehensive D-Bus coverage with real integration
- **Result:** 85% coverage target achieved without Phase 3 unit tests

**Future Phases (Optional):**
4. UI tests (KRunner plugin, KCM, dialogs with Qt Test UI framework)
5. Additional OATH protocol tests (edge cases, error handling)

**Coverage Achievement:**
- **~85% line coverage, ~87% function coverage** ✅ **TARGET ACHIEVED**
- Critical components: Services (85%), Storage (95%), D-Bus (90% via E2E), Workflows (60%), Utilities (75%)

**Qt Resources in tests:** `QFile::exists()` may fail, use `iconPath.endsWith(".svg")`, allow fallback

## Debugging

**PC/SC:** `sudo systemctl status pcscd`, `pcsc_scan`

**D-Bus:** `busctl --user list | grep yubikey`, `busctl --user tree pl.jkolo.yubikey.oath.daemon`, `dbus-monitor --session "sender='pl.jkolo.yubikey.oath.daemon'"`

**Common issue:** "Could not register D-Bus service" → another daemon running: `pkill -9 yubikey-oath-daemon`, check `$DBUS_SESSION_BUS_ADDRESS`

## Dependencies

**Runtime:** Qt 6.7+ (Core, Widgets, Qml, Quick, QuickWidgets, Gui, DBus, Concurrent, Sql), KDE Frameworks 6.0+ (Runner, I18n, Config, ConfigWidgets, Notifications, CoreAddons, Wallet, KCMUtils, WidgetsAddons), PC/SC Lite, xkbcommon, libportal-qt6, KWayland, ZXing-C++

**Build-time only:** ImageMagick 7+ (for icon generation), optipng (optional, for icon optimization)

## Icon System

**Hicolor Theme Integration** (v2.0.0+): Icons follow freedesktop.org hicolor theme standard

**Installation:** Icons installed to `/usr/share/icons/hicolor/{SIZE}/devices/` during build
- **Sizes:** 16×16, 22×22, 32×32, 48×48, 64×64, 128×128, 256×256 (PNG), scalable (SVG)
- **Generated automatically:** ImageMagick script (`scripts/generate-icon-sizes.sh`) converts source 1000px PNGs → 7 standard sizes
- **Build integration:** CMake custom target `generate-icons` runs before compilation
- **Total files:** 85 icons (12 models × 7 sizes + 1 SVG generic)

**Naming convention:** `{brand}-{series}[{usb}][-{variant}][-nfc]` (no extension)
- Examples: `yubikey-5c-nfc`, `yubikey-5-nano`, `nitrokey-3c`, `yubikey-oath` (generic fallback)
- **No Qt resources:** All icons loaded via `QIcon::fromTheme()` - system handles size selection and fallback

**API:** `YubiKeyIconResolver::getIconName(deviceModel)` returns theme name (not path)

**Multi-brand support:** YubiKey (9 models), Nitrokey (3 models), extensible for additional brands

## Code Style

- **C++26**, modern Qt6/KF6, SOLID principles
- **QString literals:** MUST use `QStringLiteral()` (QT_NO_CAST_FROM_ASCII flag) - `if (name.contains(QStringLiteral("Nitrokey")))`
- **clang-tidy:** MUST pass zero errors
- **bugprone-branch-clone:** use fallthrough for identical branches
- **misc-const-correctness:** declare const if never modified
- **[[nodiscard]]:** MUST use on Result<T> methods and critical getters to prevent ignored return values
- **CMake only** with absolute paths (no make/ninja)
- Interface-based config (ConfigurationProvider), Result<T> pattern with [[nodiscard]], smart pointers

## Verify
```bash
cmake --build build-clang-debug -j$(nproc)  # zero errors/warnings
cd build-clang-debug && ctest --output-on-failure  # 27/28 pass (test_yubikey_proxy requires daemon)
```
