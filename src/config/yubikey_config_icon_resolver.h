/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "i_device_icon_resolver.h"

namespace YubiKeyOath {
namespace Config {
class YubiKeyConfig;
}
}

/**
 * @brief Adapter that implements IDeviceIconResolver by delegating to YubiKeyConfig
 *
 * This adapter allows DeviceDelegate to use YubiKeyConfig's icon resolution
 * functionality through a minimal interface, following the Interface Segregation Principle.
 *
 * The adapter holds a pointer to YubiKeyConfig and delegates getModelIcon() calls.
 *
 * Lifetime: The adapter does NOT own the YubiKeyConfig pointer - caller must ensure
 * YubiKeyConfig outlives this adapter.
 */
class YubiKeyConfigIconResolver : public IDeviceIconResolver
{
public:
    /**
     * @brief Constructs adapter with YubiKeyConfig instance
     * @param config Pointer to YubiKeyConfig (must outlive this adapter)
     */
    explicit YubiKeyConfigIconResolver(YubiKeyOath::Config::YubiKeyConfig *config);

    // IDeviceIconResolver interface
    QString getModelIcon(quint32 deviceModel) const override;

private:
    YubiKeyOath::Config::YubiKeyConfig *m_config;  // Not owned
};
