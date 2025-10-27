/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "configuration_provider.h"

namespace YubiKeyOath {
namespace Shared {

// This file provides implementation location for ConfigurationProvider
// to resolve vtable issues with Qt MOC.
//
// Qt's Meta-Object Compiler (MOC) requires at least one non-inline
// virtual function implementation in a .cpp file for classes with Q_OBJECT.
// Without this, the linker cannot find the vtable, causing compilation errors
// like "undefined reference to `vtable for ConfigurationProvider'".
//
// The destructor is moved from inline (in .h) to out-of-line (here)
// to serve as the anchor point for the vtable.

ConfigurationProvider::~ConfigurationProvider() = default;

} // namespace Shared
} // namespace YubiKeyOath
