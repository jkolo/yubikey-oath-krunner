/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "change_password_dialog_helper.h"
#include "change_password_dialog.h"
#include "logging_categories.h"
#include "../dbus/yubikey_manager_proxy.h"
#include "../dbus/yubikey_device_proxy.h"

#include <QPointer>
#include <KLocalizedString>

namespace YubiKeyOath {
namespace Shared {
namespace ChangePasswordDialogHelper {

void showDialog(
    const QString &deviceId,
    const QString &deviceName,
    bool requiresPassword,
    YubiKeyManagerProxy *manager,
    QObject *parent,
    std::function<void()> onPasswordChangeSuccess)
{
    qCDebug(YubiKeyUILog) << "Showing change password dialog for device:" << deviceId
                          << "requiresPassword:" << requiresPassword;

    // Create change password dialog
    auto *dlg = new ChangePasswordDialog(deviceId, deviceName, requiresPassword);

    // Auto-delete when closed
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    // Connect password change requested signal
    QObject::connect(dlg, &ChangePasswordDialog::passwordChangeRequested, parent,
            [dlg, deviceId, manager, onPasswordChangeSuccess](
                const QString &devId,
                const QString &oldPassword,
                const QString &newPassword) {
                // Use QPointer to safely check if dialog still exists
                QPointer<ChangePasswordDialog> dialogPtr(dlg);

                // Get device proxy
                YubiKeyDeviceProxy *device = manager->getDevice(devId);
                if (!device) {
                    qCWarning(YubiKeyUILog) << "Device not found:" << devId;
                    if (dialogPtr) {
                        QMetaObject::invokeMethod(dialogPtr.data(), [dialogPtr]() {
                            if (dialogPtr) {
                                dialogPtr->showError(i18n("Device not found"));
                            }
                        }, Qt::QueuedConnection);
                    }
                    return;
                }

                // Change password via device proxy (blocking call) - get detailed error message
                QString errorMessage;
                bool success = device->changePassword(oldPassword, newPassword, errorMessage);

                // Check if dialog still exists before accessing it
                if (!dialogPtr) {
                    qCDebug(YubiKeyUILog) << "Change password dialog was closed before operation completed";
                    return;
                }

                if (success) {
                    if (newPassword.isEmpty()) {
                        qCDebug(YubiKeyUILog) << "Password removed successfully for device:" << devId;
                    } else {
                        qCDebug(YubiKeyUILog) << "Password changed successfully for device:" << devId;
                    }

                    // Success - close dialog
                    QMetaObject::invokeMethod(dialogPtr.data(), [dialogPtr, onPasswordChangeSuccess]() {
                        if (dialogPtr) {
                            dialogPtr->accept();
                        }
                        // Invoke success callback (e.g., notification, model refresh)
                        if (onPasswordChangeSuccess) {
                            onPasswordChangeSuccess();
                        }
                    }, Qt::QueuedConnection);
                } else {
                    // Failed - show detailed error in dialog, keep it open
                    qCWarning(YubiKeyUILog) << "Password change failed for device:" << devId << "Error:" << errorMessage;
                    QMetaObject::invokeMethod(dialogPtr.data(), [dialogPtr, errorMessage]() {
                        if (dialogPtr) {
                            // Use detailed error message if available, otherwise use generic message
                            QString displayError = errorMessage.isEmpty()
                                ? i18n("Failed to change password.\n"
                                       "The current password may be incorrect, or the YubiKey may not be accessible.")
                                : errorMessage;
                            dialogPtr->showError(displayError);
                        }
                    }, Qt::QueuedConnection);
                }
            });

    // Show dialog (non-modal)
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

} // namespace ChangePasswordDialogHelper
} // namespace Shared
} // namespace YubiKeyOath
