/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "text_input_factory.h"
#include "portal_text_input.h"
#include "wayland_text_input.h"
#include "x11_text_input.h"
#include "../logging_categories.h"

#include <QDebug>

namespace KRunner {
namespace YubiKey {

std::unique_ptr<TextInputProvider> TextInputFactory::createProvider(QObject *parent)
{
    // Try modern xdg-desktop-portal approach first (recommended for Wayland)
    auto portalProvider = std::make_unique<PortalTextInput>(parent);
    if (portalProvider->isCompatible()) {
        qCDebug(TextInputLog) << "TextInputFactory: Created Portal provider (xdg-desktop-portal + libei)";
        return portalProvider;
    }

    // Fallback to legacy KWayland FakeInput
    auto waylandProvider = std::make_unique<WaylandTextInput>(parent);
    if (waylandProvider->isCompatible()) {
        qCDebug(TextInputLog) << "TextInputFactory: Created Wayland provider (legacy KWayland FakeInput)";
        return waylandProvider;
    }

    // Try X11
    auto x11Provider = std::make_unique<X11TextInput>(parent);
    if (x11Provider->isCompatible()) {
        qCDebug(TextInputLog) << "TextInputFactory: Created X11 provider";
        return x11Provider;
    }

    qCWarning(TextInputLog) << "TextInputFactory: No compatible text input provider found";
    return nullptr;
}

} // namespace YubiKey
} // namespace KRunner
