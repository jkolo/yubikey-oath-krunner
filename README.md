# KRunner YubiKey OATH Plugin

A KDE Plasma 6 plugin that integrates YubiKey OATH (TOTP/HOTP) two-factor authentication codes directly into KRunner.

![KDE Plasma](https://img.shields.io/badge/KDE%20Plasma-6.0+-blue)
![Qt](https://img.shields.io/badge/Qt-6.7+-green)
![License](https://img.shields.io/badge/license-GPL--3.0-blue)

## Features

- 🔍 **Quick Search** - Search for OATH accounts directly in KRunner (Alt+Space)
- 📋 **Copy to Clipboard** - Instantly copy TOTP/HOTP codes with one click
- ⌨️ **Auto-Type** - Automatically type codes into applications via keyboard simulation
- 🔄 **D-Bus Daemon** - Persistent YubiKey monitoring and credential caching
- 🔌 **Multi-Device Support** - Seamlessly work with multiple YubiKeys
- 🔒 **Secure Storage** - YubiKey passwords stored securely in KWallet
- 🖥️ **Multi-Platform** - Supports X11, Wayland, and XDG Portal for input
- 🔔 **Smart Notifications** - Shows remaining time with countdown progress bar
- ⚡ **Touch Support** - Visual feedback for YubiKey touch requirements
- 🎨 **Customizable Display** - Multiple formatting options (name only, name+user, full details)

## Screenshots

**KRunner Integration:**
```
Alt+Space → Type account name → See matching TOTP codes
```

**Notification with Countdown:**
```
[YubiKey Icon] Code copied: 123456
Expires in: 25 seconds [████████░░] 83%
```

## Requirements

### Runtime Dependencies
- KDE Plasma 6.0+
- Qt 6.7+
- KDE Frameworks 6.18+ (KRunner, KI18n, KConfig, KNotifications, KWallet, KCMUtils)
- PC/SC Lite (pcscd service)
- YubiKey with OATH application configured

### Build Dependencies
- CMake 3.16+
- Extra CMake Modules (ECM)
- C++26 compatible compiler (GCC 13+, Clang 16+)
- Qt 6 development packages
- KDE Frameworks 6 development packages
- libpcsclite development headers
- xkbcommon development headers
- libei development headers (Wayland input)

## Installation

### From Source

```bash
# Install dependencies (Arch Linux)
sudo pacman -S base-devel cmake extra-cmake-modules \
    qt6-base qt6-qml qt6-quick \
    kf6-krunner kf6-ki18n kf6-kconfig kf6-knotifications \
    kf6-kcoreaddons kf6-kwallet kf6-kcmutils \
    pcsclite libei xkbcommon kwayland

# Clone and build
git clone https://git.kolosowscy.pl/jurek/krunner-yoath.git
cd krunner-yoath
mkdir build && cd build

# Configure and build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Install system-wide
sudo cmake --install .

# Restart KRunner
krunner --replace
```

### Arch Linux (AUR)

```bash
# Install from AUR (when available)
yay -S krunner-yubikey
```

## Usage

### First-Time Setup

1. **Enable PC/SC Service:**
   ```bash
   sudo systemctl enable --now pcscd
   ```

2. **Configure YubiKey OATH:**
   - Insert your YubiKey
   - Use `ykman oath accounts add` to add accounts
   - Or use Yubico Authenticator app

3. **Configure Plugin:**
   - Open System Settings → Search → KRunner
   - Find "YubiKey OATH" in the list
   - Configure display format, notifications, touch timeout

### Daily Use

1. **Open KRunner:** Press `Alt+Space` or `Alt+F2`

2. **Search for Account:** Type part of the account name
   - Example: "github" → shows GitHub TOTP codes

3. **Copy Code:** Click the result or press Enter
   - Code is copied to clipboard
   - Notification shows expiration countdown

4. **Auto-Type Code:** Press `Ctrl+Enter` (configurable)
   - Code is automatically typed into focused application
   - Works on X11, Wayland, and via XDG Portal

### Password-Protected YubiKeys

If your YubiKey OATH application has a password:

1. Plugin will prompt for password on first use
2. Password is stored securely in KWallet
3. Retrieved automatically on subsequent uses
4. Manage via System Settings → YubiKey OATH

### Touch-Required Credentials

For OATH credentials configured with `require-touch`:

1. Plugin shows notification: "Touch your YubiKey"
2. Touch the YubiKey sensor
3. Code is generated and action completes
4. Timeout configurable in settings (default: 15 seconds)

## Configuration

Access settings via **System Settings → Search → KRunner → YubiKey OATH**

### Available Options

- **Display Format:**
  - Name Only: `GitHub`
  - Name + User: `GitHub (user@example.com)`
  - Full Details: `GitHub - user@example.com (TOTP)`

- **Notifications:**
  - Enable/disable notifications
  - Show expiration countdown
  - Custom timeout values

- **Touch Timeout:**
  - Time to wait for YubiKey touch (5-60 seconds)
  - Default: 15 seconds

- **Keyboard Shortcuts:**
  - Copy action: `Enter`
  - Type action: `Ctrl+Enter` (configurable)

## Architecture

### Core Components

- **YubiKeyRunner** - Main KRunner plugin entry point
- **YubiKeyOath** - PC/SC communication and APDU commands
- **NotificationOrchestrator** - Notification management with countdown timers
- **TouchWorkflowCoordinator** - Touch requirement workflow
- **ActionExecutor** - Copy and auto-type actions
- **TextInputFactory** - Multi-platform input abstraction (X11/Wayland/Portal)
- **PasswordStorage** - Secure password storage via KWallet

### Design Patterns

- **SOLID Principles** - Interface segregation, dependency inversion
- **Result<T> Pattern** - Type-safe error handling
- **Strategy Pattern** - Pluggable credential display formats
- **Factory Pattern** - Platform-specific input provider creation
- **Smart Pointers** - RAII memory management

### Security

- **No Credential Storage** - Credentials remain on YubiKey
- **Secure Password Storage** - KWallet integration for OATH passwords
- **PC/SC Communication** - Direct hardware communication
- **No Network Access** - Fully offline operation

## Development

### Build for Development

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build . -j$(nproc)
cmake --install .
```

### Running Tests

```bash
cd build
ctest --output-on-failure

# Individual test suites
./bin/test_result              # Result<T> template tests
./bin/test_code_validator      # Code validation tests
./bin/test_credential_formatter # Display formatting tests
./bin/test_match_builder       # KRunner match building tests
```

### Code Coverage

```bash
# Build with coverage
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

### Debugging

Enable detailed logging:

```bash
QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.*=true" \
QT_LOGGING_TO_CONSOLE=1 \
QT_FORCE_STDERR_LOGGING=1 \
krunner --replace 2>&1 | tee /tmp/krunner_debug.log
```

Available logging categories:
- `pl.jkolo.yubikey.oath.daemon.runner` - Main plugin
- `pl.jkolo.yubikey.oath.daemon.oath` - OATH protocol
- `pl.jkolo.yubikey.oath.daemon.notification` - Notifications
- `pl.jkolo.yubikey.oath.daemon.touch` - Touch workflow
- `pl.jkolo.yubikey.oath.daemon.input` - Input emulation
- `pl.jkolo.yubikey.oath.daemon.pcsc` - PC/SC communication

See [CLAUDE.md](CLAUDE.md) for complete logging categories list.

## Troubleshooting

### Plugin Not Showing in KRunner

```bash
# Check if plugin is installed
ls -la /usr/lib/qt6/plugins/kf6/krunner/krunner_yubikey.so

# Restart KRunner
krunner --replace

# Check KRunner configuration
kreadconfig6 --file krunnerrc --group Plugins --key krunner_yubikeyEnabled
```

### YubiKey Not Detected

```bash
# Check PC/SC service
sudo systemctl status pcscd

# Test YubiKey detection
pcsc_scan

# Check for YubiKey
ykman list
```

### Auto-Type Not Working

**X11:**
```bash
# Install XTest support
sudo pacman -S xorg-xhost
```

**Wayland:**
```bash
# Install libei/liboeffis
sudo pacman -S libei

# Or use XDG Portal (automatic fallback)
```

### Password Errors

```bash
# Test OATH password manually
ykman oath accounts list

# Clear stored password
# Open KWallet Manager → YubiKey OATH Application → Delete entry
```

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Follow existing code style (see [CLAUDE.md](CLAUDE.md))
4. Add tests for new functionality
5. Ensure all tests pass (`ctest`)
6. Commit with clear messages (`git commit -m 'Add amazing feature'`)
7. Push to branch (`git push origin feature/amazing-feature`)
8. Open a Pull Request

### Code Quality Standards

- **Test Coverage:** Maintain >50% coverage for core logic
- **No Warnings:** Code must compile without warnings
- **Documentation:** Update README and API docs for changes
- **Logging:** Use appropriate logging categories

## Project Status

**Version:** 2.0.0
**Status:** Production Ready
**Test Coverage:** 58.0% (8 test suites, 100% pass rate)

Key achievements:
- ✅ Thread-safe multi-device support
- ✅ D-Bus daemon for persistent YubiKey monitoring
- ✅ Namespace organization (KRunner::YubiKey)
- ✅ Type-safe Result<T> error handling
- ✅ Comprehensive Qt logging categories
- ✅ 8 comprehensive test suites (100+ individual tests)
- ✅ Clean compilation (zero warnings)
- ✅ Static analysis validated

## License

This project is licensed under the GNU General Public License v2.0 or later (GPL-2.0-or-later). See source file headers for license details.

## Acknowledgments

- **KDE Community** - For the excellent Plasma desktop and frameworks
- **Yubico** - For YubiKey hardware and documentation
- **PC/SC Workgroup** - For smart card communication standards

## Links

- **KDE Plasma:** https://kde.org/plasma-desktop/
- **Yubico:** https://www.yubico.com/
- **PC/SC Lite:** https://pcsclite.apdu.fr/
- **Repository:** https://git.kolosowscy.pl/jurek/krunner-yoath
- **Bug Reports:** https://git.kolosowscy.pl/jurek/krunner-yoath/-/issues

## Support

- **Documentation:** See [CLAUDE.md](CLAUDE.md) for developer guide
- **Issues:** https://git.kolosowscy.pl/jurek/krunner-yoath/-/issues

---

**Made with ❤️ for the KDE Community**
