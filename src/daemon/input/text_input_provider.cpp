/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "text_input_provider.h"
#include "../logging_categories.h"

namespace YubiKeyOath {
namespace Daemon {

TextInputProvider::TextInputProvider(QObject *parent)
    : QObject(parent)
{
}

TextInputProvider::~TextInputProvider() = default;

} // namespace Daemon
} // namespace YubiKeyOath
