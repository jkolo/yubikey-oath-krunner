/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_config_icon_resolver.h"
#include "yubikey_config.h"
#include "../shared/types/device_brand.h"
#include "../shared/types/device_model.h"
#include "../shared/utils/yubikey_icon_resolver.h"

using namespace YubiKeyOath::Shared;

QString YubiKeyConfigIconResolver::getModelIcon(const QString& modelString,
                                                quint32 modelCode,
                                                const QStringList& capabilities) const
{
    // Reconstruct DeviceModel from available data
    DeviceModel deviceModel;
    deviceModel.brand = detectBrandFromModelString(modelString);
    deviceModel.modelCode = modelCode;
    deviceModel.modelString = modelString;
    deviceModel.formFactor = 0;  // Not used for icon resolution
    deviceModel.capabilities = capabilities;

    // Use multi-brand icon resolver (returns theme icon name)
    return YubiKeyIconResolver::getIconName(deviceModel);
}
