# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

KRunner YubiKey OATH Plugin - A KDE Plasma 6 plugin that integrates YubiKey OATH TOTP/HOTP codes directly into KRunner. Users can search for accounts, copy codes to clipboard, or auto-type them using keyboard shortcuts.

The project consists of two main components:
1. **yubikey-oath-daemon** - Background D-Bus service for persistent YubiKey monitoring
2. **krunner_yubikey.so** - KRunner plugin that connects to daemon or uses direct PC/SC

## Build Commands

### Building Daemon and Plugin

**PREFERRED METHOD: Using CMake Presets**

The project includes CMake presets for consistent builds (see `CMakePresets.json`):

```bash
# Configure and build with clang-debug preset
cmake --preset clang-debug
cmake --build build-clang-debug -j$(nproc)
run0 cmake --install build-clang-debug

# Or use clang-release preset for optimized builds
cmake --preset clang-release
cmake --build build-clang-release -j$(nproc)
run0 cmake --install build-clang-release
```

**Available presets:** `clang-debug` (development), `clang-release` (optimized)

### Verify Installation

```bash
# Check daemon binary
ls -la /usr/bin/yubikey-oath-daemon

# Check KRunner plugin
ls -la /usr/lib/qt6/plugins/kf6/krunner/krunner_yubikey.so

# Check KCM (settings) plugin
ls -la /usr/lib/qt6/plugins/kcm_krunner_yubikey.so
```

### Development Build & Quick Rebuild

```bash
# Local installation
cmake --preset clang-debug -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build-clang-debug -j$(nproc) && cmake --install build-clang-debug

# Quick rebuild after changes
cmake --build build-clang-debug -j$(nproc) && run0 cmake --install build-clang-debug
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
│       ├── yubikey.svg  # Application icon
│       └── models/      # YubiKey model-specific icons (PNG format)
│
├── daemon/              # D-Bus daemon service
│   ├── main.cpp, yubikey_dbus_service.{h,cpp}
│   ├── logging_categories.{h,cpp}  # Daemon-specific logging
│   ├── services/        # Business logic layer (YubiKeyService, PasswordService, DeviceLifecycleService, CredentialService)
│   ├── actions/         # Action coordination and execution
│   ├── config/          # Daemon configuration (DaemonConfiguration)
│   ├── workflows/       # Touch workflow coordination, notification orchestration
│   ├── clipboard/       # Clipboard management
│   ├── input/           # Text input emulation (X11, Wayland, Portal)
│   ├── notification/    # D-Bus notification manager
│   ├── formatting/      # Code validation
│   ├── ui/              # UI components (add_credential_dialog)
│   ├── utils/           # Daemon utilities (async_waiter, qr/otpauth parsers, screenshot_capture)
│   ├── cache/           # Credential caching (CredentialCacheSearcher)
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
    ├── device_delegate.{h,cpp}  # Custom item delegate for device list
    ├── logging_categories.{h,cpp}  # Config-specific logging
    ├── yubikey_config.ui  # Qt Designer UI file
    └── resources/       # Config module resources (icons only)
        └── config.qrc   # Qt resource file
```

**Key principles:**
- **Component isolation**: Each module (daemon, krunner, config) has its own subdirectory
- **Shared resources**: Common code and assets in `shared/` to avoid duplication
- **Daemon-centric architecture**: Most business logic moved to daemon; krunner is now lightweight
- **Resource organization**: Icons in shared/resources/, Qt Designer .ui files in config/
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
- Delegates to specialized services: PasswordService, DeviceLifecycleService, CredentialService
- Implements all business logic for device management, credential operations, password handling
- Handles device lifecycle events (connection, disconnection, credential updates)
- Provides action coordinator for copy/type operations
- Emits signals forwarded to D-Bus layer
- **Note:** Authentication failures are handled internally - empty credential list indicates auth failure, no separate signal emitted

### Service Layer Architecture (v1.1+)

YubiKeyService delegates to specialized services following Single Responsibility Principle:

**PasswordService** (`src/daemon/services/password_service.{h,cpp}`)
- Handles password operations (~224 lines)
- Methods: `validatePassword()`, `savePassword()`, `changePassword()`, `hasStoredPassword()`
- Dependencies: SecretStorage (KWallet), YubiKeyOathDevice
- Validates passwords against YubiKey OATH application
- Stores validated passwords in KWallet

**DeviceLifecycleService** (`src/daemon/services/device_lifecycle_service.{h,cpp}`)
- Manages device lifecycle events (~510 lines)
- Methods: `handleDeviceConnected()`, `handleDeviceDisconnected()`, `setDeviceName()`, `forgetDevice()`
- Dependencies: YubiKeyDeviceManager, YubiKeyDatabase
- Coordinates device connection/disconnection workflow
- Persists device metadata to database
- Cleans up resources on device removal

