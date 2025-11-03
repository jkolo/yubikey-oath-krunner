# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

KRunner YubiKey OATH Plugin - A KDE Plasma 6 plugin that integrates YubiKey OATH TOTP/HOTP codes directly into KRunner. Users can search for accounts, copy codes to clipboard, or auto-type them using keyboard shortcuts.

The project consists of two main components:
1. **yubikey-oath-daemon** - Background D-Bus service for persistent YubiKey monitoring
2. **krunner_yubikey.so** - KRunner plugin that connects to daemon or uses direct PC/SC

## Build Commands

### Building Daemon and Plugin

The project builds both the daemon and KRunner plugin:

```bash
# From project root
mkdir -p build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
cmake --build . -j$(nproc)
sudo cmake --install .
```

### Verify Installation

```bash
# Check daemon binary
ls -la /usr/bin/yubikey-oath-daemon

# Check KRunner plugin
ls -la /usr/lib/qt6/plugins/kf6/krunner/krunner_yubikey.so

# Check KCM (settings) plugin
ls -la /usr/lib/qt6/plugins/kcm_krunner_yubikey.so
```

### Development Build

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build . -j$(nproc)
cmake --install .
```

### Quick Rebuild After Changes

```bash
cd build
cmake --build . -j$(nproc) && sudo cmake --install .
```

### Restart KRunner After Install

```bash
# Standard method - uses --replace flag to replace running instance
krunner --replace

# With detailed logging (logs go to stderr)
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true" \
QT_LOGGING_TO_CONSOLE=1 \
QT_FORCE_STDERR_LOGGING=1 \
krunner --replace 2>&1 | tee /tmp/krunner.log
```

**IMPORTANT**: Do NOT use `kquitapp6` or `killall`. The `--replace` flag automatically replaces the running instance.

### Running the Daemon

```bash
# Manual start with logging
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true" \
/usr/bin/yubikey-oath-daemon --verbose 2>&1 | tee /tmp/daemon.log

# Check if daemon is running
busctl --user list | grep yubikey

# Check daemon D-Bus service
busctl --user status pl.jkolo.yubikey.oath.daemon

# Interact with daemon
busctl --user call pl.jkolo.yubikey.oath.daemon /Device \
  pl.jkolo.yubikey.oath.daemon.Device ListDevices
```

## Architecture

### Source Code Organization

The source code is organized into clear component boundaries:

```
src/
├── shared/              # Shared components used by multiple modules
│   ├── common/          # Core utilities (result.h)
│   ├── dbus/            # D-Bus client and type conversions (used by krunner + config)
│   ├── types/           # Shared data structures (oath_credential.h, oath_credential_data.h)
│   ├── config/          # Shared configuration keys and providers
│   ├── ui/              # Shared UI components (password_dialog)
│   ├── utils/           # Shared utilities (deferred_execution)
│   ├── po/              # Project translations
│   │   ├── CMakeLists.txt
│   │   ├── krunner_yubikey.pot
│   │   └── pl.po
│   └── resources/       # Shared resources (icons)
│       ├── shared.qrc   # Qt resource file for shared assets
│       └── yubikey.svg  # Application icon
│
├── daemon/              # D-Bus daemon service
│   ├── main.cpp, yubikey_dbus_service.{h,cpp}
│   ├── logging_categories.{h,cpp}  # Daemon-specific logging
│   ├── services/        # Business logic layer (YubiKeyService)
│   ├── actions/         # Action coordination and execution
│   ├── config/          # Daemon configuration (DaemonConfiguration)
│   ├── workflows/       # Touch workflow coordination, notification orchestration
│   ├── clipboard/       # Clipboard management
│   ├── input/           # Text input emulation (X11, Wayland, Portal)
│   ├── notification/    # D-Bus notification manager
│   ├── formatting/      # Code validation
│   ├── ui/              # UI components (add_credential_dialog)
│   ├── utils/           # Daemon utilities (async_waiter, qr/otpauth parsers, screenshot_capture)
│   ├── oath/            # OATH protocol implementation
│   ├── storage/         # Database and KWallet
│   └── pcsc/            # PC/SC card reader monitoring
│
├── krunner/             # KRunner plugin (lightweight - delegates to daemon)
│   ├── yubikeyrunner.{h,cpp}  # Entry point (root level)
│   ├── logging_categories.{h,cpp}  # KRunner-specific logging
│   ├── actions/         # Action management (ActionManager)
│   ├── config/          # Configuration management (KRunnerConfiguration)
│   ├── matching/        # Match building (MatchBuilder)
│   └── formatting/      # Credential formatting (CredentialFormatter)
│
└── config/              # KCM configuration module
    ├── yubikey_config.{h,cpp}, yubikey_device_model.{h,cpp}
    ├── logging_categories.{h,cpp}  # Config-specific logging
    └── resources/       # Config module resources
        ├── config.qrc   # Qt resource file for QML UI
        └── ui/          # QML UI files
            └── YubiKeyConfig.qml
