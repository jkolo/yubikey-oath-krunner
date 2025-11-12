/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../types/yubikey_model.h"
#include "../types/device_model.h"
#include <QString>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Resolves model-specific icon theme names for OATH devices (YubiKey, Nitrokey, etc.)
 *
 * This class provides a centralized mechanism for selecting appropriate
 * icon theme names based on device model information. Icons are installed
 * in the standard freedesktop.org hicolor theme following best practices.
 *
 * Icon Selection Algorithm:
 * 1. Try exact match: series + variant + ports (e.g., "yubikey-5c-nano")
 * 2. Try series + ports: (e.g., "yubikey-5c-nfc")
 * 3. Use generic fallback: "yubikey-oath"
 *
 * Icons are installed in /usr/share/icons/hicolor/{SIZE}/devices/
 * and the Qt icon theme system handles automatic fallback and size selection.
 *
 * Note: There's no series-only fallback (e.g., no "yubikey-5") because
 * all real YubiKey models have concrete specifications (5 NFC, 5C, etc.)
 *
 * Icon Naming Convention:
 * - All lowercase, hyphen-separated
 * - Format: "yubikey-{series}{usb_type}[-{variant}][-nfc]"
 * - No file extension (theme system handles PNG/SVG selection)
 * - Examples:
 *   - "yubikey-5-nfc" (YubiKey 5 NFC, USB-A + NFC)
 *   - "yubikey-5c-nfc" (YubiKey 5C NFC, USB-C + NFC)
 *   - "yubikey-5-nano" (YubiKey 5 Nano, USB-A)
 *   - "yubikey-5c-nano" (YubiKey 5C Nano, USB-C)
 *   - "yubikey-5ci" (YubiKey 5Ci, USB-C + Lightning)
 *   - "nitrokey-3c" (Nitrokey 3C)
 *   - "yubikey-oath" (generic fallback)
 *
 * Usage Example:
 * @code
 * DeviceModel model = ...; // From D-Bus or detection
 * QString iconName = YubiKeyIconResolver::getIconName(model);
 * QIcon icon = QIcon::fromTheme(iconName);
 * // icon will automatically select appropriate size and fallback
 * @endcode
 */
class YubiKeyIconResolver
{
public:
    /**
     * @brief Gets icon theme name for device model (multi-brand support)
     * @param deviceModel Device model with brand and model string
     * @return Icon theme name for model-specific icon, or generic icon if not found
     *
     * Returns the most specific available icon name for the given device model.
     * Supports multiple brands (YubiKey, Nitrokey, etc.) with brand-specific
     * icon naming conventions and fallback strategies.
     *
     * The returned name can be used with QIcon::fromTheme() and will automatically
     * select the appropriate size and format (PNG/SVG) from the hicolor theme.
     *
     * @note Always returns a valid icon name - never returns empty string
     */
    static QString getIconName(const DeviceModel& deviceModel);

    /**
     * @brief Gets icon theme name for YubiKey model (legacy overload)
     * @param model Encoded YubiKey model (0xSSVVPPFF format)
     * @return Icon theme name for model-specific icon, or generic icon if not found
     *
     * @deprecated Use getIconName(const DeviceModel&) for multi-brand support
     */
    static QString getIconName(YubiKeyModel model);

    /**
     * @brief Gets generic OATH icon theme name (fallback)
     * @return Icon theme name for generic OATH icon ("yubikey-oath")
     */
    static QString getGenericIconName();

private:
    /**
     * @brief Gets icon theme name for Nitrokey device
     * @param deviceModel Device model with Nitrokey brand
     * @return Icon theme name for Nitrokey icon ("nitrokey-3c", "nitrokey-3a", etc.)
     */
    static QString getNitrokeyIconName(const DeviceModel& deviceModel);

    /**
     * @brief Gets icon theme name for YubiKey device
     * @param model YubiKey model code
     * @return Icon theme name for YubiKey icon ("yubikey-5c-nfc", "yubikey-neo", etc.)
     */
    static QString getYubiKeyIconName(YubiKeyModel model);

    /**
     * @brief Builds icon theme name from model components
     * @param series YubiKey series
     * @param variant YubiKey variant (Standard, Nano, DualConnector)
     * @param ports YubiKey ports (USB-A, USB-C, NFC, Lightning)
     * @param includeVariant Whether to include variant in name
     * @return Icon theme name without extension (e.g., "yubikey-5c-nfc")
     */
    static QString buildIconName(YubiKeySeries series,
                                  YubiKeyVariant variant,
                                  YubiKeyPorts ports,
                                  bool includeVariant);

    /**
     * @brief Converts series enum to string for icon theme name
     * @param series YubiKey series
     * @return Lowercase series string (e.g., "5", "bio", "neo")
     */
    static QString seriesString(YubiKeySeries series);

    /**
     * @brief Converts variant enum to string for icon theme name
     * @param variant YubiKey variant
     * @return Lowercase variant string (e.g., "nano", "ci", "") or empty if Standard
     */
    static QString variantString(YubiKeyVariant variant);

    // Constants
    static const QString GENERIC_ICON_NAME;
};

} // namespace Shared
} // namespace YubiKeyOath
