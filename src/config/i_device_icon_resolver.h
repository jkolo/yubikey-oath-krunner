/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QStringList>

/**
 * @brief Interface for resolving device model-specific icons (multi-brand)
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
     * @brief Get icon path for device model (multi-brand support)
     * @param modelString Human-readable model string (e.g., "Nitrokey 3C NFC", "YubiKey 5C NFC")
     * @param modelCode Numeric model code (0xGGVVPPFF format)
     * @param capabilities Device capabilities list
     * @return Qt resource path to model-specific icon (e.g., ":/icons/models/nitrokey-3c-nfc.png")
     */
    virtual QString getModelIcon(const QString& modelString,
                                 quint32 modelCode,
                                 const QStringList& capabilities) const = 0;
};
