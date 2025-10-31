/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "modifier_key_checker.h"
#include "../logging_categories.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QThread>
#include <KLocalizedString>

// X11 includes for XQueryKeymap
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdlib>

// evdev ioctl for Wayland support (direct kernel interface)
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <cstring>
#include <vector>

namespace YubiKeyOath {
namespace Daemon {

namespace {
    // Modifiers to check (excludes Meta/Windows and Keypad)
    constexpr Qt::KeyboardModifiers MONITORED_MODIFIERS =
        Qt::ShiftModifier |
        Qt::ControlModifier |
        Qt::AltModifier |
        Qt::GroupSwitchModifier;  // AltGr

    // Linux evdev keycodes for modifier keys
    // Reference: /usr/include/linux/input-event-codes.h
    constexpr int EVDEV_KEY_LEFTSHIFT = 42;
    constexpr int EVDEV_KEY_RIGHTSHIFT = 54;
    constexpr int EVDEV_KEY_LEFTCTRL = 29;
    constexpr int EVDEV_KEY_RIGHTCTRL = 97;
    constexpr int EVDEV_KEY_LEFTALT = 56;
    constexpr int EVDEV_KEY_RIGHTALT = 100;  // AltGr on international keyboards

    /**
     * @brief Helper macro to test if a bit is set in a bit array
     * @param bit Bit number to test
     * @param array Byte array containing bits
     * @return true if bit is set
     *
     * Similar to kernel test_bit() macro. Uses same formula as X11 XQueryKeymap.
     */
    #define test_bit(bit, array) ((array)[(bit)/8] & (1<<((bit)%8)))

    /**
     * @brief RAII wrapper for keyboard device file descriptor
     */
    struct KeyboardDevice {
        int fd = -1;

        ~KeyboardDevice() {
            if (fd >= 0) {
                close(fd);
            }
        }

        // Delete copy operations (move-only type)
        KeyboardDevice(const KeyboardDevice&) = delete;
        KeyboardDevice& operator=(const KeyboardDevice&) = delete;

        // Move operations
        KeyboardDevice(KeyboardDevice&& other) noexcept
            : fd(other.fd) {
            other.fd = -1;
        }

        KeyboardDevice& operator=(KeyboardDevice&& other) noexcept {
            if (this != &other) {
                // Clean up current resource
                if (fd >= 0) {
                    close(fd);
                }
                // Transfer ownership
                fd = other.fd;
                other.fd = -1;
            }
            return *this;
        }

        KeyboardDevice() = default;
    };

    // Cache of opened keyboard devices
    static std::vector<KeyboardDevice> g_keyboards;
    static bool g_evdev_initialized = false;

    /**
     * @brief Checks if a device is a keyboard using ioctl EVIOCGBIT
     * @param fd File descriptor of the device to check
     * @return true if device has keyboard capabilities
     */
    bool isKeyboardDevice(int fd)
    {
        if (fd < 0) {
            return false;
        }

        // Check if device supports EV_KEY event type
        unsigned char evtype_bits[(EV_MAX + 7) / 8];
        std::memset(evtype_bits, 0, sizeof(evtype_bits));

        if (ioctl(fd, EVIOCGBIT(0, sizeof(evtype_bits)), evtype_bits) < 0) {
            return false;
        }

        if (!test_bit(EV_KEY, evtype_bits)) {
            return false;
        }

        // Check if device has standard letter keys (keyboards have KEY_A)
        unsigned char key_bits[(KEY_MAX + 7) / 8];
        std::memset(key_bits, 0, sizeof(key_bits));

        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
            return false;
        }

        if (!test_bit(KEY_A, key_bits)) {
            return false;
        }

        // Optionally get device name for logging
        char name[256] = "Unknown";
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            qCDebug(TextInputLog) << "Identified keyboard device:" << name;
        }

        return true;
    }

