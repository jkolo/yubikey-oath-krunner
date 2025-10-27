/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QLoggingCategory>

namespace YubiKeyOath {
namespace Config {

/**
 * @brief Qt Logging Categories for YubiKey Configuration Module
 *
 * Control via environment:
 *   QT_LOGGING_RULES="pl.jkolo.yubikey.oath.daemon.config=true"
 */

Q_DECLARE_LOGGING_CATEGORY(YubiKeyConfigLog)

} // namespace Config
} // namespace YubiKeyOath
