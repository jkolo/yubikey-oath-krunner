/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "password_dialog_helper.h"
#include "password_dialog.h"
#include "logging_categories.h"
#include "../dbus/yubikey_manager_proxy.h"
#include "../dbus/yubikey_device_proxy.h"

#include <QPointer>
#include <KLocalizedString>

namespace YubiKeyOath {
namespace Shared {
namespace PasswordDialogHelper {

void showDialog(
    const QString &deviceId,
    const QString &deviceName,
    YubiKeyManagerProxy *manager,
    QObject *parent,
    std::function<void()> onPasswordSuccess)
{
    qCDebug(YubiKeyUILog) << "Showing password dialog for device:" << deviceId;

    // Create custom PasswordDialog
    auto *dlg = new PasswordDialog(deviceId, deviceName);

    // Auto-delete when closed
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    // Connect device name changed signal - updates name immediately via device proxy
    QObject::connect(dlg, &PasswordDialog::deviceNameChanged, parent,
            [manager](const QString &devId, const QString &newName) {
                qCDebug(YubiKeyUILog) << "Device name changed to:" << newName;
                YubiKeyDeviceProxy *device = manager->getDevice(devId);
                if (device) {
                    device->setName(newName);
                } else {
                    qCWarning(YubiKeyUILog) << "Device not found:" << devId;
                }
            });

    // Connect password entered signal
    // Note: Dialog already shows spinner before emitting this signal
    // Signal emits (deviceId, password), we ignore first param and use second
    QObject::connect(dlg, &PasswordDialog::passwordEntered, parent,
            [dlg, deviceId, manager, onPasswordSuccess](const QString &devId, const QString &password) {
                // Use QPointer to safely check if dialog still exists
                QPointer<PasswordDialog> dialogPtr(dlg);

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

                // Test and save password via device proxy (blocking call)
                bool success = device->savePassword(password);

                // Check if dialog still exists before accessing it
                if (!dialogPtr) {
                    qCDebug(YubiKeyUILog) << "Password dialog was closed before verification completed";
                    return;
                }

                if (success) {
                    qCDebug(YubiKeyUILog) << "Password saved successfully for device:" << devId;

                    // Success - close dialog (queued to ensure execution in GUI thread)
                    // Note: Device name is already updated via deviceNameChanged signal
                    QMetaObject::invokeMethod(dialogPtr.data(), [dialogPtr, onPasswordSuccess]() {
                        if (dialogPtr) {
                            dialogPtr->accept();
                        }
                        // Invoke success callback (e.g., notification, model refresh)
                        if (onPasswordSuccess) {
                            onPasswordSuccess();
                        }
                    }, Qt::QueuedConnection);
                } else {
                    // Invalid password - show error in dialog, keep it open
                    // Use QueuedConnection to ensure UI manipulation happens in GUI thread
                    qCWarning(YubiKeyUILog) << "Password test failed for device:" << devId;
                    QMetaObject::invokeMethod(dialogPtr.data(), [dialogPtr]() {
                        if (dialogPtr) {
                            dialogPtr->showError(i18n("Invalid password. Please try again."));
                            // showError() calls setVerifying(false) internally
                        }
                    }, Qt::QueuedConnection);
                }
            });

    QObject::connect(dlg, &QDialog::rejected, parent,
            []() {
                qCDebug(YubiKeyUILog) << "Password dialog cancelled";
            });

    // Show non-modally and bring to front
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

} // namespace PasswordDialogHelper
} // namespace Shared
} // namespace YubiKeyOath