**CredentialService** (`src/daemon/services/credential_service.{h,cpp}`)
- CRUD operations for credentials (~658 lines)
- Methods: `getCredentials()`, `generateCode()`, `addCredential()`, `deleteCredential()`
- Dependencies: YubiKeyDeviceManager, YubiKeyDatabase, DaemonConfiguration, DBusNotificationManager
- Retrieves credentials (online devices + cached for offline)
- Interactive credential addition with AddCredentialDialog
- Shows success notifications after credential operations
- Appends cached credentials for offline devices when cache enabled

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
- **IMPORTANT**: TAG_PROPERTY (0x78) uses Tag-Value format (NOT Tag-Length-Value)
  - Correct: `78 02` (tag + value, 2 bytes)
  - Wrong: `78 01 02` (tag + length + value, 3 bytes causes 0x6a80 error)

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

**ManagementProtocol** (`src/daemon/oath/management_protocol.{h,cpp}`)
- Stateless utility class for YubiKey Management interface protocol
- Implements GET DEVICE INFO command for firmware and model detection
- Available on YubiKey 4.1+ firmware
- Protocol constants and TLV tag definitions
- Device information retrieval:
  - Firmware version (major.minor.patch)
  - Serial number (4-byte big-endian)
  - Form factor detection (Keychain, Nano, USB-C variants)
  - USB/NFC interface capabilities
  - FIPS compliance and Security Key detection
- Static command creation and response parsing methods
- No state, no I/O - pure functions only
- Used by OathSession for extended device information
- Complements OathProtocol (OATH-specific) with Management interface (device-level)

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

### UI Components

**DeviceDelegate** (`src/config/device_delegate.{h,cpp}`)
- Custom QStyledItemDelegate for device list in KCM settings module (~425 lines)
- Renders each device as a styled card with model-specific icon
- Uses IDeviceIconResolver interface (ISP compliance) instead of full YubiKeyConfig dependency
- Button click handling via editorEvent() with signal emission
- Inline editing of device names via createEditor/setEditorData/setModelData
- Delegates rendering to DeviceCardPainter, layout to DeviceCardLayout
- Uses i18n() for all translatable strings (NOT Qt tr())

**DeviceCardLayout** (`src/config/device_card_layout.{h,cpp}`)
- Calculates layout for device card rendering (~139 lines)
- Extracted from DeviceDelegate following Single Responsibility Principle
- Computes positions for: icon, text lines, buttons, status indicators
- Handles card geometry, margins, and spacing
- Pure layout logic without rendering or interaction

**DeviceCardPainter** (`src/config/device_card_painter.{h,cpp}`)
- Renders device card visual components (~254 lines)
- Extracted from DeviceDelegate following Single Responsibility Principle
- Paints: background, icon, device name, connection status, "last seen" timestamp
- Uses QPainter for custom drawing
- Works with DeviceCardLayout for positioning

**RelativeTimeFormatter** (`src/config/relative_time_formatter.{h,cpp}`)
- Formats timestamps as relative time strings (~99 lines)
- Extracted from DeviceDelegate following Single Responsibility Principle
- Examples: "2 minutes ago", "3 hours ago", "2 days ago"
- Handles edge cases: just now, singular/plural forms
- Uses i18n() for localized output

**IDeviceIconResolver** (`src/config/i_device_icon_resolver.h`)
- Interface for icon resolution (~31 lines)
- Pure virtual interface following Interface Segregation Principle
- Method: `getModelIcon(deviceModel) → QString`
- Decouples DeviceDelegate from YubiKeyConfig

**YubiKeyConfigIconResolver** (`src/config/yubikey_config_icon_resolver.{h,cpp}`)
- Adapter implementing IDeviceIconResolver (~58 lines)
- Delegates to YubiKeyConfig::getModelIcon()
- Created to fix ISP violation in DeviceDelegate

### Utility Classes

**CredentialFinder** (`src/shared/utils/credential_finder.{h,cpp}`)
- Single-purpose utility function for credential search
- Static function: `findCredential(credentials, credentialName, deviceId)`
- Case-insensitive credential name matching
- Device ID filtering
- Returns std::optional<OathCredential>
- Eliminates code duplication across components
- Used by YubiKeyRunner, TouchWorkflowCoordinator, and other components
- ~26 lines of focused search logic

