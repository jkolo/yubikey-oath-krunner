/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "credential_update_notifier.h"

namespace YubiKeyOath {
namespace Shared {

// Implementation required for Qt meta-object system
// Constructor and destructor are inline in header, but we need
// this .cpp file for moc to generate meta-object code

} // namespace Shared
} // namespace YubiKeyOath

// Include moc-generated code
// NOLINTNEXTLINE(bugprone-suspicious-include) - Qt moc pattern
#include "moc_credential_update_notifier.cpp"