    /**
     * @brief Initializes evdev device cache by enumerating /dev/input/event*
     * @return true if at least one keyboard device was found
     */
    bool initializeEvdevDevices()
    {
        if (g_evdev_initialized) {
            return !g_keyboards.empty();
        }

        qCDebug(TextInputLog) << "Initializing evdev keyboard devices...";

        // Enumerate /dev/input/event0 through event31 (typical range)
        for (int i = 0; i < 32; ++i) {
            QString const device_path = QStringLiteral("/dev/input/event%1").arg(i);

            // Try to open device (non-blocking, read-only)
            int fd = open(device_path.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                // Device doesn't exist or no permission - skip silently
                continue;
            }

            // Check if this is a keyboard device using ioctl
            if (isKeyboardDevice(fd)) {
                // Create KeyboardDevice and move into cache
                KeyboardDevice kbd;
                kbd.fd = fd;
                g_keyboards.push_back(std::move(kbd));
                qCDebug(TextInputLog) << "Added keyboard device:" << device_path;
            } else {
                // Not a keyboard - cleanup
                close(fd);
            }
        }

        g_evdev_initialized = true;

        if (g_keyboards.empty()) {
            qCDebug(TextInputLog) << "No keyboard devices found via evdev"
                                  << "(permission denied or no keyboards available)";
            return false;
        }

        qCDebug(TextInputLog) << "Successfully initialized" << g_keyboards.size()
                              << "keyboard device(s) via evdev";
        return true;
    }

    /**
     * @brief Cleanup all evdev keyboard devices
     */
    [[maybe_unused]] void cleanupEvdevDevices()
    {
        qCDebug(TextInputLog) << "Cleaning up evdev keyboard devices...";
        g_keyboards.clear();  // RAII destructors handle cleanup
        g_evdev_initialized = false;
    }

    /**
     * @brief Checks if a specific evdev key is pressed on any keyboard device using ioctl EVIOCGKEY
     * @param keycode Linux evdev keycode (e.g., EVDEV_KEY_LEFTSHIFT)
     * @return true if key is currently pressed on any keyboard
     */
    bool isKeyPressedEvdev(int keycode)
    {
        for (const auto& kbd : g_keyboards) {
            if (kbd.fd < 0) {
                continue;
            }

            // Get current key state from kernel
            unsigned char key_states[(KEY_MAX + 7) / 8];
            std::memset(key_states, 0, sizeof(key_states));

            if (ioctl(kbd.fd, EVIOCGKEY(sizeof(key_states)), key_states) < 0) {
                continue;
            }

            // Check if specific key is pressed (bit set in array)
            if (test_bit(keycode, key_states)) {
                return true;
            }
        }

        return false;
    }

    /**
     * @brief Gets currently pressed modifiers using evdev ioctl (EVIOCGKEY)
     * @return Bitwise OR of pressed modifiers from MONITORED_MODIFIERS
     */
    Qt::KeyboardModifiers getCurrentModifiersEvdev()
    {
        Qt::KeyboardModifiers mods = Qt::NoModifier;

        // Check Shift (left and right)
        if (isKeyPressedEvdev(EVDEV_KEY_LEFTSHIFT) || isKeyPressedEvdev(EVDEV_KEY_RIGHTSHIFT)) {
            mods |= Qt::ShiftModifier;
        }

        // Check Control (left and right)
        if (isKeyPressedEvdev(EVDEV_KEY_LEFTCTRL) || isKeyPressedEvdev(EVDEV_KEY_RIGHTCTRL)) {
            mods |= Qt::ControlModifier;
        }

        // Check Alt (left and right)
        if (isKeyPressedEvdev(EVDEV_KEY_LEFTALT) || isKeyPressedEvdev(EVDEV_KEY_RIGHTALT)) {
            mods |= Qt::AltModifier;
        }

        // Check AltGr (typically mapped to right Alt on international keyboards)
        // Note: This is a heuristic - not all systems map right Alt to AltGr
        if (isKeyPressedEvdev(EVDEV_KEY_RIGHTALT)) {
            mods |= Qt::GroupSwitchModifier;
        }

        return mods & MONITORED_MODIFIERS;
    }

    /**
     * @brief Checks if a specific key is pressed using X11 XQueryKeymap
     * @param keysym X11 keysym to check (e.g., XK_Shift_L)
     * @return true if key is currently pressed
     */
    bool isKeyPressedX11(KeySym keysym)
    {
        Display *display = XOpenDisplay(nullptr);
        if (!display) {
            return false;
        }

        char keys[32];
        XQueryKeymap(display, keys);

        KeyCode const kc = XKeysymToKeycode(display, keysym);
        bool const pressed = !!(keys[kc >> 3] & (1 << (kc & 7)));

        XCloseDisplay(display);
        return pressed;
    }

