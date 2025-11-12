/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QObject>
#include <functional>

namespace YubiKeyOath {
namespace Shared {

class OathManagerProxy;

namespace PasswordDialogHelper {

/**
 * @brief Shows password dialog for YubiKey authentication
 * @param deviceId Device ID requiring password
 * @param deviceName Friendly device name
 * @param manager Manager proxy for communication with daemon
 * @param parent Parent QObject for dialog
 * @param onPasswordSuccess Callback invoked when password successfully saved
 *
 * Creates and shows a non-modal password dialog. The dialog allows:
 * - Entering YubiKey OATH password
 * - Editing device name (saved immediately via D-Bus)
 * - Retry on invalid password (dialog stays open with error)
 * - Cancel operation
 *
 * When password is successfully saved, calls onPasswordSuccess callback.
 * Caller is responsible for any post-success actions (e.g., notifications, model refresh).
 */
void showDialog(
    const QString &deviceId,
    const QString &deviceName,
    OathManagerProxy *manager,
    QObject *parent,
    const std::function<void()> &onPasswordSuccess
);

} // namespace PasswordDialogHelper
} // namespace Shared
} // namespace YubiKeyOath
