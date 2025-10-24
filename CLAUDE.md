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
QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.*=true" \
QT_LOGGING_TO_CONSOLE=1 \
QT_FORCE_STDERR_LOGGING=1 \
krunner --replace 2>&1 | tee /tmp/krunner.log
```

**IMPORTANT**: Do NOT use `kquitapp6` or `killall`. The `--replace` flag automatically replaces the running instance.

### Running the Daemon

```bash
# Manual start with logging
QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.*=true" \
/usr/bin/yubikey-oath-daemon --verbose 2>&1 | tee /tmp/daemon.log

# Check if daemon is running
busctl --user list | grep yubikey

# Check daemon D-Bus service
busctl --user status org.kde.plasma.krunner.yubikey

# Interact with daemon
busctl --user call org.kde.plasma.krunner.yubikey /Device \
  org.kde.plasma.krunner.yubikey.Device ListDevices
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
│   ├── utils/           # Shared utilities (portal_permission_manager, deferred_execution)
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
- D-Bus interface: `org.kde.plasma.krunner.yubikey.Device`
- Object path: `/Device`
- Single responsibility: Convert D-Bus types ↔ internal types and delegate to YubiKeyService
- Provides methods: `ListDevices`, `GetCredentials`, `GenerateCode`, `SavePassword`, `ForgetDevice`, `SetDeviceName`, `AddCredential`, `CopyCodeToClipboard`, `TypeCode`, `AddCredentialFromScreen`
- Emits signals: `DeviceConnected`, `DeviceDisconnected`, `CredentialsUpdated`
- **NO business logic** - pure delegation to YubiKeyService
- Uses TypeConversions utility for D-Bus type conversions

**YubiKeyService** (`src/daemon/services/yubikey_service.{h,cpp}`)
- **Business logic layer** (~430 lines)
- Aggregates and coordinates all daemon components
- Owns: YubiKeyDeviceManager, YubiKeyDatabase, PasswordStorage, DaemonConfiguration, YubiKeyActionCoordinator
- Implements all business logic for device management, credential operations, password handling
- Handles device lifecycle events (connection, disconnection, credential updates)
- Provides action coordinator for copy/type operations
- Emits signals forwarded to D-Bus layer
- **Note:** Authentication failures are handled internally - empty credential list indicates auth failure, no separate signal emitted

**YubiKeyDBusClient** (`src/shared/dbus/yubikey_dbus_client.{h,cpp}`)
- KRunner plugin side - connects to daemon
- Proxies D-Bus calls to daemon service
- Handles daemon availability detection
- Auto-fallback to direct PC/SC if daemon unavailable
- Located in `shared/` as it's used by both krunner and config modules

**TypeConversions** (`src/shared/dbus/type_conversions.{h,cpp}`)
- **Static utility class** for D-Bus type conversions
- Converts between internal types (OathCredential) and D-Bus types (CredentialInfo)
- Methods: `toCredentialInfo(const OathCredential&)`
- Used by YubiKeyDBusService for marshaling
- Located in `shared/dbus/` as part of D-Bus client library
- Pure utility class (deleted constructor, all static methods)

**Daemon Architecture Pattern:**
- Daemon runs as separate process for persistent YubiKey monitoring
- KRunner plugin connects via D-Bus for on-demand operations
- Separation enables background YubiKey detection and credential caching
- **IMPORTANT:** Daemon must successfully register D-Bus service or it exits immediately

**Daemon Lifecycle:**

1. **Startup:**
   - Daemon starts and registers D-Bus service `org.kde.plasma.krunner.yubikey`
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
   - Uses YubiKeyDBusClient to proxy requests
   - YubiKeyDBusService receives D-Bus calls → delegates to YubiKeyService
   - Falls back to direct PC/SC if daemon unavailable

### Core Components

**YubiKeyRunner** (`src/krunner/yubikeyrunner.{h,cpp}`)
- Main KRunner plugin entry point
- Orchestrates all components
- Handles search queries and result matching
- Manages lifecycle of all subsystems
- Can use either D-Bus client or direct PC/SC

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
- Creates KRunner::QueryMatch objects
- Calculates relevance scores
- Builds password error matches
- Applies display strategies for credential formatting

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
- Priority: Portal → Wayland → X11
- Factory pattern for platform abstraction

**X11TextInput** (`src/input/x11_text_input.{h,cpp}`)
- Uses XTest extension for X11
- Direct keyboard simulation
- Character-by-character typing

**WaylandTextInput** (`src/input/wayland_text_input.{h,cpp}`)
- Uses libei for Wayland input emulation
- Portal-based authentication
- Modern Wayland input protocol

**PortalTextInput** (`src/input/portal_text_input.{h,cpp}`)
- Uses org.freedesktop.portal.RemoteDesktop
- Works across X11/Wayland with proper permissions
- Requires user permission grants