```

**Key principles:**
- **Component isolation**: Each module (daemon, krunner, config) has its own subdirectory
- **Shared resources**: Common code and assets in `shared/` to avoid duplication
- **Daemon-centric architecture**: Most business logic moved to daemon; krunner is now lightweight
- **Resource organization**: Icons in shared/, QML UI in config/resources/
- **Configuration keys**: Shared configuration keys in `shared/config/configuration_keys.h`
- **Logical grouping**: Files organized into subdirectories by function
- **Entry point visibility**: Main entry points (like `yubikeyrunner.cpp`) stay at module root level
- **Local logging**: Each module has its own `logging_categories.{h,cpp}`
- **Translations centralized**: All translation files in `src/shared/po/`

### D-Bus Service Layer (Daemon)

The daemon uses a **layered architecture** separating D-Bus marshaling from business logic:

```
YubiKeyDBusService (D-Bus marshaling - thin layer)
    ↓ delegates
YubiKeyService (business logic layer)
    ↓ uses
Components (DeviceManager, Database, PasswordStorage, ActionCoordinator)
```

**YubiKeyDBusService** (`src/daemon/yubikey_dbus_service.{h,cpp}`)
- **THIN D-Bus marshaling layer** (~135 lines)
- Standalone daemon binary: `yubikey-oath-daemon`
- Creates and manages YubiKeyManagerObject (hierarchical D-Bus architecture)
- **Legacy interface** (backward compatibility):
  - D-Bus interface: `pl.jkolo.yubikey.oath.daemon.Device`
  - Object path: `/Device`
  - Provides methods: `ListDevices`, `GetCredentials`, `GenerateCode`, `SavePassword`, `ForgetDevice`, `SetDeviceName`, `AddCredential`, `CopyCodeToClipboard`, `TypeCode`, `AddCredentialFromScreen`
  - Emits signals: `DeviceConnected`, `DeviceDisconnected`, `CredentialsUpdated`
- Single responsibility: Convert D-Bus types ↔ internal types and delegate to YubiKeyService
- **NO business logic** - pure delegation to YubiKeyService
- Uses TypeConversions utility for D-Bus type conversions

**Hierarchical D-Bus Architecture** (NEW in v1.0)
The daemon now exposes a 3-level hierarchical D-Bus object tree following best practices:

```
/pl/jkolo/yubikey/oath (Manager)
  └─ /devices/<deviceId> (Device)
      └─ /credentials/<credentialId> (Credential)
