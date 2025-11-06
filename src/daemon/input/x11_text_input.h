/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "text_input_provider.h"
#include <QObject>

namespace YubiKeyOath {
namespace Daemon {

// Forward declarations
class SecretStorage;

/**
 * @brief X11-specific text input implementation
 */
class X11TextInput : public QObject, public TextInputProvider
{
    Q_OBJECT

public:
    explicit X11TextInput(SecretStorage *secretStorage, QObject *parent = nullptr);
    ~X11TextInput() override = default;

    bool typeText(const QString &text) override;
    bool isCompatible() const override;
    QString providerName() const override;
};

} // namespace Daemon
} // namespace YubiKeyOath
