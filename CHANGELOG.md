# Changelog

All notable changes to KRunner YubiKey OATH Plugin will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - 2025-11-15

### ⚠️ Breaking Changes

**D-Bus Interface Split:**
- `pl.jkolo.yubikey.oath.Device` interface split into two separate interfaces:
  - **Device interface** (hardware/OATH operations): Removed `State`, `StateMessage`, `HasValidPassword`, `LastSeen`, `SavePassword()` properties/methods
  - **New DeviceSession interface** (connection state): Added `State`, `StateMessage`, `HasValidPassword`, `LastSeen`, `SavePassword()` properties/methods
- **Client Migration Required:**
  - Old code: Single `YubiKeyDeviceProxy` for all operations
  - New code: Use `OathDeviceProxy` (OATH operations) + `OathDeviceSessionProxy` (connection state)
  - Example:
    ```cpp
    // Before (v1.x):
    YubiKeyDeviceProxy *device = manager->getDevice(deviceId);
    device->savePassword(password);  // BROKEN in v2.0

    // After (v2.0):
    OathDeviceProxy *device = manager->getDevice(deviceId);
    OathDeviceSessionProxy *session = manager->getDeviceSession(deviceId);
    session->savePassword(password);  // Correct
    ```

**API Changes:**
- `IDeviceIconResolver` interface: `getModelIcon()` changed from 2 parameters to 3 parameters (added `capabilities`)
- D-Bus proxy classes renamed: `YubiKey*Proxy` → `Oath*Proxy` (DeviceProxy, DeviceSessionProxy, CredentialProxy, ManagerProxy)
- Internal device handling APIs changed for brand-agnostic support

### Added

**Multi-Brand OATH Support:**
- Added support for **Nitrokey devices** (Nitrokey 3A, 3C, 3 Mini) alongside YubiKey
- Brand detection from reader names and firmware versions
- Brand-specific protocol handling:
  - YubiKey: CALCULATE_ALL (0xA4) for bulk code generation
  - Nitrokey: Individual CALCULATE (0xA2) operations
- Device model detection with fallback hierarchy
- Brand-agnostic icon resolution with hicolor theme integration

**Async Architecture:**
- Full async operations with worker pool (4 threads by default)
- `PcscWorkerPool` for all PC/SC operations with per-device rate limiting (50ms minimum interval)
- `DeviceState` enum: Disconnected, Connecting, Authenticating, FetchingCredentials, Ready, Error
- `AsyncResult<T>` template wrapper for async operations with UUID tracking
- D-Bus available in <100ms startup time (vs 10-30s blocking in v1.x)
- Zero blocking during device hot-plug events

**Design Patterns:**
- **Template Method Pattern:** `OathDevice` base class with `createTempSession()` factory method
  - Eliminated ~550 lines of duplication between YubiKeyOathDevice and NitrokeyOathDevice
  - Single source of truth for 13 common operations
- **Rich Domain Model:** Moved behavior into `OathCredential` class (Tell, Don't Ask principle)
  - Eliminated ~50 lines from utility classes
  - Methods: `getDisplayName()`, `matches()`, `isExpired()`, `needsRegeneration()`
- **Dependency Inversion Principle:** `IOathSelector` interface (pcsc/ layer)
  - Breaks circular dependency between CardTransaction and YkOathSession
  - Enables polymorphic card operations

**PC/SC Improvements:**
- RAII `CardTransaction` class for automatic transaction cleanup
- Cooperative multi-access: Changed from `SCARD_SHARE_EXCLUSIVE` to `SCARD_SHARE_SHARED` with per-operation transactions
- Allows GnuPG and other tools to access card between OATH operations
- Preemptive touch notification signal emission (better UX timing)
- Automatic PC/SC service recovery from pcscd.service restarts (detection, cleanup, re-enumeration)

**Testing:**
- Virtual device emulators: `VirtualYubiKey`, `VirtualNitrokey` with full APDU emulation
- Isolated D-Bus test sessions via `TestDbusSession` helper
- E2E test suite: `test_e2e_device_lifecycle` (7 test cases covering detection → SELECT → CALCULATE)
- Service layer tests: PasswordService, DeviceLifecycleService, CredentialService
- Storage layer tests: YubiKeyDatabase (19 cases), SecretStorage (10 cases)
- **Target achieved:** ~85% line coverage, ~87% function coverage

**Security:**
- `OathErrorCodes` constants prevent i18n translation bugs in error handling
- `Result<T>` monad with `[[nodiscard]]` attributes enforce compile-time error checking
- `SecureMemory::SecureString` for automatic password memory wiping

### Changed

- Icon system: Migrated from Qt resources to hicolor theme standard (`/usr/share/icons/hicolor/`)
- Generated 85 icon files (12 models × 7 sizes + SVG generic) via ImageMagick build script
- Portal session lifecycle: Session-per-operation instead of persistent session (~30s faster daemon startup)
- Improved notification urgency handling (Critical for touch/reconnect/TOTP)
- Device-specific icons in notifications via `image-path` hint

### Fixed

- Touch notification timing (preemptive signal emission before blocking operations)
- PC/SC communication errors via 50ms rate limiting
- Daemon startup blocking D-Bus interface availability
- UI freezes during device hot-plug events
- Memory cleanup in portal sessions

### Documentation

- Comprehensive CLAUDE.md with architecture details, design patterns, testing strategy
- Polish translations (144 messages, 100% coverage)
- Multi-brand support documentation
- Breaking changes migration guide

---

## [1.1.0] - 2024-11-XX

### Added
- Initial multi-device support
- Enhanced UI components

### Fixed
- Various stability improvements

---

## [1.0.3] - 2024-XX-XX

### Fixed
- Bug fixes and stability improvements

---

## [1.0.2] - 2024-XX-XX

### Fixed
- Minor bug fixes

---

## [1.0.1] - 2024-XX-XX

### Fixed
- Initial bug fixes

---

## [1.0.0] - 2024-XX-XX

### Added
- Initial release
- YubiKey OATH TOTP/HOTP support
- KRunner integration
- KCM configuration module
- D-Bus daemon architecture
- SQLite credential database
- KWallet password storage
- Touch notification workflow
- Copy/type action support
- Polish localization

---

[2.0.0]: https://github.com/jkolo/yubikey-oath-krunner/compare/v1.1.0...v2.0.0
[1.1.0]: https://github.com/jkolo/yubikey-oath-krunner/compare/v1.0.3...v1.1.0
[1.0.3]: https://github.com/jkolo/yubikey-oath-krunner/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/jkolo/yubikey-oath-krunner/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/jkolo/yubikey-oath-krunner/compare/v1.0...v1.0.1
[1.0.0]: https://github.com/jkolo/yubikey-oath-krunner/releases/tag/v1.0