```

**YubiKeyManagerObject** (`src/daemon/dbus/yubikey_manager_object.{h,cpp}`)
- **Manager object** at path `/pl/jkolo/yubikey/oath`
- Interfaces:
  - `pl.jkolo.yubikey.oath.Manager` (application interface)
  - `org.freedesktop.DBus.ObjectManager` (discovery interface)
- **Minimalist design following D-Bus best practices:**
  - Only ONE application property: `Version` (string): "1.0"
  - NO aggregated properties (DeviceCount, Devices, TotalCredentials, Credentials) - removed for simplicity
  - Clients use `GetManagedObjects()` to discover hierarchy and calculate aggregates locally
- Methods: `GetManagedObjects()` - returns complete device/credential hierarchy
- Signals: `InterfacesAdded`, `InterfacesRemoved` (ObjectManager pattern)
- Automatically creates/destroys Device objects on YubiKey connection/disconnection
- **Architecture rationale:** Pure ObjectManager without redundant cached properties reduces code complexity and follows freedesktop.org standards

**YubiKeyDeviceObject** (`src/daemon/dbus/yubikey_device_object.{h,cpp}`)
- **Device objects** at path `/pl/jkolo/yubikey/oath/devices/<deviceId>`
- Interface: `pl.jkolo.yubikey.oath.Device`
- Methods:
  - `SavePassword(password: string) → bool`
  - `ChangePassword(oldPassword: string, newPassword: string) → bool`
  - `Forget()` - Removes device from database and KWallet
  - `AddCredential(name, secret, type, algorithm, digits, period, counter, requireTouch) → (status, pathOrMessage)`
- Properties:
  - `Name` (string, writable): Custom device name
  - `DeviceId` (string): Hex device ID
  - `IsConnected` (bool): Connection status
  - `RequiresPassword` (bool): Password requirement
  - `HasValidPassword` (bool): Valid password availability
  - **Note:** CredentialCount and Credentials properties removed - use parent Manager's GetManagedObjects()
- Signals: `CredentialAdded`, `CredentialRemoved`
- Automatically creates/destroys Credential objects on credential updates
- **Minimalist design:** Follows D-Bus best practices by not duplicating credential data in properties

**YubiKeyCredentialObject** (`src/daemon/dbus/yubikey_credential_object.{h,cpp}`)
- **Credential objects** at path `/pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>`
- Interface: `pl.jkolo.yubikey.oath.Credential`
- Methods:
  - `GenerateCode() → (code: string, validUntil: int64)`
  - `CopyToClipboard() → bool`
  - `TypeCode(fallbackToCopy: bool) → bool` - Types code with optional fallback to clipboard
  - `Delete()` - Removes credential from YubiKey
- Properties (all read-only):
  - `Name` (string): Full credential name (issuer:username)
  - `Issuer` (string): Service issuer
  - `Username` (string): Account username
  - `Type` (string): "TOTP" or "HOTP"
  - `Algorithm` (string): "SHA1", "SHA256", or "SHA512"
  - `Digits` (int): Number of digits (6-8)
  - `Period` (int): TOTP period in seconds
  - `RequiresTouch` (bool): Physical touch requirement
  - `DeviceId` (string): Parent device ID

**D-Bus XML Interface Definitions:**
- `src/daemon/dbus/pl.jkolo.yubikey.oath.Manager.xml`
- `src/daemon/dbus/pl.jkolo.yubikey.oath.Device.xml`
- `src/daemon/dbus/pl.jkolo.yubikey.oath.Credential.xml`

**Credential ID Encoding:**
- Credential names are URL-encoded for D-Bus path compatibility
- `%` replaced with `_`, converted to lowercase
- Names starting with digit: prepended with 'c'
- Very long names (>200 chars): hashed with SHA256

**Backward Compatibility:**
- Legacy `/Device` interface remains available
- YubiKeyDBusClient continues to use legacy interface
- Both interfaces work simultaneously

**YubiKeyService** (`src/daemon/services/yubikey_service.{h,cpp}`)
- **Business logic layer** (~430 lines)
- Aggregates and coordinates all daemon components
- Owns: YubiKeyDeviceManager, YubiKeyDatabase, PasswordStorage, DaemonConfiguration, YubiKeyActionCoordinator
- Implements all business logic for device management, credential operations, password handling
- Handles device lifecycle events (connection, disconnection, credential updates)
- Provides action coordinator for copy/type operations
- Emits signals forwarded to D-Bus layer
- **Note:** Authentication failures are handled internally - empty credential list indicates auth failure, no separate signal emitted

**Client-Side Proxy Architecture** (New in v1.0)

The client side now uses an object-oriented proxy architecture instead of a flat facade:

**YubiKeyManagerProxy** (`src/shared/dbus/yubikey_manager_proxy.{h,cpp}`)
- **Singleton manager proxy** for daemon communication
- Represents `/pl/jkolo/yubikey/oath` (Manager D-Bus object)
- Uses ObjectManager pattern: `GetManagedObjects()` for auto-discovery
- Monitors daemon lifecycle with QDBusServiceWatcher
- Owns YubiKeyDeviceProxy objects (Qt parent-child)
- Methods:
  - `static instance()` - singleton access
  - `devices()` - all device proxies
  - `getDevice(deviceId)` - specific device proxy
  - `getAllCredentials()` - aggregated from all devices
  - `isDaemonAvailable()` - daemon status
- Signals:
  - `deviceConnected(YubiKeyDeviceProxy*)`
  - `deviceDisconnected(QString)`
  - `credentialsChanged()`
  - `daemonAvailable()` / `daemonUnavailable()`
- Auto-creates/destroys device proxies on InterfacesAdded/Removed

**YubiKeyDeviceProxy** (`src/shared/dbus/yubikey_device_proxy.{h,cpp}`)
- Represents `/pl/jkolo/yubikey/oath/devices/<deviceId>` (Device D-Bus object)
- Owns YubiKeyCredentialProxy objects (Qt parent-child)
- Cached properties: deviceId, name, isConnected, requiresPassword, hasValidPassword
- Methods:
  - `credentials()` - all credential proxies
  - `getCredential(name)` - specific credential proxy
  - `savePassword()`, `changePassword()`, `forget()`, `addCredential()`, `setName()`
  - `toDeviceInfo()` - converts to value type
- Signals:
  - `credentialAdded(YubiKeyCredentialProxy*)`
  - `credentialRemoved(QString)`
  - `nameChanged(QString)`, `connectionChanged(bool)`
- Auto-creates/destroys credential proxies on CredentialAdded/Removed

**YubiKeyCredentialProxy** (`src/shared/dbus/yubikey_credential_proxy.{h,cpp}`)
- Represents `/pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>` (Credential D-Bus object)
- Cached properties: name, issuer, username, requiresTouch, type, algorithm, digits, period, deviceId
- Methods:
  - `generateCode()` - generates TOTP/HOTP code
  - `copyToClipboard()` - copies code to clipboard
  - `typeCode(fallbackToCopy)` - types code via input emulation
  - `deleteCredential()` - deletes from YubiKey
  - `toCredentialInfo()` - converts to value type
- All properties are const (credentials are immutable)

**YubiKeyDBusClient** - **REMOVED**
- ~~Legacy flat D-Bus client previously used by AddCredentialWorkflow~~
- **REMOVED in 2025-10-27 refactoring** - no longer used anywhere in codebase
- All components now use proxy architecture (YubiKeyManagerProxy)

**Value Types** (`src/shared/types/yubikey_value_types.{h,cpp}`)
- Data transfer objects: DeviceInfo, CredentialInfo, GenerateCodeResult, AddCredentialResult
- D-Bus marshaling operators for passing over D-Bus
- Conversion methods: `proxy->toDeviceInfo()`, `proxy->toCredentialInfo()`
- Located in `shared/types/` (not `shared/dbus/` - they're value objects, not D-Bus specific)

**Daemon Architecture Pattern:**
- Daemon runs as separate process for persistent YubiKey monitoring
- KRunner plugin connects via D-Bus for on-demand operations
- Separation enables background YubiKey detection and credential caching
- **IMPORTANT:** Daemon must successfully register D-Bus service or it exits immediately

**Daemon Lifecycle:**

1. **Startup:**
   - Daemon starts and registers D-Bus service `pl.jkolo.yubikey.oath.daemon`
   - YubiKeyDBusService creates YubiKeyService
   - YubiKeyService initializes: YubiKeyDeviceManager, YubiKeyDatabase, PasswordStorage, DaemonConfiguration, YubiKeyActionCoordinator
   - YubiKeyDeviceManager starts PC/SC card reader monitoring
   - Detects already-connected YubiKeys
   - YubiKeyService emits `DeviceConnected` (forwarded to D-Bus by YubiKeyDBusService)

2. **Runtime:**
   - YubiKeyDeviceManager monitors for YubiKey insertion/removal via PC/SC
   - YubiKeyService emits `DeviceConnected`/`DeviceDisconnected` signals (forwarded to D-Bus)
   - YubiKeyOathDevice maintains credential cache per device
   - YubiKeyService handles authentication (loads passwords from KWallet)
   - Background credential fetching via YubiKeyOathDevice

3. **KRunner Integration:**
   - YubiKeyRunner plugin connects to daemon via D-Bus
   - Uses YubiKeyManagerProxy singleton for daemon communication
   - Proxy architecture auto-discovers devices/credentials via ObjectManager
   - Direct method calls on proxy objects (device->savePassword(), credential->generateCode())
   - Manager proxy monitors daemon lifecycle and auto-refreshes on reconnect

### Core Components

**YubiKeyRunner** (`src/krunner/yubikeyrunner.{h,cpp}`)
- Main KRunner plugin entry point
- Orchestrates all components
- Handles search queries and result matching
- Uses YubiKeyManagerProxy::instance() singleton for daemon communication
- Components: KRunnerConfiguration, ActionManager, MatchBuilder
- match() - searches credentials via manager->getAllCredentials()
- run() - executes actions via credential proxy methods (typeCode(), copyToClipboard())
- Connects to manager signals: deviceConnected, deviceDisconnected, credentialsChanged

**YubiKeyDeviceManager** (`src/oath/yubikey_device_manager.{h,cpp}`)
- Manages lifecycle of multiple YubiKey devices
- PC/SC context management (shared by all devices)
- Hot-plug detection via CardReaderMonitor
- Device connection/disconnection coordination
- Credential aggregation from multiple devices via `getCredentials()`
- **Direct access to devices via `getDevice(deviceId)`**
- **Clients should get device instance and call methods directly**
- Signal forwarding for multi-device monitoring

**YubiKeyOathDevice** (`src/oath/yubikey_oath_device.{h,cpp}`)
- Per-device OATH operations wrapper
- Manages device-specific card handle and protocol
- Handles credential caching for each device
- Thread-safe background credential fetching
- Async credential updates via QFuture
- Delegates all OATH protocol operations to OathSession

**OathSession** (`src/oath/oath_session.{h,cpp}`)
- Handles full OATH protocol communication with single YubiKey
- PC/SC I/O operations with chained response handling
- High-level OATH operations (select, list, calculate, authenticate)
- Business logic (PBKDF2 key derivation, HMAC authentication)
- Uses OathProtocol for command building and response parsing
- Does NOT own SCARDHANDLE (caller retains ownership)
- NOT thread-safe (caller must serialize with mutex)

**OathProtocol** (`src/oath/oath_protocol.{h,cpp}`)
- Stateless utility class for OATH protocol
- All protocol constants (INS_*, SW_*, TAG_*, OATH_AID)
- Static command creation methods (createSelectCommand, etc.)
- Static response parsing methods (parseSelectResponse, etc.)
- Helper functions (TLV parsing, TOTP counter calculation)
- No state, no I/O - pure functions only

**OATH Protocol Architecture**:
```
YubiKeyDeviceManager (multi-device lifecycle)
    ↓ creates
