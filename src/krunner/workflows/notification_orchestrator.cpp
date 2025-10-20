/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "notification_orchestrator.h"
#include "../config/configuration_provider.h"
#include "../notification/dbus_notification_manager.h"
#include "notification_helper.h"
#include "../logging_categories.h"

#include <KLocalizedString>
#include <KNotification>
#include <QTimer>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

NotificationOrchestrator::NotificationOrchestrator(DBusNotificationManager *notificationManager,
                                                   const ConfigurationProvider *config,
                                                   QObject *parent)
    : QObject(parent)
    , m_notificationManager(notificationManager)
    , m_config(config)
    , m_codeUpdateTimer(new QTimer(this))
    , m_touchUpdateTimer(new QTimer(this))
{
    connect(m_codeUpdateTimer, &QTimer::timeout, this, &NotificationOrchestrator::updateCodeNotification);
    connect(m_touchUpdateTimer, &QTimer::timeout, this, &NotificationOrchestrator::updateTouchNotification);

    connect(m_notificationManager, &DBusNotificationManager::actionInvoked,
            this, &NotificationOrchestrator::onNotificationActionInvoked);
    connect(m_notificationManager, &DBusNotificationManager::notificationClosed,
            this, &NotificationOrchestrator::onNotificationClosed);
}

NotificationOrchestrator::~NotificationOrchestrator() = default;

void NotificationOrchestrator::showCodeNotification(const QString &code,
                                                    const QString &credentialName,
                                                    int expirationSeconds)
{
    if (!m_config->showNotifications() || !m_notificationManager || !m_notificationManager->isAvailable()) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing code notification for:" << credentialName
             << "expiration:" << expirationSeconds << "seconds";

    // Store state for updates
    m_codeExpirationTime = QDateTime::currentDateTime().addSecs(expirationSeconds);
    m_currentCredentialName = credentialName;
    m_currentCode = code;

    // Format notification body - simple and concise
    QString body = i18n("%1 • Copied\n", credentialName.toHtmlEscaped());
    body += i18n("Expires in %1s", expirationSeconds);

    // Prepare hints with progress bar
    QVariantMap hints;
    hints[QStringLiteral("urgency")] = 1; // Normal urgency
    hints[QStringLiteral("value")] = 100; // Start at 100%

    // Show notification without timeout - we'll close it manually
    m_codeNotificationId = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        m_codeNotificationId, // replaces_id
        QStringLiteral(":/icons/yubikey.svg"),
        i18n("Code Copied"),
        body,
        QStringList(), // No actions
        hints,
        0 // no timeout - we manage closing manually
    );

    qCDebug(NotificationOrchestratorLog) << "Code notification shown with ID:" << m_codeNotificationId;

    // Start timer to update notification every second with progress bar
    m_codeUpdateTimer->start(1000);
}

void NotificationOrchestrator::showTouchNotification(const QString &credentialName, int timeoutSeconds)
{
    if (!m_config->showNotifications() || !m_notificationManager || !m_notificationManager->isAvailable()) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing touch notification for:" << credentialName
             << "timeout:" << timeoutSeconds << "seconds";

    // Close any existing touch notification
    if (m_touchNotificationId != 0) {
        m_notificationManager->closeNotification(m_touchNotificationId);
        m_touchNotificationId = 0;
    }

    // Store state for updates
    m_touchExpirationTime = QDateTime::currentDateTime().addSecs(timeoutSeconds);
    m_touchCredentialName = credentialName;

    // Format message - simple and concise
    QString body = i18n("Timeout in %1s", timeoutSeconds);

    // Prepare hints with progress bar
    QVariantMap hints;
    hints[QStringLiteral("urgency")] = 1; // Normal urgency
    hints[QStringLiteral("value")] = 100; // Start at 100%

    // Add Cancel action
    QStringList actions;
    actions << QStringLiteral("cancel") << i18n("Cancel");

    // Show notification without timeout - we'll update it manually
    m_touchNotificationId = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        m_touchNotificationId, // replaces_id
        QStringLiteral(":/icons/yubikey.svg"),
        i18n("Please touch your YubiKey"),
        body,
        actions,
        hints,
        0 // no timeout - we manage closing manually
    );

    qCDebug(NotificationOrchestratorLog) << "Touch notification shown with ID:" << m_touchNotificationId;

    // Start timer to update notification every second with progress bar
    m_touchUpdateTimer->start(1000);
}

void NotificationOrchestrator::closeTouchNotification()
{
    if (m_touchNotificationId != 0 && m_notificationManager) {
        qCDebug(NotificationOrchestratorLog) << "Closing touch notification ID:" << m_touchNotificationId;
        m_notificationManager->closeNotification(m_touchNotificationId);
        m_touchNotificationId = 0;
    }

    // Stop update timer
    m_touchUpdateTimer->stop();

    // Fallback: close old KNotification if it still exists
    if (m_touchNotification) {
        m_touchNotification->close();
        m_touchNotification = nullptr;
    }
}

