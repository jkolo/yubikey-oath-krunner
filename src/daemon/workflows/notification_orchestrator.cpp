/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "notification_orchestrator.h"
#include "config/configuration_provider.h"
#include "../notification/dbus_notification_manager.h"
#include "notification_helper.h"
#include "notification_utils.h"
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
    , m_modifierUpdateTimer(new QTimer(this))
{
    connect(m_codeUpdateTimer, &QTimer::timeout, this, &NotificationOrchestrator::updateCodeNotification);
    connect(m_touchUpdateTimer, &QTimer::timeout, this, &NotificationOrchestrator::updateTouchNotification);
    connect(m_modifierUpdateTimer, &QTimer::timeout, this, &NotificationOrchestrator::updateModifierNotification);

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
    m_codeTotalSeconds = expirationSeconds;
    m_currentCredentialName = credentialName;
    m_currentCode = code;

    // Format notification body: "CODE (copied) • expires in XXs"
    QString body = i18n("%1 (copied) • expires in %2s", code, expirationSeconds);

    // Prepare hints with progress bar (100% at start)
    QVariantMap hints = NotificationUtils::createNotificationHints(1, 100);

    // Show notification without timeout - we'll close it manually
    m_codeNotificationId = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        m_codeNotificationId, // replaces_id
        QStringLiteral(":/icons/yubikey.svg"),
        credentialName,
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

    // Prepare hints with progress bar (100% at start)
    QVariantMap hints = NotificationUtils::createNotificationHints(1, 100);

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

void NotificationOrchestrator::showModifierReleaseNotification(const QStringList& modifiers, int timeoutSeconds)
{
    if (!m_config->showNotifications() || !m_notificationManager || !m_notificationManager->isAvailable()) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing modifier release notification"
                                         << "modifiers:" << modifiers
                                         << "timeout:" << timeoutSeconds << "seconds";

    // Close any existing modifier notification
    if (m_modifierNotificationId != 0) {
        m_notificationManager->closeNotification(m_modifierNotificationId);
        m_modifierNotificationId = 0;
    }

    // Store state for updates
    m_modifierExpirationTime = QDateTime::currentDateTime().addSecs(timeoutSeconds);
    m_currentModifiers = modifiers;

    // Format message
    QString modifierList = modifiers.join(QStringLiteral(", "));
    QString body = i18n("Pressed keys: %1\n", modifierList);
    body += i18n("Timeout in %1s", timeoutSeconds);

    // Prepare hints with progress bar (100% at start)
    QVariantMap hints = NotificationUtils::createNotificationHints(1, 100);

    // Show notification without timeout - we'll update it manually
    m_modifierNotificationId = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        m_modifierNotificationId, // replaces_id
        QStringLiteral(":/icons/yubikey.svg"),
        i18n("Please release modifier keys"),
        body,
        QStringList(), // No actions
        hints,
        0 // no timeout - we manage closing manually
    );

    qCDebug(NotificationOrchestratorLog) << "Modifier notification shown with ID:" << m_modifierNotificationId;

    // Start timer to update notification every second with progress bar
    m_modifierUpdateTimer->start(1000);
}

void NotificationOrchestrator::closeModifierNotification()
{
    if (m_modifierNotificationId != 0 && m_notificationManager) {
        qCDebug(NotificationOrchestratorLog) << "Closing modifier notification ID:" << m_modifierNotificationId;
        m_notificationManager->closeNotification(m_modifierNotificationId);
        m_modifierNotificationId = 0;
    }

    // Stop update timer
    m_modifierUpdateTimer->stop();
}

void NotificationOrchestrator::showModifierCancelNotification()
{
    if (!m_config->showNotifications()) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing modifier cancel notification";

    auto notification = new KNotification(QStringLiteral("yubikey-oath"),
                                         KNotification::CloseOnTimeout,
                                         nullptr);
    notification->setComponentName(QStringLiteral("krunner_yubikey"));
    notification->setTitle(i18n("Code Input Cancelled"));
    notification->setText(i18n("Modifier keys were held down for too long"));
    notification->setIconName(QStringLiteral(":/icons/yubikey.svg"));

    notification->sendEvent();
}

