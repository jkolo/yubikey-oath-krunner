/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_config_icon_resolver.h"
#include "yubikey_config.h"

YubiKeyConfigIconResolver::YubiKeyConfigIconResolver(YubiKeyOath::Config::YubiKeyConfig *config)
    : m_config(config)
{
}

QString YubiKeyConfigIconResolver::getModelIcon(quint32 deviceModel) const
{
    return m_config->getModelIcon(deviceModel);
}