YubiKeyOathDevice (per-device state, async, caching)
    ↓ owns
OathSession (PC/SC I/O + OATH operations)
    ↓ uses
OathProtocol (static utility functions)
```

**NotificationOrchestrator** (`src/krunner/workflows/notification_orchestrator.{h,cpp}`)
- Manages all notification types (code, touch, errors)
- Implements countdown timers with progress bars
- Uses DBusNotificationManager for direct D-Bus communication
- Handles notification updates every second with `updateNotification()`
- Touch notifications: managed timeout with manual countdown (bypasses server 10-second limit)
- Code notifications: shows expiration countdown with progress bar

**TouchWorkflowCoordinator** (`src/krunner/workflows/touch_workflow_coordinator.{h,cpp}`)
- Coordinates YubiKey touch requirement workflow
- Shows touch notification, polls for completion
- Executes action (copy/type) after successful touch
- **Must show code notification after copy action** (similar to yubikeyrunner.cpp)

**ActionExecutor** (`src/krunner/actions/action_executor.{h,cpp}`)
- Executes copy and type actions
- Returns ActionResult::Success/Failure
- Used by both YubiKeyRunner and TouchWorkflowCoordinator
- Integrates with ClipboardManager and TextInputFactory

**YubiKeyActionCoordinator** (`src/daemon/actions/yubikey_action_coordinator.{h,cpp}`)
- **Daemon-side action coordinator** (high-level orchestration)
- Coordinates YubiKey actions: copy, type, add credential
- Checks touch requirements before executing actions
- Delegates to TouchWorkflowCoordinator for touch-required credentials
- Delegates to ActionExecutor for direct action execution
- Aggregates: ActionExecutor, TouchWorkflowCoordinator, ClipboardManager, TextInputProvider, NotificationOrchestrator
- **DRY implementation**: `executeActionInternal()` handles common logic for copy/type
- Used by YubiKeyService to execute D-Bus action requests

**ActionManager** (`src/krunner/actions/action_manager.{h,cpp}`)
- Manages KRunner actions (copy, type)
- Primary action selection based on configuration
- Action metadata management (icons, text, shortcuts)

**MatchBuilder** (`src/krunner/matching/match_builder.{h,cpp}`)
- Creates KRunner::QueryMatch objects from proxy objects
- buildCredentialMatch(YubiKeyCredentialProxy*, query, YubiKeyManagerProxy*)
- Generates code directly via credentialProxy->generateCode()
- Gets device information from manager->devices()
- Calculates relevance scores
- Builds password error matches from DeviceInfo
- Applies credential formatting with device names

**TouchHandler** (`src/krunner/workflows/touch_handler.{h,cpp}`)
- Manages touch requirement detection
- Timeout configuration
- Signal emission for touch events
- Coordinates with TouchWorkflowCoordinator
- Located in `workflows/` as it's part of the touch workflow orchestration

**ClipboardManager** (`src/clipboard/clipboard_manager.{h,cpp}`)
- Secure clipboard operations
- Auto-clear support for security
- Integration with QClipboard

### Input System (Multi-Platform)

**TextInputFactory** (`src/input/text_input_factory.{h,cpp}`)
- Creates appropriate input provider for current environment
- Priority: Portal → X11
- Factory pattern for platform abstraction

**X11TextInput** (`src/input/x11_text_input.{h,cpp}`)
- Uses XTest extension for X11
- Direct keyboard simulation
- Character-by-character typing

**PortalTextInput** (`src/input/portal_text_input.{h,cpp}`)
- Uses libportal for RemoteDesktop portal session management
- Uses xdp_session_keyboard_key() API for keyboard emulation
- Works across all Wayland compositors (KDE Plasma, GNOME, Sway, Hyprland)
- Supports Wayland via xdg-desktop-portal RemoteDesktop interface
- Clean, dependency-minimal implementation (no libei/liboeffis)
- Requires user permission grants on first use
- Async session creation with GLib callbacks and Qt QEventLoop
- Uses Linux evdev keycodes for key events

### Storage and Configuration

**YubiKeyDatabase** (`src/daemon/storage/yubikey_database.{h,cpp}`)
- SQLite database for device persistence
- Stores device metadata (name, serial, requires_password flag)
- Path: `~/.local/share/krunner-yubikey/devices.db`
- **Database schema**:
  - `devices` table: device_id (PRIMARY KEY), device_name, requires_password
  - `credentials` table: id (PRIMARY KEY), device_id (FOREIGN KEY), original_name, issuer, account, type, algorithm, digits, period, requires_touch
  - Foreign key constraint: `credentials.device_id` → `devices.device_id` with ON DELETE CASCADE
- **Input validation**: Device IDs validated as 16-character hex strings before SQL operations
- **Transaction safety**: Uses TransactionGuard for RAII-based transaction management
- Thread-safe database operations

**TransactionGuard** (`src/daemon/storage/transaction_guard.{h,cpp}`)
- **RAII pattern** for database transaction management
- Automatically rolls back transaction in destructor if not committed
- Exception-safe transaction handling
- Usage:
  ```cpp
  TransactionGuard guard(m_db);
  if (!guard.isValid()) return false;

  // Perform database operations
  if (!operation1()) return false;  // Auto-rollback on early return
  if (!operation2()) return false;  // Auto-rollback on early return

  return guard.commit();  // Explicit commit
  ```
- Replaces manual begin/commit/rollback pattern
- Added in v1.1.0

**CredentialCacheSearcher** (`src/daemon/cache/credential_cache_searcher.{h,cpp}`)
- **Single Responsibility**: Search database cache for credentials in offline devices
- Extracted from YubiKeyActionCoordinator to follow SRP
- Searches cached credentials when YubiKey is disconnected
- Respects configuration: only searches if cache is enabled
- Search algorithm:
  1. Check if cache is enabled in configuration
  2. If device hint provided, search only that device (if offline)
  3. Otherwise, search all offline devices in database
  4. Return first matching device ID or nullopt
- Used by YubiKeyActionCoordinator for reconnect workflow
- Added in v1.1.0

**PasswordStorage** (`src/daemon/storage/secret_storage.{h,cpp}`)
- Stores YubiKey passwords in KWallet
- Per-device storage using device ID as key
- Folder: "YubiKey OATH Application"
- Secure password retrieval and storage

**KRunnerConfiguration** (`src/krunner/config/krunner_configuration.{h,cpp}`)
- Implements ConfigurationProvider interface
- Reads settings from krunnerrc file
- Configuration change notifications

### KCM (Settings Module)

**YubiKeyConfig** (`src/config/yubikey_config.{h,cpp}`)
- KCModule for System Settings integration
- Separate plugin: `kcm_krunner_yubikey.so`
- **Must include Qt resources** via `qt6_add_resources()` in CMakeLists.txt
- Correct module name: `kcm_krunner_yubikey` (not `kcm_yubikey`)
- Uses YubiKeyManagerProxy::instance() singleton
- Device management UI via YubiKeyDeviceModel

**YubiKeyDeviceModel** (`src/config/yubikey_device_model.{h,cpp}`)
- QAbstractListModel for device list
- Uses YubiKeyManagerProxy for daemon communication
- refreshDevices() - converts device proxies to DeviceInfo for QML
- testAndSavePassword() - uses deviceProxy->savePassword()
- forgetDevice() - uses deviceProxy->forget()
- setDeviceName() - uses deviceProxy->setName()
- Connects to manager signals: deviceConnected, deviceDisconnected, credentialsChanged
- Real-time device status updates

**YubiKeyConfig.qml** (`src/config/resources/ui/YubiKeyConfig.qml`)
- QML UI for configuration
- Settings: notifications, display format, touch timeout
- Embedded via Qt resources: `qrc:/qml/config/YubiKeyConfig.qml`
- Icon path: `:/icons/yubikey.svg` (from shared resources)
- Device password management
- **Resource initialization**: Config module must call both `qInitResources_shared()` and `qInitResources_config()` to load all resources

### PC/SC Layer

**CardReaderMonitor** (`src/pcsc/card_reader_monitor.{h,cpp}`)
- Monitors PC/SC reader status
- Emits signals on card insertion/removal
- Background thread for polling
- Uses `SCardGetStatusChange` for efficient monitoring

## Key Design Patterns

### Builder Pattern - FormatOptionsBuilder

**FormatOptionsBuilder** (`src/shared/formatting/credential_formatter.h`)
- **Fluent API** for constructing FormatOptions with readable, self-documenting code
- Replaces error-prone 6-parameter constructor
- Added in v1.1.0

**Preferred Usage:**
```cpp
FormatOptions options = FormatOptionsBuilder()
    .withUsername()
    .withDevice(deviceName)
    .withDeviceCount(connectedDeviceCount)
    .onlyWhenMultipleDevices()
    .build();
