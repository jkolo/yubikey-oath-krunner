/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "../types/yubikey_model.h"
#include <QString>

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Resolves model-specific icon paths for YubiKey devices
 *
 * This class provides a centralized mechanism for selecting appropriate
 * icons based on YubiKey model information. It implements a fallback
 * strategy to ensure an icon is always available.
 *
 * Icon Selection Algorithm:
 * 1. Try exact match: series + variant + ports (e.g., "yubikey-5c-nano.png")
 * 2. Try series + ports: (e.g., "yubikey-5c-nfc.png")
 * 3. Use generic fallback: "yubikey.svg"
 *
 * Note: There's no series-only fallback (e.g., no "yubikey-5.svg") because
 * all real YubiKey models have concrete specifications (5 NFC, 5C, etc.)
 *
 * Icon Naming Convention:
 * - All lowercase, hyphen-separated
 * - Format: "yubikey-{series}{usb_type}[-{variant}][-nfc]"
 * - File format: PNG (with transparency) for real models, SVG for generic fallback
 * - Examples:
 *   - "yubikey-5-nfc.png" (YubiKey 5 NFC, USB-A + NFC)
 *   - "yubikey-5c-nfc.png" (YubiKey 5C NFC, USB-C + NFC)
 *   - "yubikey-5-nano.png" (YubiKey 5 Nano, USB-A)
 *   - "yubikey-5c-nano.png" (YubiKey 5C Nano, USB-C)
 *   - "yubikey-5ci.png" (YubiKey 5Ci, USB-C + Lightning)
 *   - "yubikey-bio.png" (YubiKey Bio USB-A)
 *   - "yubikey-bio-c.png" (YubiKey Bio USB-C)
 *   - "yubikey-neo.png" (YubiKey NEO)
 *
 * Usage Example:
 * @code
 * YubiKeyModel model = ...; // From D-Bus or detection
 * QString iconPath = YubiKeyIconResolver::getIconPath(model);
 * // iconPath = ":/icons/models/yubikey-5c-nfc.svg" or fallback
 * @endcode
 */
class YubiKeyIconResolver
{
public:
    /**
     * @brief Gets icon path for YubiKey model
     * @param model Encoded model (0xSSVVPPFF format)
     * @return Qt resource path to model-specific icon, or generic icon if not found
     *
     * Returns the most specific available icon for the given model.
     * If the exact model icon doesn't exist, falls back progressively
     * to series-level icons and finally the generic YubiKey icon.
     *
     * @note Always returns a valid path - never returns empty string
     */
    static QString getIconPath(YubiKeyModel model);

    /**
     * @brief Gets generic YubiKey icon path (fallback)
     * @return Qt resource path to generic YubiKey icon
     */
    static QString getGenericIconPath();

private:
    /**
     * @brief Builds icon filename from model components
     * @param series YubiKey series
     * @param variant YubiKey variant (Standard, Nano, DualConnector)
     * @param ports YubiKey ports (USB-A, USB-C, NFC, Lightning)
     * @param includeVariant Whether to include variant in filename
     * @return Icon filename without path or extension (e.g., "yubikey-5c-nfc")
     */
    static QString buildIconFilename(YubiKeySeries series,
                                     YubiKeyVariant variant,
                                     YubiKeyPorts ports,
                                     bool includeVariant);

    /**
     * @brief Converts series enum to string for icon filename
     * @param series YubiKey series
     * @return Lowercase series string (e.g., "5", "bio", "neo")
     */
    static QString seriesString(YubiKeySeries series);

    /**
     * @brief Converts variant enum to string for icon filename
     * @param variant YubiKey variant
     * @return Lowercase variant string (e.g., "nano", "ci", "") or empty if Standard
     */
    static QString variantString(YubiKeyVariant variant);

    /**
     * @brief Checks if icon resource exists
     * @param iconPath Qt resource path (e.g., ":/icons/models/yubikey-5c-nfc.svg")
     * @return true if resource exists, false otherwise
     */
    static bool iconExists(const QString& iconPath);

    /**
     * @brief Builds full Qt resource path from icon filename
     * @param filename Icon filename without extension (e.g., "yubikey-5c-nfc")
     * @return Full Qt resource path (e.g., ":/icons/models/yubikey-5c-nfc.svg")
     */
    static QString buildResourcePath(const QString& filename);

    // Constants
    static constexpr const char* ICON_PATH_PREFIX = ":/icons/models/";
    static constexpr const char* ICON_EXTENSION_PNG = ".png";
    static constexpr const char* ICON_EXTENSION_SVG = ".svg";
    static constexpr const char* GENERIC_ICON_PATH = ":/icons/yubikey.svg";
};

} // namespace Shared
} // namespace YubiKeyOath
