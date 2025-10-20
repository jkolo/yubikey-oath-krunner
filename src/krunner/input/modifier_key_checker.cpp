/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "modifier_key_checker.h"
#include "../logging_categories.h"

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QThread>
#include <KLocalizedString>

// X11 includes for XQueryKeymap
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdlib>

namespace KRunner {
namespace YubiKey {

namespace {
    // Modifiers to check (excludes Meta/Windows and Keypad)
    constexpr Qt::KeyboardModifiers MONITORED_MODIFIERS =
        Qt::ShiftModifier |
        Qt::ControlModifier |
        Qt::AltModifier |
        Qt::GroupSwitchModifier;  // AltGr

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

        KeyCode kc = XKeysymToKeycode(display, keysym);
        bool pressed = !!(keys[kc >> 3] & (1 << (kc & 7)));

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
     * Uses X11 XQueryKeymap when $DISPLAY is available (X11 or XWayland).
     * If not available (pure Wayland), returns Qt::NoModifier to skip checking.
     */
    Qt::KeyboardModifiers getCurrentModifiers()
    {
        // Check if X11/XWayland is available via $DISPLAY
        const char* display_env = std::getenv("DISPLAY");
        if (display_env != nullptr && display_env[0] != '\0') {
            // X11 or XWayland available - use XQueryKeymap for reliable detection
            qCDebug(TextInputLog) << "Using X11 XQueryKeymap for modifier detection";
            return getCurrentModifiersX11();
        }

        // Pure Wayland without XWayland - skip checking
        // (Wayland has no API to query keyboard state without focus)
        qCDebug(TextInputLog) << "X11/XWayland not available - skipping modifier check";
        return Qt::NoModifier;
    }
}

bool ModifierKeyChecker::hasModifiersPressed()
{
    Qt::KeyboardModifiers modifiers = getCurrentModifiers();
    bool hasModifiers = (modifiers != Qt::NoModifier);

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
    Qt::KeyboardModifiers modifiers = getCurrentModifiers();
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

} // namespace YubiKey
} // namespace KRunner
