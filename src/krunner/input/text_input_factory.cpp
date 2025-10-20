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

namespace {
/**
 * @brief Helper to try creating a text input provider
 * @tparam T Provider type to create
 * @param parent Parent QObject
 * @param description Human-readable description for logging
 * @return Provider instance if compatible, nullptr otherwise
 */
template<typename T>
std::unique_ptr<TextInputProvider> tryCreateProvider(QObject* parent, const char* description)
{
    auto provider = std::make_unique<T>(parent);
    if (provider->isCompatible()) {
        qCDebug(TextInputLog) << "TextInputFactory: Created" << description << "provider";
        return provider;
    }
    return nullptr;
}
} // anonymous namespace

std::unique_ptr<TextInputProvider> TextInputFactory::createProvider(QObject *parent)
{
    // Try providers in priority order: Portal → Wayland → X11
    if (auto provider = tryCreateProvider<PortalTextInput>(parent, "Portal (xdg-desktop-portal + libei)")) {
        return provider;
    }

    if (auto provider = tryCreateProvider<WaylandTextInput>(parent, "Wayland (legacy KWayland FakeInput)")) {
        return provider;
    }

    if (auto provider = tryCreateProvider<X11TextInput>(parent, "X11")) {
        return provider;
    }

    qCWarning(TextInputLog) << "TextInputFactory: No compatible text input provider found";
    return nullptr;
}

} // namespace YubiKey
} // namespace KRunner