    /**
     * @brief Gets currently pressed modifiers using X11 XQueryKeymap
     * @return Bitwise OR of pressed modifiers from MONITORED_MODIFIERS
     */
    Qt::KeyboardModifiers getCurrentModifiersX11()
    {
        Qt::KeyboardModifiers mods = Qt::NoModifier;

        // Check Shift (left and right)
        if (isKeyPressedX11(XK_Shift_L) || isKeyPressedX11(XK_Shift_R)) {
            mods |= Qt::ShiftModifier;
        }

        // Check Control (left and right)
        if (isKeyPressedX11(XK_Control_L) || isKeyPressedX11(XK_Control_R)) {
            mods |= Qt::ControlModifier;
        }

        // Check Alt (left and right)
        if (isKeyPressedX11(XK_Alt_L) || isKeyPressedX11(XK_Alt_R)) {
            mods |= Qt::AltModifier;
        }

        // Check AltGr (ISO Level 3 Shift)
        if (isKeyPressedX11(XK_ISO_Level3_Shift)) {
            mods |= Qt::GroupSwitchModifier;
        }

        return mods & MONITORED_MODIFIERS;
    }

    /**
     * @brief Gets currently pressed modifiers that we care about
     * @return Bitwise OR of pressed modifiers from MONITORED_MODIFIERS
     *
     * Tries multiple methods in order of preference:
     * 1. evdev ioctl (works on all systems with /dev/input access)
     * 2. X11 XQueryKeymap (fallback for X11/XWayland when evdev unavailable)
     * 3. Qt::NoModifier (skip checking if no method available)
     */
    Qt::KeyboardModifiers getCurrentModifiers()
    {
        // 1. Try evdev ioctl first (works on Wayland and X11)
        if (initializeEvdevDevices()) {
            qCDebug(TextInputLog) << "Using evdev ioctl for modifier detection";
            return getCurrentModifiersEvdev();
        }

        // 2. Fallback to X11 if $DISPLAY is available
        const char* display_env = std::getenv("DISPLAY");
        if (display_env != nullptr && display_env[0] != '\0') {
            qCDebug(TextInputLog) << "Using X11 XQueryKeymap for modifier detection (evdev unavailable)";
            return getCurrentModifiersX11();
        }

        // 3. No method available - skip checking
        qCDebug(TextInputLog) << "No modifier detection method available"
                              << "(evdev: no keyboards found or permission denied, X11: not available)";
        return Qt::NoModifier;
    }
}

bool ModifierKeyChecker::hasModifiersPressed()
{
    Qt::KeyboardModifiers const modifiers = getCurrentModifiers();
    bool const hasModifiers = (modifiers != Qt::NoModifier);

    if (hasModifiers) {
        qCDebug(TextInputLog) << "ModifierKeyChecker: Detected pressed modifiers:"
                              << modifiers;
    }

    return hasModifiers;
}

bool ModifierKeyChecker::waitForModifierRelease(int timeoutMs, int pollIntervalMs)
{
    qCDebug(TextInputLog) << "ModifierKeyChecker: Waiting for modifier release"
                          << "timeout:" << timeoutMs << "ms, poll interval:" << pollIntervalMs << "ms";

    QElapsedTimer timer;
    timer.start();

    // Check immediately
    if (!hasModifiersPressed()) {
        qCDebug(TextInputLog) << "ModifierKeyChecker: No modifiers pressed (immediate check)";
        return true;
    }

    // Poll until timeout or release
    while (timer.elapsed() < timeoutMs) {
        // Process events to keep UI responsive and update keyboard state
        QCoreApplication::processEvents(QEventLoop::AllEvents, pollIntervalMs);

        // Wait for poll interval
        QThread::msleep(pollIntervalMs);

        // Check if modifiers are released
        if (!hasModifiersPressed()) {
            qCDebug(TextInputLog) << "ModifierKeyChecker: Modifiers released after"
                                  << timer.elapsed() << "ms";
            return true;
        }
    }

    qCDebug(TextInputLog) << "ModifierKeyChecker: Timeout after" << timeoutMs
                          << "ms - modifiers still pressed";
    return false;
}

QStringList ModifierKeyChecker::getPressedModifiers()
{
    Qt::KeyboardModifiers const modifiers = getCurrentModifiers();
    QStringList names;

    if (modifiers & Qt::ShiftModifier) {
        names << i18n("Shift");
    }
    if (modifiers & Qt::ControlModifier) {
        names << i18n("Ctrl");
    }
    if (modifiers & Qt::AltModifier) {
        names << i18n("Alt");
    }
    if (modifiers & Qt::GroupSwitchModifier) {
        names << i18n("AltGr");
    }

    return names;
}

} // namespace Daemon
} // namespace YubiKeyOath
