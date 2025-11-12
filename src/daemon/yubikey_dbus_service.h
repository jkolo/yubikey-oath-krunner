/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <memory>
#include "types/yubikey_value_types.h"
#include "types/oath_credential.h"

// Forward declarations
namespace YubiKeyOath {
namespace Daemon {
    class YubiKeyService;
    class OathManagerObject;
}
}

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

/**
 * @brief D-Bus service for YubiKey OATH operations (thin marshaling layer)
 *
 * Single Responsibility: D-Bus marshaling - convert between D-Bus types and business logic
 *
 * This is a THIN layer that:
 * 1. Receives D-Bus method calls
 * 2. Converts D-Bus types to internal types (using TypeConversions)
 * 3. Delegates to YubiKeyService (business logic layer)
 * 4. Converts results back to D-Bus types
 * 5. Forwards signals from YubiKeyService
 *
 * @par Architecture
 * ```
 * D-Bus Client
 *     ↓ calls
 * YubiKeyDBusService (marshaling) ← YOU ARE HERE
 *     ↓ delegates
 * YubiKeyService (business logic)
 * ```
 *
 * NO business logic should be in this class!
 */
class YubiKeyDBusService : public QObject
{
    Q_OBJECT

public:
    explicit YubiKeyDBusService(QObject *parent = nullptr);
    ~YubiKeyDBusService() override;

private:
    std::unique_ptr<YubiKeyService> m_service;
    OathManagerObject *m_manager;  // Owned by QObject hierarchy (parent = this)
};

} // namespace Daemon
} // namespace YubiKeyOath