**YubiKeyIconResolver** (`src/shared/utils/yubikey_icon_resolver.{h,cpp}`)
- Resolves model-specific icon paths for YubiKey devices
- Centralized icon selection based on YubiKeyModel metadata
- Multi-level fallback strategy:
  1. Exact match: series + variant + ports (e.g., "yubikey-5c-nano.png")
  2. Series + ports match (e.g., "yubikey-5c-nfc.png")
  3. Generic fallback: ":/icons/yubikey.svg"
- Icon naming convention: lowercase, hyphen-separated
  - Format: "yubikey-{series}{usb_type}[-{variant}][-nfc]"
  - File format: PNG for model-specific icons, SVG for generic fallback
  - Examples: "yubikey-5-nfc.png", "yubikey-5c-nano.png", "yubikey-bio-c.png"
- Icon location: `src/shared/resources/models/`
- Returns Qt resource path: ":/icons/models/{icon}" or ":/icons/yubikey.svg"
- ~183 lines including comprehensive fallback logic
- Used by DeviceDelegate and any component displaying device icons
- **FIPS model handling:**
  - YubiKey 5 FIPS uses same icons as YubiKey 5 (no separate FIPS icons)
  - YubiKey 4 FIPS uses same icons as YubiKey 4
  - Implementation uses fallthrough pattern in switch statement
  - Example: Both YubiKey5 and YubiKey5FIPS return "5" series string
  - Security Key and Bio series fallback to generic icon (no OATH support)

**SecureMemory** (`src/daemon/utils/secure_memory.{h,cpp}`)
- RAII-based secure memory management for sensitive data (~207 lines)
- Template class for automatic zeroing of sensitive data on destruction
- Uses `sodium_mlock()` / `sodium_munlock()` to prevent swapping to disk
- Overwrites memory with zeros before deallocation
- Prevents sensitive data (passwords, secrets) from leaking to swap or core dumps
- Used for password and secret key handling throughout daemon
- Thread-safe, move-only semantics

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

**yubikey_config.ui** (`src/config/yubikey_config.ui`)
- **Qt Designer UI file** (NOT QML) for configuration interface
- Settings: notifications, display format, touch timeout, credential caching
- Uses standard Qt Widgets (QCheckBox, QSpinBox, QListView, etc.)
- Device list rendered via DeviceDelegate custom item delegate
- Icon path: `:/icons/yubikey.svg` (from shared resources)
- Device password management with inline editing
- Uses i18n() for all translatable strings
- **Resource initialization**: Config module must call `qInitResources_shared()` in constructor to load icons

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

**TransactionGuard** ensures database transaction safety through RAII: Constructor begins transaction, destructor auto-rollbacks if not committed. Eliminates manual rollback on error paths, exception-safe.

```cpp
TransactionGuard guard(m_db);
if (!guard.isValid()) return false;
// All early returns automatically trigger rollback
return guard.commit(); // Explicit commit
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
- Generic: `:/icons/yubikey.svg` (location: `src/shared/resources/yubikey.svg`)
- Model-specific: `:/icons/models/yubikey-5c-nfc.png` via YubiKeyIconResolver (location: `src/shared/resources/models/`)
- Resources: `shared.qrc` (all modules), `config.qrc` (config only)
- Config module: Must include `shared.qrc` via `qt6_add_resources()` and call `qInitResources_shared()`

### Error Handling
- Password errors show special match that opens settings
- Touch timeout emits signal to TouchHandler
- D-Bus notification failures degrade gracefully
- Result<T> pattern for type-safe error handling

### Translation System

The project uses the KDE i18n system for internationalization:

**Translation Infrastructure:**
- All user-visible strings use `i18n()` macro (NOT Qt's `tr()`)
- Plural forms handled with `i18np()` macro
- Translation files located in `src/shared/po/`
- CMake integration via `ki18n_install()` in CMakeLists.txt

**Available Translations:**
- **Polish (pl.po)**: 144 messages, 100% complete
- Template file: `yubikey_oath.pot` (source message catalog)

**Translation Usage:**
```cpp
// Simple translation
QString text = i18n("Connect your YubiKey");

// Translation with placeholders
QString msg = i18n("Device %1 connected", deviceName);

// Plural forms
QString count = i18np("1 credential", "%1 credentials", num);
```

**IMPORTANT:**
- **NEVER** use Qt's `tr()` function in this codebase
- Always use KDE i18n macros for consistency with KDE Frameworks
- DeviceDelegate, YubiKeyConfig, and all UI components use i18n()
- Even C++ UI code (not just QML) must use i18n(), not tr()

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
- `test_relative_time_formatter.cpp` - Relative time formatting tests (18 tests, 100% coverage)
- `test_yubikey_icon_resolver.cpp` - Icon resolver with FIPS verification (21 tests, 100% coverage)
- `test_device_icon_resolver.cpp` - Interface Segregation Principle compliance (13 tests)
- `test_device_card_layout.cpp` - Device card layout calculations (14 tests, 100% coverage)
- `mocks/mock_configuration_provider.cpp` - Mock for configuration testing

### Running Tests

**With CMake presets (recommended):**

```bash
# Run all tests using preset
ctest --preset clang-debug --output-on-failure

