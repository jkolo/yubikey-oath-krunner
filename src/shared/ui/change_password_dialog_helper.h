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

namespace ChangePasswordDialogHelper {

/**
 * @brief Shows change password dialog for YubiKey
 * @param deviceId Device ID
 * @param deviceName Friendly device name
 * @param requiresPassword Whether device currently requires password
 * @param manager Manager proxy for communication with daemon
 * @param parent Parent QObject for dialog
 * @param onPasswordChangeSuccess Callback invoked when password successfully changed
 *
 * Creates and shows a non-modal change password dialog. The dialog allows:
 * - Entering current password (optional if requiresPassword=false)
 * - Setting new password with confirmation
 * - Removing password protection (via checkbox)
 * - Retry on failure (dialog stays open with error)
 * - Cancel operation
 *
 * When password is successfully changed, calls onPasswordChangeSuccess callback.
 * Caller is responsible for any post-success actions (e.g., notifications, model refresh).
 */
void showDialog(
    const QString &deviceId,
    const QString &deviceName,
    bool requiresPassword,
    OathManagerProxy *manager,
    QObject *parent,
    const std::function<void()> &onPasswordChangeSuccess
);

} // namespace ChangePasswordDialogHelper
} // namespace Shared
} // namespace YubiKeyOath