```

**Legacy constructor** (deprecated but still supported for backward compatibility):
```cpp
FormatOptions options(showUsername, showCode, showDevice, deviceName, deviceCount, onlyMultiple);
```

**Benefits:**
- Improved readability - each option is explicitly named
- Self-documenting code - no need to remember parameter order
- Easier to maintain - adding new options doesn't break existing code
- Type-safe - compiler catches missing required parameters

**Available methods:**
- `withUsername(bool = true)` - Show username in parentheses
- `withCode(bool = true)` - Show TOTP/HOTP code if available
- `withDevice(QString name, bool = true)` - Show device name
- `withDeviceCount(int count)` - Set number of connected devices
- `onlyWhenMultipleDevices(bool = true)` - Only show device when multiple devices connected
- `build()` - Build and return FormatOptions instance

### RAII Pattern - TransactionGuard

**TransactionGuard** ensures database transaction safety through RAII:
- Constructor begins transaction
- Destructor automatically rolls back if not committed
- Eliminates manual rollback on error paths
- Exception-safe by design

**Usage:**
```cpp
TransactionGuard guard(m_db);
if (!guard.isValid()) return false;

// All early returns automatically trigger rollback
if (!deleteOldCredentials(deviceId)) return false;
if (!insertNewCredentials(deviceId, credentials)) return false;

