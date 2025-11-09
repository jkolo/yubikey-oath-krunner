/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

/**
 * @brief Interface for resolving YubiKey model-specific icons
 *
 * This interface follows the Interface Segregation Principle (ISP),
 * allowing classes like DeviceDelegate to depend only on icon resolution
 * functionality without coupling to the entire KCModule.
 *
 * Implementations may delegate to YubiKeyIconResolver or provide
 * custom icon resolution logic.
 */
class IDeviceIconResolver
{
public:
    virtual ~IDeviceIconResolver() = default;

    /**
     * @brief Get icon path for a YubiKey model
     * @param deviceModel Device model encoded as quint32 (from YubiKeyModel)
     * @return Qt resource path to model-specific icon (e.g., ":/icons/models/yubikey-5c-nfc.png")
     */
    virtual QString getModelIcon(quint32 deviceModel) const = 0;
};