void NotificationOrchestrator::showSimpleNotification(const QString &title, const QString &message, int type)
{
    Q_UNUSED(type)

    if (!m_config->showNotifications()) {
        return;
    }

    auto notification = new KNotification(QStringLiteral("yubikey-oath"),
                                         KNotification::CloseOnTimeout,
                                         nullptr);
    notification->setComponentName(QStringLiteral("krunner_yubikey"));
    notification->setTitle(title);
    notification->setText(message);
    notification->setIconName(QStringLiteral(":/icons/yubikey.svg"));

    notification->sendEvent();
}

void NotificationOrchestrator::updateCodeNotification()
{
    if (m_codeNotificationId == 0 || !m_notificationManager) {
        m_codeUpdateTimer->stop();
        return;
    }

    // Calculate remaining time
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 expirationTime = m_codeExpirationTime.toSecsSinceEpoch();
    int remainingSeconds = static_cast<int>(expirationTime - now);

    if (remainingSeconds <= 0) {
        // Time's up, close notification
        qCDebug(NotificationOrchestratorLog) << "Code expired, closing notification";
        m_notificationManager->closeNotification(m_codeNotificationId);
        m_codeNotificationId = 0;
        m_codeUpdateTimer->stop();
        return;
    }

    // Calculate progress (100% at start, 0% at end)
    int totalSeconds = NotificationHelper::calculateNotificationDuration(m_config);
    int progress = (remainingSeconds * 100) / totalSeconds;

    qCDebug(NotificationOrchestratorLog) << "Updating code notification - remaining:" << remainingSeconds
             << "progress:" << progress << "%";

    // Update notification body with countdown - simple and concise
    QString body = i18n("%1 • Copied\n", m_currentCredentialName.toHtmlEscaped());
    body += i18n("Expires in %1s", remainingSeconds);

    // Update hints with new progress value
    QVariantMap hints;
    hints[QStringLiteral("urgency")] = 1;
    hints[QStringLiteral("value")] = progress;

    m_codeNotificationId = m_notificationManager->updateNotification(
        m_codeNotificationId,
        i18n("Code Copied"),
        body,
        hints,
        0 // no timeout
    );
}

void NotificationOrchestrator::updateTouchNotification()
{
    if (m_touchNotificationId == 0 || !m_notificationManager) {
        m_touchUpdateTimer->stop();
        return;
    }

    // Calculate remaining time
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 expirationTime = m_touchExpirationTime.toSecsSinceEpoch();
    int remainingSeconds = static_cast<int>(expirationTime - now);

    if (remainingSeconds <= 0) {
        // Time's up - show timeout message then close
        qCDebug(NotificationOrchestratorLog) << "Touch timeout, showing timeout message";
        m_touchUpdateTimer->stop();

        // Show timeout message - simple and concise
        QString body = i18n("Operation cancelled");

        QVariantMap hints;
        hints[QStringLiteral("urgency")] = 1;
        hints[QStringLiteral("value")] = 0; // 0% - timeout reached

        m_notificationManager->updateNotification(
            m_touchNotificationId,
            i18n("Touch Timeout"),
            body,
            hints,
            5000 // Auto-close after 5 seconds
        );

        m_touchNotificationId = 0;
        return;
    }

    // Calculate progress (100% at start, 0% at end)
    int totalSeconds = m_config->touchTimeout();
    int progress = (remainingSeconds * 100) / totalSeconds;

    qCDebug(NotificationOrchestratorLog) << "Updating touch notification - remaining:" << remainingSeconds
             << "progress:" << progress << "%";

    // Update notification body with countdown - simple and concise
    QString body = i18n("Timeout in %1s", remainingSeconds);

    // Update hints with new progress value
    QVariantMap hints;
    hints[QStringLiteral("urgency")] = 1;
    hints[QStringLiteral("value")] = progress;

    m_touchNotificationId = m_notificationManager->updateNotification(
        m_touchNotificationId,
        i18n("Please touch your YubiKey"),
        body,
        hints,
        0 // no timeout
    );
}

void NotificationOrchestrator::onNotificationActionInvoked(uint id, const QString &actionKey)
{
    qCDebug(NotificationOrchestratorLog) << "Notification action invoked - ID:" << id << "action:" << actionKey;

    if (id == m_touchNotificationId && actionKey == QStringLiteral("cancel")) {
        qCDebug(NotificationOrchestratorLog) << "User cancelled touch operation via notification";
        closeTouchNotification();
        Q_EMIT touchCancelled();
    }
}

void NotificationOrchestrator::onNotificationClosed(uint id, uint reason)
{
    qCDebug(NotificationOrchestratorLog) << "Notification closed - ID:" << id << "reason:" << reason;

    if (id == m_codeNotificationId) {
        qCDebug(NotificationOrchestratorLog) << "Code notification closed";
        m_codeNotificationId = 0;
        m_codeUpdateTimer->stop();
    } else if (id == m_touchNotificationId) {
        qCDebug(NotificationOrchestratorLog) << "Touch notification closed";
        m_touchNotificationId = 0;
        m_touchUpdateTimer->stop();
    }
}

} // namespace YubiKey
} // namespace KRunner