return guard.commit(); // Explicit commit
```

**Replaced pattern:**
```cpp
// Old manual pattern (error-prone):
m_db.transaction();
if (!op1()) { m_db.rollback(); return false; }
if (!op2()) { m_db.rollback(); return false; }
m_db.commit();
```

### Notification Management
- All notifications use `timeout = 0` (no auto-close)
- Manual countdown with QTimer (1 second intervals)
- Use `updateNotification()` NOT `showNotification()` for updates
- Touch notifications update body text with remaining seconds
- Code notifications update progress bar hint value

### Touch Workflow
1. Show touch notification with countdown
2. Poll for code generation (500ms intervals)
3. On success: close notification, execute action, show code notification (for copy)
4. On timeout: close notification, cancel operation

### Icon and Resource Usage
- **Icon path**: Always use `:/icons/yubikey.svg` for consistency
- **Icon location**: `src/shared/resources/yubikey.svg`
- Required in: match results, notifications, password errors, config UI
- **Resource files**:
  - `shared.qrc` - Contains icon, used by both krunner and config modules
  - `config.qrc` - Contains QML UI files, used only by config module
- **CMakeLists.txt**: Config module must include both resource files via `qt6_add_resources()`
- **Code initialization**: Config module must call both `qInitResources_shared()` and `qInitResources_config()` in constructor

### Error Handling
- Password errors show special match that opens settings
- Touch timeout emits signal to TouchHandler
- D-Bus notification failures degrade gracefully
- Result<T> pattern for type-safe error handling

## Logging

The project uses Qt Logging Categories for selective, component-based logging control. Logging categories are organized per-module:
- **Daemon**: `src/daemon/logging_categories.{h,cpp}`
- **KRunner**: `src/krunner/logging_categories.{h,cpp}`
- **Config**: `src/config/logging_categories.{h,cpp}`

### Available Logging Categories

**Daemon Components** (`src/daemon/logging_categories.{h,cpp}`):
- `YubiKeyDaemonLog` - Daemon service (`pl.jkolo.yubikey.oath.daemon.daemon`)
- `YubiKeyDeviceManagerLog` - Device manager (`pl.jkolo.yubikey.oath.daemon.manager`)
- `YubiKeyOathDeviceLog` - Per-device OATH operations (`pl.jkolo.yubikey.oath.daemon.oath.device`)
- `CardReaderMonitorLog` - PC/SC monitoring (`pl.jkolo.yubikey.oath.daemon.pcsc`)
- `PasswordStorageLog` - KWallet storage (`pl.jkolo.yubikey.oath.daemon.storage`)
- `YubiKeyDatabaseLog` - Database operations (`pl.jkolo.yubikey.oath.daemon.database`)

**KRunner Components** (`src/krunner/logging_categories.{h,cpp}`):
- `YubiKeyRunnerLog` - Main KRunner plugin (`pl.jkolo.yubikey.oath.daemon.runner`)
- `NotificationOrchestratorLog` - Notification management (`pl.jkolo.yubikey.oath.daemon.notification`)
- `TouchWorkflowCoordinatorLog` - Touch workflow (`pl.jkolo.yubikey.oath.daemon.touch`)
- `ActionExecutorLog` - Action execution (`pl.jkolo.yubikey.oath.daemon.action`)
- `MatchBuilderLog` - Match building (`pl.jkolo.yubikey.oath.daemon.match`)
- `TextInputLog` - Input emulation (`pl.jkolo.yubikey.oath.daemon.input`)
- `DBusNotificationLog` - D-Bus notifications (`pl.jkolo.yubikey.oath.daemon.dbus`)

**Config Module** (`src/config/logging_categories.{h,cpp}`):
- `YubiKeyConfigLog` - Settings module (`pl.jkolo.yubikey.oath.daemon.config`)

### Usage in Code

**In daemon files:**
```cpp
#include "logging_categories.h"  // or "../daemon/logging_categories.h"