### Storage and Configuration

**YubiKeyDatabase** (`src/storage/yubikey_database.{h,cpp}`)
- SQLite database for device persistence
- Stores device metadata (name, serial, requires_password flag)
- Path: `~/.local/share/krunner-yubikey/devices.db`
- Tables: devices
- Thread-safe database operations

**PasswordStorage** (`src/storage/password_storage.{h,cpp}`)
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
- Device management UI

**YubiKeyDeviceModel** (`src/config/yubikey_device_model.{h,cpp}`)
- QAbstractListModel for device list
- Connects to D-Bus daemon for device information
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
- `YubiKeyDaemonLog` - Daemon service (`org.kde.plasma.krunner.yubikey.daemon`)
- `YubiKeyDeviceManagerLog` - Device manager (`org.kde.plasma.krunner.yubikey.manager`)
- `YubiKeyOathDeviceLog` - Per-device OATH operations (`org.kde.plasma.krunner.yubikey.oath.device`)
- `CardReaderMonitorLog` - PC/SC monitoring (`org.kde.plasma.krunner.yubikey.pcsc`)
- `PasswordStorageLog` - KWallet storage (`org.kde.plasma.krunner.yubikey.storage`)
- `YubiKeyDatabaseLog` - Database operations (`org.kde.plasma.krunner.yubikey.database`)

**KRunner Components** (`src/krunner/logging_categories.{h,cpp}`):
- `YubiKeyRunnerLog` - Main KRunner plugin (`org.kde.plasma.krunner.yubikey.runner`)
- `NotificationOrchestratorLog` - Notification management (`org.kde.plasma.krunner.yubikey.notification`)
- `TouchWorkflowCoordinatorLog` - Touch workflow (`org.kde.plasma.krunner.yubikey.touch`)
- `ActionExecutorLog` - Action execution (`org.kde.plasma.krunner.yubikey.action`)
- `MatchBuilderLog` - Match building (`org.kde.plasma.krunner.yubikey.match`)
- `TextInputLog` - Input emulation (`org.kde.plasma.krunner.yubikey.input`)
- `DBusNotificationLog` - D-Bus notifications (`org.kde.plasma.krunner.yubikey.dbus`)

**Config Module** (`src/config/logging_categories.{h,cpp}`):
- `YubiKeyConfigLog` - Settings module (`org.kde.plasma.krunner.yubikey.config`)

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
QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.*=true" krunner --replace

# Enable specific components
QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.runner=true;org.kde.plasma.krunner.yubikey.oath=true" krunner --replace

# Enable with different log levels
QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.*.debug=true" krunner --replace

# Disable specific component
QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.*=true;org.kde.plasma.krunner.yubikey.dbus=false" krunner --replace
```

### Persistent Logging Configuration

Create `~/.config/QtProject/qtlogging.ini`:
```ini
[Rules]
org.kde.plasma.krunner.yubikey.*=true
org.kde.plasma.krunner.yubikey.oath.debug=true
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
QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.*=true" \
QT_LOGGING_TO_CONSOLE=1 \
QT_FORCE_STDERR_LOGGING=1 \
krunner --replace 2>&1 | tee /tmp/krunner_debug.log

# All components - Daemon
QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.*=true" \
QT_LOGGING_TO_CONSOLE=1 \
QT_FORCE_STDERR_LOGGING=1 \
/usr/bin/yubikey-oath-daemon --verbose 2>&1 | tee /tmp/daemon_debug.log

# Specific components for targeted debugging
QT_LOGGING_RULES="org.kde.plasma.krunner.yubikey.runner=true;org.kde.plasma.krunner.yubikey.oath=true;org.kde.plasma.krunner.yubikey.notification=true" \
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

```bash
# Check if daemon D-Bus service is registered
busctl --user list | grep yubikey

# Get service status
busctl --user status org.kde.plasma.krunner.yubikey

# Test daemon connectivity
busctl --user call org.kde.plasma.krunner.yubikey /Device \
  org.kde.plasma.krunner.yubikey.Device ListDevices

# Monitor D-Bus signals from daemon
dbus-monitor --session "sender='org.kde.plasma.krunner.yubikey'"

# Introspect D-Bus interface
busctl --user introspect org.kde.plasma.krunner.yubikey /Device
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
- KDE Frameworks 6.0+ (Runner, I18n, Config, ConfigWidgets, Notifications, CoreAddons, Wallet, KCMUtils)
- PC/SC Lite (libpcsclite)
- xkbcommon (keyboard handling)
- libei-1.0 (Wayland input)
- liboeffis-1.0 (Wayland input)
- KWayland (Wayland protocol)

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
