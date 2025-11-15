/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "i_device_icon_resolver.h"

namespace YubiKeyOath {
namespace Config {
class OathConfig;
}
}

/**
 * @brief Adapter that implements IDeviceIconResolver for multi-brand icon resolution
 *
 * This adapter allows DeviceDelegate to use YubiKeyIconResolver's multi-brand icon resolution
 * functionality through a minimal interface, following the Interface Segregation Principle.
 *
 * The adapter reconstructs DeviceModel from available data and delegates to
 * YubiKeyIconResolver::getIconPath() static method.
 */
class OathConfigIconResolver : public IDeviceIconResolver
{
public:
    /**
     * @brief Constructs adapter for icon resolution
     *
     * No configuration needed - uses static YubiKeyIconResolver methods.
     */
    OathConfigIconResolver() = default;

    // IDeviceIconResolver interface
    QString getModelIcon(const QString& modelString,
                        quint32 modelCode,
                        const QStringList& capabilities) const override;
};