qCDebug(YubiKeyDaemonLog) << "Debug information";
qCWarning(YubiKeyDeviceManagerLog) << "Warning message";
```

**In krunner files:**
```cpp
#include "logging_categories.h"  // or "../logging_categories.h" from subdirs

qCDebug(YubiKeyRunnerLog) << "Debug information";
qCWarning(ActionExecutorLog) << "Warning message";
```

**In config files:**
```cpp
#include "logging_categories.h"

qCDebug(YubiKeyConfigLog) << "Debug information";
```

**IMPORTANT**:
- Always use the category name with "Log" suffix (e.g., `YubiKeyRunnerLog`, not `YubiKeyRunner`) to avoid conflicts with class names
- Include path depends on file location: use relative paths (`logging_categories.h` at same level, or `../logging_categories.h` from subdirectories)

### Enabling Logging at Runtime

```bash
# Enable all YubiKey logging
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true" krunner --replace

# Enable specific components
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.runner=true;pl.jkolo.yubikey.oath.daemon.oath=true" krunner --replace

# Enable with different log levels
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*.debug=true" krunner --replace

# Disable specific component
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true;pl.jkolo.yubikey.oath.daemon.dbus=false" krunner --replace
```

### Persistent Logging Configuration

Create `~/.config/QtProject/qtlogging.ini`:
```ini
[Rules]
pl.jkolo.yubikey.oath.daemon.*=true
pl.jkolo.yubikey.oath.daemon.oath.debug=true
```

## Testing

### Test Structure

The project has comprehensive unit tests:

- `test_result.cpp` - Result<T> template tests
- `test_oath_protocol.cpp` - OATH protocol tests (23 tests, 94.8% coverage)
- `test_action_manager.cpp` - Action manager tests (11 tests, 78.9% coverage)
- `test_display_strategies.cpp` - Display strategies tests (21 tests, 100% coverage)
- `test_code_validator.cpp` - Code validation tests (100% coverage)
- `test_credential_formatter.cpp` - Display formatting tests
- `test_match_builder.cpp` - KRunner match building tests
- `mocks/mock_configuration_provider.cpp` - Mock for configuration testing

### Running Tests

```bash
# Build and run all tests
cd build
ctest --output-on-failure

# Run individual test suites
./tests/test_result              # Result<T> tests
./tests/test_oath_protocol       # OATH protocol tests
./tests/test_action_manager      # Action manager tests
./tests/test_display_strategies  # Display strategies tests
./tests/test_code_validator      # Validation tests
./tests/test_credential_formatter # Formatting tests
./tests/test_match_builder       # Match building tests
```

### Code Coverage

```bash
# Build with coverage
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build .
ctest

# Generate coverage report
lcov --capture --directory . --output-file coverage.info
lcov --remove coverage.info '/usr/*' --output-file coverage.info
lcov --list coverage.info

# HTML report
genhtml coverage.info --output-directory coverage_html
```

### Test Coverage Statistics

Current coverage (as of v1.0.0):
- **Lines:** 58.0% (738/1272)
- **Functions:** 65.3% (118/180)
- **Test Suites:** 8 (all passing)
- **Individual Tests:** 100+

Components with excellent coverage:
- `oath_protocol.cpp`: 94.8% lines, 100% functions
- Display strategies: 100% coverage (all strategies)
- `action_manager.cpp`: 78.9% lines, 100% functions
- `code_validator.cpp`: 100% coverage
- `result.h`: 100% coverage

See [COVERAGE.md](COVERAGE.md) for detailed coverage analysis.

## Common Debugging

### Enable Detailed Logging

```bash
# All components - KRunner
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true" \
QT_LOGGING_TO_CONSOLE=1 \
QT_FORCE_STDERR_LOGGING=1 \
krunner --replace 2>&1 | tee /tmp/krunner_debug.log

# All components - Daemon
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true" \
QT_LOGGING_TO_CONSOLE=1 \
QT_FORCE_STDERR_LOGGING=1 \
/usr/bin/yubikey-oath-daemon --verbose 2>&1 | tee /tmp/daemon_debug.log

