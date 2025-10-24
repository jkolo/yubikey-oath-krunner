/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "add_credential_workflow.h"
#include "dbus/yubikey_dbus_client.h"
#include "../utils/screenshot_capture.h"
#include "../utils/qr_code_parser.h"
#include "../utils/otpauth_uri_parser.h"
#include "../ui/add_credential_dialog.h"
#include "notification_orchestrator.h"
#include "../logging_categories.h"

#include <QTimer>
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

AddCredentialWorkflow::AddCredentialWorkflow(YubiKeyDBusClient *dbusClient,
                                            NotificationOrchestrator *notificationOrchestrator,
                                            QObject *parent)
    : QObject(parent)
    , m_dbusClient(dbusClient)
    , m_notificationOrchestrator(notificationOrchestrator)
{
}

AddCredentialWorkflow::~AddCredentialWorkflow() = default;

void AddCredentialWorkflow::start()
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialWorkflow: Starting workflow";

    // Step 1: Capture screenshot
    auto *screenshotCapture = new ScreenshotCapture(this);

    connect(screenshotCapture, &ScreenshotCapture::screenshotCaptured,
            this, &AddCredentialWorkflow::onScreenshotCaptured);
    connect(screenshotCapture, &ScreenshotCapture::screenshotCancelled,
            this, &AddCredentialWorkflow::onScreenshotCancelled);

    // Start capture in next event loop iteration to allow signal connections
    QTimer::singleShot(0, [screenshotCapture]() {
        screenshotCapture->captureInteractive();
    });
}

void AddCredentialWorkflow::onScreenshotCaptured(const QString &filePath)
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialWorkflow: Screenshot captured:" << filePath;

    m_screenshotPath = filePath;

    // Step 2: Parse QR code from screenshot
    auto qrResult = QrCodeParser::parse(filePath);

    if (qrResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "AddCredentialWorkflow: QR code parsing failed:"
                                     << qrResult.error();
        showErrorNotification(i18n("No QR code found in screenshot: %1", qrResult.error()));
        Q_EMIT error(qrResult.error());
        return;
    }

    QString otpauthUri = qrResult.value();
    qCDebug(YubiKeyDaemonLog) << "AddCredentialWorkflow: Decoded QR code, URI length:"
                               << otpauthUri.length();

    // Step 3: Parse otpauth:// URI
    auto parseResult = OtpauthUriParser::parse(otpauthUri);

    if (parseResult.isError()) {
        qCWarning(YubiKeyDaemonLog) << "AddCredentialWorkflow: URI parsing failed:"
                                     << parseResult.error();
        showErrorNotification(i18n("Invalid otpauth:// URI: %1", parseResult.error()));
        Q_EMIT error(parseResult.error());
        return;
    }

    m_credentialData = parseResult.value();
    qCDebug(YubiKeyDaemonLog) << "AddCredentialWorkflow: Parsed credential:"
                               << m_credentialData.name;

    // Step 4: Get available devices
    QList<DeviceInfo> devices = m_dbusClient->listDevices();
    QStringList deviceIds;
    for (const auto &device : devices) {
        if (device.isConnected) {
            deviceIds.append(device.deviceId);
        }
    }

    if (deviceIds.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "AddCredentialWorkflow: No YubiKey devices available";
        showErrorNotification(i18n("No YubiKey devices connected"));
        Q_EMIT error(i18n("No YubiKey devices connected"));
        return;
    }

    // Step 5: Show credential dialog for review/editing
    auto *dialog = new AddCredentialDialog(m_credentialData, deviceIds, nullptr);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    connect(dialog, &QDialog::accepted, this, &AddCredentialWorkflow::onDialogAccepted);
    connect(dialog, &QDialog::rejected, this, &AddCredentialWorkflow::onDialogRejected);

    // Store dialog pointer to retrieve data later
    dialog->setProperty("credentialDialog", QVariant::fromValue(static_cast<QObject*>(dialog)));

    dialog->show();
}

void AddCredentialWorkflow::onScreenshotCancelled()
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialWorkflow: Screenshot cancelled by user";
    Q_EMIT cancelled();
}

void AddCredentialWorkflow::onDialogAccepted()
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialWorkflow: Dialog accepted, adding credential";

    // Get dialog from sender
    auto *dialog = qobject_cast<AddCredentialDialog*>(sender());
    if (!dialog) {
        qCWarning(YubiKeyDaemonLog) << "AddCredentialWorkflow: Dialog cast failed";
        showErrorNotification(i18n("Internal error: dialog cast failed"));
        Q_EMIT error(i18n("Internal error"));
        return;
    }

    // Get final credential data
    m_credentialData = dialog->getCredentialData();
    m_selectedDeviceId = dialog->getSelectedDeviceId();

    if (m_selectedDeviceId.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "AddCredentialWorkflow: No device selected";
        showErrorNotification(i18n("No device selected"));
        Q_EMIT error(i18n("No device selected"));
        return;
    }

    qCDebug(YubiKeyDaemonLog) << "AddCredentialWorkflow: Adding credential" << m_credentialData.name
                               << "to device" << m_selectedDeviceId;

    // Step 6: Add credential to YubiKey via D-Bus
    QString errorMsg = m_dbusClient->addCredential(
        m_selectedDeviceId,
        m_credentialData.name,
        m_credentialData.secret,
        m_credentialData.type == OathType::TOTP ? QStringLiteral("TOTP") : QStringLiteral("HOTP"),
        algorithmToString(m_credentialData.algorithm),
        m_credentialData.digits,
        m_credentialData.period,
        static_cast<int>(m_credentialData.counter),
        m_credentialData.requireTouch
    );

    if (!errorMsg.isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "AddCredentialWorkflow: Failed to add credential:" << errorMsg;
        showErrorNotification(i18n("Failed to add credential: %1", errorMsg));
        Q_EMIT error(errorMsg);
        return;
    }

    // Success!
    qCDebug(YubiKeyDaemonLog) << "AddCredentialWorkflow: Credential added successfully";
    showSuccessNotification(m_credentialData.name);
    Q_EMIT finished();
}

void AddCredentialWorkflow::onDialogRejected()
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialWorkflow: Dialog cancelled by user";
    Q_EMIT cancelled();
}

void AddCredentialWorkflow::showErrorNotification(const QString &message)
{
    if (m_notificationOrchestrator) {
        m_notificationOrchestrator->showSimpleNotification(
            i18n("Add Credential Failed"),
            message,
            2 // Error type
        );
    }
}

void AddCredentialWorkflow::showSuccessNotification(const QString &credentialName)
{
    if (m_notificationOrchestrator) {
        m_notificationOrchestrator->showSimpleNotification(
            i18n("Credential Added"),
            i18n("Successfully added %1 to YubiKey", credentialName),
            0 // Info type
        );
    }
}

} // namespace YubiKey
} // namespace KRunner