# Or manually with build directory
cd build-clang-debug
ctest --output-on-failure

# Run individual test suites
./bin/test_result              # Result<T> tests
./bin/test_oath_protocol       # OATH protocol tests
./bin/test_action_manager      # Action manager tests
./bin/test_flexible_display_strategy  # Display strategies tests
./bin/test_code_validator      # Validation tests
./bin/test_credential_formatter # Formatting tests
./bin/test_match_builder       # Match building tests
```

**Legacy method:**

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

### Testing Qt Resources

**IMPORTANT:** Qt resource system behaves differently in test environment:

- `QFile::exists(":/icons/yubikey.svg")` may return `false` even if resource exists
- Tests should check file extensions instead: `iconPath.endsWith(".svg")`
- Tests should allow both specific icons OR generic fallback:
  ```cpp
  // ✅ CORRECT - allows fallback
  QVERIFY(iconPath.contains("yubikey-5-nfc") || iconPath.endsWith(".svg"));

  // ❌ WRONG - fails when fallback triggers
  QVERIFY(iconPath.contains("yubikey-5-nfc"));
  ```
- Resource files must be registered: Config module calls `qInitResources_shared()`

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
# All components (KRunner or Daemon)
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true" QT_LOGGING_TO_CONSOLE=1 QT_FORCE_STDERR_LOGGING=1 \
  krunner --replace 2>&1 | tee /tmp/krunner.log
# Or: /usr/bin/yubikey-oath-daemon --verbose 2>&1 | tee /tmp/daemon.log

# Specific components
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.runner=true;pl.jkolo.yubikey.oath.daemon.oath=true" krunner --replace
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

```bash
# Check daemon status
busctl --user list | grep yubikey
busctl --user status pl.jkolo.yubikey.oath.daemon

# Legacy interface (/Device) - backward compatibility
busctl --user call pl.jkolo.yubikey.oath.daemon /Device pl.jkolo.yubikey.oath.daemon.Device ListDevices

# Hierarchical interface (v1.0) - ObjectManager pattern
busctl --user tree pl.jkolo.yubikey.oath.daemon  # View 3-level hierarchy
busctl --user call pl.jkolo.yubikey.oath.daemon /pl/jkolo/yubikey/oath pl.jkolo.yubikey.oath.Manager GetManagedObjects
busctl --user introspect pl.jkolo.yubikey.oath.daemon /pl/jkolo/yubikey/oath/devices/<deviceId>
busctl --user call pl.jkolo.yubikey.oath.daemon /pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credId> pl.jkolo.yubikey.oath.Credential GenerateCode

# Monitor signals
dbus-monitor --session "sender='pl.jkolo.yubikey.oath.daemon'"
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
- W projekcie używaj tylko CMake z bezwzględnymi ścieżkami do każdej operacji. Zabronione jest korzystanie z make, ninja, etc.
- W projekcie używaj tylko CMake z bezwzględnymi ścieżkami do każdej operacji. Zabronione jest korzystanie z make, ninja, etc.

## Code Quality and Static Analysis

### Clang-Tidy Compliance

The project uses clang-tidy for static code analysis. All code MUST pass clang-tidy checks without errors.

**Common clang-tidy issues and solutions:**

1. **bugprone-branch-clone** - Identical consecutive switch branches
   ```cpp
   // ❌ WRONG - triggers bugprone-branch-clone
   switch (series) {
       case YubiKeySeries::YubiKey5:
           return QStringLiteral("5");
       case YubiKeySeries::YubiKey5FIPS:
           return QStringLiteral("5");  // Identical to above - ERROR
   }

   // ✅ CORRECT - use fallthrough pattern
   switch (series) {
       case YubiKeySeries::YubiKey5:
       case YubiKeySeries::YubiKey5FIPS:
           // FIPS models use same icons as non-FIPS counterparts
           return QStringLiteral("5");
   }
   ```

2. **Switch statement fallthrough pattern:**
   - Combine case labels when return values are identical
   - Add comment explaining why cases are combined
   - Applies to: icon resolution, model detection, etc.

**Verification:**
```bash
# Build with clang-tidy enabled (automatic in CMake presets)
cmake --build build-clang-debug -j$(nproc)

# Expect zero errors - all warnings are treated as errors
```