# Specific components for targeted debugging
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.runner=true;pl.jkolo.yubikey.oath.daemon.oath=true;pl.jkolo.yubikey.oath.daemon.notification=true" \
QT_LOGGING_TO_CONSOLE=1 \
QT_FORCE_STDERR_LOGGING=1 \
krunner --replace 2>&1 | tee /tmp/krunner_debug.log
```

### Check Plugin Installation

```bash
ls -la /usr/lib/qt6/plugins/kf6/krunner/krunner_yubikey.so
ls -la /usr/lib/qt6/plugins/kcm_krunner_yubikey.so
ls -la /usr/bin/yubikey-oath-daemon
```

### Verify PC/SC Daemon

```bash
sudo systemctl status pcscd
pcsc_scan  # Should detect YubiKey
```

### D-Bus Service Debugging

**Legacy Interface (backward compatibility):**

```bash
# Check if daemon D-Bus service is registered
busctl --user list | grep yubikey

# Get service status
busctl --user status pl.jkolo.yubikey.oath.daemon

# Test daemon connectivity (legacy interface)
busctl --user call pl.jkolo.yubikey.oath.daemon /Device \
  pl.jkolo.yubikey.oath.daemon.Device ListDevices

# Monitor D-Bus signals from daemon
dbus-monitor --session "sender='pl.jkolo.yubikey.oath.daemon'"

# Introspect legacy interface
busctl --user introspect pl.jkolo.yubikey.oath.daemon /Device
```

**Hierarchical D-Bus Architecture (v1.0):**

```bash
# View complete object tree (3-level hierarchy)
busctl --user tree pl.jkolo.yubikey.oath.daemon

# Introspect Manager object
busctl --user introspect pl.jkolo.yubikey.oath.daemon /pl/jkolo/yubikey/oath

# Get Manager property (only Version is available - other data via GetManagedObjects)
busctl --user get-property pl.jkolo.yubikey.oath.daemon \
  /pl/jkolo/yubikey/oath \
  pl.jkolo.yubikey.oath.Manager Version

# Get all managed objects (ObjectManager pattern)
busctl --user call pl.jkolo.yubikey.oath.daemon \
  /pl/jkolo/yubikey/oath \
  pl.jkolo.yubikey.oath.Manager GetManagedObjects

# Introspect specific Device object
busctl --user introspect pl.jkolo.yubikey.oath.daemon \
  /pl/jkolo/yubikey/oath/devices/28b5c0b54ccb10db

# Get Device properties
busctl --user get-property pl.jkolo.yubikey.oath.daemon \
  /pl/jkolo/yubikey/oath/devices/28b5c0b54ccb10db \
  pl.jkolo.yubikey.oath.Device Name

# Introspect specific Credential object
busctl --user introspect pl.jkolo.yubikey.oath.daemon \
  /pl/jkolo/yubikey/oath/devices/28b5c0b54ccb10db/credentials/github_3ajkolo

# Generate TOTP code via Credential object
busctl --user call pl.jkolo.yubikey.oath.daemon \
  /pl/jkolo/yubikey/oath/devices/28b5c0b54ccb10db/credentials/github_3ajkolo \
  pl.jkolo.yubikey.oath.Credential GenerateCode

# Type code with fallback to clipboard
busctl --user call pl.jkolo.yubikey.oath.daemon \
  /pl/jkolo/yubikey/oath/devices/28b5c0b54ccb10db/credentials/github_3ajkolo \
  pl.jkolo.yubikey.oath.Credential TypeCode b true

# Monitor ObjectManager signals (device/credential additions/removals)
dbus-monitor --session "sender='pl.jkolo.yubikey.oath.daemon',interface='pl.jkolo.yubikey.oath.Manager'"
```

### Common D-Bus Issues

**Daemon exits immediately with "Could not register D-Bus service: ""**

This indicates D-Bus service registration failed. Possible causes:
- Another instance of daemon already running
- D-Bus session bus not accessible
- Permissions issue with D-Bus configuration

```bash
# Kill any existing daemon instances
pkill -9 yubikey-oath-daemon

# Check D-Bus session
echo $DBUS_SESSION_BUS_ADDRESS

# Try starting daemon again
/usr/bin/yubikey-oath-daemon --verbose
```

## Dependencies

- Qt 6.7+ (Core, Widgets, Qml, Quick, QuickWidgets, Gui, DBus, Concurrent, Sql)
- KDE Frameworks 6.0+ (Runner, I18n, Config, ConfigWidgets, Notifications, CoreAddons, Wallet, KCMUtils, WidgetsAddons)
- PC/SC Lite (libpcsclite)
- xkbcommon (keyboard handling)
- libportal-qt6 (xdg-desktop-portal RemoteDesktop for Wayland input)
- KWayland (Wayland protocol)
- ZXing-C++ (QR code scanning)

## Code Style Notes

- **C++ Standard:** C++26 (set in CMakeLists.txt)
- Uses modern Qt6/KF6 APIs
- SOLID principles with dependency inversion
- Interface-based configuration (ConfigurationProvider)
- Signal/slot communication between components
- Resource files for embedded assets (icons)
- Result<T> pattern for type-safe error handling
- Smart pointers (std::unique_ptr, std::shared_ptr) for memory management
- Namespace organization: KRunner::YubiKey
