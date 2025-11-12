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

## Architecture

**Source:** `src/shared/` (types, dbus, ui, utils, po, resources), `src/daemon/` (services, oath, storage, pcsc), `src/krunner/` (plugin), `src/config/` (KCM)

### D-Bus Hierarchy

```
/pl/jkolo/yubikey/oath (Manager) → /devices/<id> (Device) → /credentials/<id> (Credential)
```

**Legacy:** `pl.jkolo.yubikey.oath.daemon.Device` at `/Device` (ListDevices, GetCredentials, GenerateCode, etc.)

**Objects:**
- **OathManagerProxy** (~100 lines): singleton, ObjectManager, devices(), getAllCredentials(), signals: deviceConnected/Disconnected, credentialsChanged
- **OathDeviceProxy** (~150 lines): credentials(), savePassword(), changePassword(), forget(), addCredential(), setName()
- **OathCredentialProxy** (~100 lines): generateCode(), copyToClipboard(), typeCode(), deleteCredential()

**Service:** YubiKeyDBusService (marshaling ~135 lines) → YubiKeyService (business logic ~430 lines) → PasswordService (~225 lines), DeviceLifecycleService (~510 lines), CredentialService (~660 lines)

### Multi-Brand OATH (v2.0.0+)

**Supported:** YubiKey (NEO/4/5/Bio), Nitrokey (3A/3C/3Mini)

**⚠️ BREAKING CHANGES (v2.0.0):**
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
- **YkOathSession** (`src/daemon/oath/yk_oath_session.{h,cpp}` ~450 lines): OATH protocol base, PC/SC I/O with 50ms rate limiting, PBKDF2, NOT thread-safe
- **OathProtocol** (`src/daemon/oath/oath_protocol.{h,cpp}` ~200 lines): utilities, constants, TLV parsing
- **OathErrorCodes** (`src/daemon/oath/oath_error_codes.h`): Translation-independent error constants (PASSWORD_REQUIRED, TOUCH_REQUIRED, etc.)
- **ManagementProtocol** (`src/daemon/oath/management_protocol.{h,cpp}` ~150 lines): GET DEVICE INFO, YubiKey 4.1+

**Workflows:**
- NotificationOrchestrator: notifications, countdown, progress
- TouchWorkflowCoordinator: touch workflow, polling
- ActionExecutor: copy/type execution
- YubiKeyActionCoordinator: daemon-side action handling
- ActionManager: KRunner actions
- MatchBuilder: QueryMatch creation
- TouchHandler: touch detection
- ClipboardManager: secure clipboard, auto-clear

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

### Input System

TextInputFactory → Portal (libportal, xdp_session_keyboard_key, all Wayland) → X11 (XTest extension)

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

**Suites:** 26 tests, 100% pass (v2.0.0) - test_result, test_oath_protocol (94.8%), test_action_manager (78.9%), test_display_strategies (100%), test_credential_formatter, test_match_builder, test_yubikey_icon_resolver (21 tests), test_device_icon_resolver, test_device_card_layout (100%), test_brand_detection (15 tests), test_nitrokey_model_detector (20 tests), test_device_capabilities (19 tests), mocks

**Run:** `ctest --preset clang-debug --output-on-failure` or `cd build-clang-debug && ctest --output-on-failure`

**Coverage (v1.0.0):** 58.0% lines, 65.3% functions

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
cd build-clang-debug && ctest --output-on-failure  # 23/23 pass
```