void NotificationOrchestrator::updateNotificationWithProgress(
    uint& notificationId,
    QTimer* updateTimer,
    const QDateTime& expirationTime,
    int totalSeconds,
    const QString& title,
    std::function<QString(int)> bodyFormatter,
    std::function<void()> onExpired)
{
    if (notificationId == 0 || !m_notificationManager) {
        updateTimer->stop();
        return;
    }

    // Calculate timer progress using helper
    auto progress = NotificationHelper::calculateTimerProgress(expirationTime, totalSeconds);

    if (progress.expired) {
        // Time's up - handle expiration
        if (onExpired) {
            onExpired();
        } else {
            // Default behavior: close notification
            qCDebug(NotificationOrchestratorLog) << "Notification expired, closing";
            m_notificationManager->closeNotification(notificationId);
            notificationId = 0;
            updateTimer->stop();
        }
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Updating notification - remaining:" << progress.remainingSeconds
             << "progress:" << progress.progressPercent << "%";

    // Format body using provided formatter
    QString body = bodyFormatter(progress.remainingSeconds);

    // Update hints with new progress value
    QVariantMap hints = NotificationUtils::createNotificationHints(1, progress.progressPercent);

    notificationId = m_notificationManager->updateNotification(
        notificationId,
        title,
        body,
        hints,
        0 // no timeout
    );
}

void NotificationOrchestrator::updateCodeNotification()
{
    // Use helper with custom body formatter
    updateNotificationWithProgress(
        m_codeNotificationId,
        m_codeUpdateTimer,
        m_codeExpirationTime,
        m_codeTotalSeconds,
        m_currentCredentialName,
        [this](int remainingSeconds) {
            return i18n("%1 (copied) • expires in %2s", m_currentCode, remainingSeconds);
        }
    );
}

void NotificationOrchestrator::updateTouchNotification()
{
    int totalSeconds = m_config->touchTimeout();

    // Use helper with custom body formatter and expiration handler
    updateNotificationWithProgress(
        m_touchNotificationId,
        m_touchUpdateTimer,
        m_touchExpirationTime,
        totalSeconds,
        i18n("Please touch your YubiKey"),
        [](int remainingSeconds) {
            return i18n("Timeout in %1s", remainingSeconds);
        },
        [this]() {
            // Custom expiration behavior: show timeout message
            qCDebug(NotificationOrchestratorLog) << "Touch timeout, showing timeout message";
            m_touchUpdateTimer->stop();

            QString body = i18n("Operation cancelled");
            QVariantMap hints = NotificationUtils::createNotificationHints(1, 0); // 0% - timeout reached

            m_notificationManager->updateNotification(
                m_touchNotificationId,
                i18n("Touch Timeout"),
                body,
                hints,
                5000 // Auto-close after 5 seconds
            );

            m_touchNotificationId = 0;
        }
    );
}

void NotificationOrchestrator::updateModifierNotification()
{
    constexpr int MODIFIER_TIMEOUT_SECONDS = 15;

    // Use helper with custom body formatter and expiration handler
    updateNotificationWithProgress(
        m_modifierNotificationId,
        m_modifierUpdateTimer,
        m_modifierExpirationTime,
        MODIFIER_TIMEOUT_SECONDS,
        i18n("Please release modifier keys"),
        [this](int remainingSeconds) {
            QString modifierList = m_currentModifiers.join(QStringLiteral(", "));
            QString body = i18n("Pressed keys: %1\n", modifierList);
            body += i18n("Timeout in %1s", remainingSeconds);
            return body;
        },
        [this]() {
            // Custom expiration behavior: close and prepare for cancel notification
            qCDebug(NotificationOrchestratorLog) << "Modifier timeout expired";
            m_modifierUpdateTimer->stop();

            // Close the notification
            if (m_modifierNotificationId != 0 && m_notificationManager) {
                m_notificationManager->closeNotification(m_modifierNotificationId);
                m_modifierNotificationId = 0;
            }
        }
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
    } else if (id == m_modifierNotificationId) {
        qCDebug(NotificationOrchestratorLog) << "Modifier notification closed";
        m_modifierNotificationId = 0;
        m_modifierUpdateTimer->stop();
    }
}

} // namespace YubiKey
} // namespace KRunner
