/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include "types/oath_credential_data.h"

// Forward declarations
namespace KRunner {
namespace YubiKey {
    class YubiKeyDBusClient;
    class NotificationOrchestrator;
}
}

namespace KRunner {
namespace YubiKey {

/**
 * @brief Orchestrates the complete workflow for adding OATH credential to YubiKey
 *
 * Workflow steps:
 * 1. Capture screenshot (interactive window selection)
 * 2. Parse QR code from screenshot
 * 3. Parse otpauth:// URI
 * 4. Show credential dialog for user review/editing
 * 5. Add credential to YubiKey via D-Bus
 * 6. Show success/error notification
 *
 * This is a one-shot workflow - create new instance for each add operation.
 */
class AddCredentialWorkflow : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs workflow coordinator
     * @param dbusClient D-Bus client for YubiKey communication
     * @param notificationOrchestrator Notification manager
     * @param parent Parent QObject
     */
    explicit AddCredentialWorkflow(YubiKeyDBusClient *dbusClient,
                                  NotificationOrchestrator *notificationOrchestrator,
                                  QObject *parent = nullptr);

    ~AddCredentialWorkflow() override;

    /**
     * @brief Starts the add credential workflow
     *
     * This method returns immediately. Progress is communicated via signals.
     * The workflow will:
     * - Show screenshot selection dialog
     * - Process QR code
     * - Show credential editor dialog
     * - Add to YubiKey
     * - Show completion notification
     */
    void start();

Q_SIGNALS:
    /**
     * @brief Emitted when workflow completes successfully
     */
    void finished();

    /**
     * @brief Emitted when workflow is cancelled by user
     */
    void cancelled();

    /**
     * @brief Emitted when workflow encounters an error
     * @param errorMessage Error description
     */
    void error(const QString &errorMessage);

private Q_SLOTS:
    void onScreenshotCaptured(const QString &filePath);
    void onScreenshotCancelled();
    void onDialogAccepted();
    void onDialogRejected();

private:
    void showErrorNotification(const QString &message);
    void showSuccessNotification(const QString &credentialName);

    YubiKeyDBusClient *m_dbusClient;
    NotificationOrchestrator *m_notificationOrchestrator;

    QString m_screenshotPath;
    OathCredentialData m_credentialData;
    QString m_selectedDeviceId;
};

} // namespace YubiKey
} // namespace KRunner
