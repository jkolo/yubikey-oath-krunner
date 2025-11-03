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

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

NotificationOrchestrator::NotificationOrchestrator(DBusNotificationManager *notificationManager,
                                                   const ConfigurationProvider *config,
                                                   QObject *parent)
    : QObject(parent)
    , m_notificationManager(notificationManager)
    , m_config(config)
    , m_codeUpdateTimer(new QTimer(this))
    , m_touchUpdateTimer(new QTimer(this))
    , m_modifierUpdateTimer(new QTimer(this))
    , m_reconnectUpdateTimer(new QTimer(this))
{
    connect(m_codeUpdateTimer, &QTimer::timeout, this, &NotificationOrchestrator::updateCodeNotification);
    connect(m_touchUpdateTimer, &QTimer::timeout, this, &NotificationOrchestrator::updateTouchNotification);
    connect(m_modifierUpdateTimer, &QTimer::timeout, this, &NotificationOrchestrator::updateModifierNotification);
    connect(m_reconnectUpdateTimer, &QTimer::timeout, this, &NotificationOrchestrator::updateReconnectNotification);

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
    QString const body = i18n("%1 (copied) • expires in %2s", code, expirationSeconds);

    // Prepare hints with progress bar (100% at start)
    QVariantMap const hints = NotificationUtils::createNotificationHints(1, 100);

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
    QString const body = i18n("Timeout in %1s", timeoutSeconds);

    // Prepare hints with progress bar (100% at start)
    QVariantMap const hints = NotificationUtils::createNotificationHints(1, 100);

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
    if (!m_config->showNotifications() || !m_notificationManager || !m_notificationManager->isAvailable()) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing simple notification:" << title << "-" << message << "type:" << type;

    // Use DBusNotificationManager directly (like other notifications)
    // type: 0 = info, 1 = warning/error
    const uint urgency = (type == 1) ? 2 : 1; // 1 = normal, 2 = critical
    QVariantMap hints;
    hints[QStringLiteral("urgency")] = urgency;

    // Show notification with 5 second timeout (auto-close)
    m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        0, // replaces_id - don't replace anything
        QStringLiteral(":/icons/yubikey.svg"),
        title,
        message,
        QStringList(), // No actions
        hints,
        5000 // 5 second timeout
    );

    qCDebug(NotificationOrchestratorLog) << "Simple notification shown";
}

uint NotificationOrchestrator::showPersistentNotification(const QString &title, const QString &message, int type)
{
    if (!m_config->showNotifications() || !m_notificationManager || !m_notificationManager->isAvailable()) {
        return 0;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing persistent notification:" << title << "-" << message << "type:" << type;

    // type: 0 = info, 1 = warning/error
    const uint urgency = (type == 1) ? 2 : 1; // 1 = normal, 2 = critical
    QVariantMap hints;
    hints[QStringLiteral("urgency")] = urgency;

    // Show notification with NO timeout - must be closed manually
    const uint notificationId = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        0, // replaces_id - don't replace anything
        QStringLiteral(":/icons/yubikey.svg"),
        title,
        message,
        QStringList(), // No actions
        hints,
        0 // NO timeout - stays until closed
    );

    qCDebug(NotificationOrchestratorLog) << "Persistent notification shown with ID:" << notificationId;
    return notificationId;
}

void NotificationOrchestrator::closeNotification(uint notificationId)
{
    if (!m_notificationManager || !m_notificationManager->isAvailable() || notificationId == 0) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Closing notification ID:" << notificationId;
    m_notificationManager->closeNotification(notificationId);
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
    QString const modifierList = modifiers.join(QStringLiteral(", "));
    QString body = i18n("Pressed keys: %1\n", modifierList);
    body += i18n("Timeout in %1s", timeoutSeconds);

    // Prepare hints with progress bar (100% at start)
    QVariantMap const hints = NotificationUtils::createNotificationHints(1, 100);

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

    auto *notification = new KNotification(QStringLiteral("yubikey-oath"),
                                         KNotification::CloseOnTimeout,
                                         nullptr);
    notification->setComponentName(QStringLiteral("yubikey_oath"));
    notification->setTitle(i18n("Code Input Cancelled"));
    notification->setText(i18n("Modifier keys were held down for too long"));
    notification->setIconName(QStringLiteral(":/icons/yubikey.svg"));

    notification->sendEvent();
}

void NotificationOrchestrator::showReconnectNotification(const QString &deviceName,
                                                         const QString &credentialName,
                                                         int timeoutSeconds)
{
    if (!m_config->showNotifications() || !m_notificationManager || !m_notificationManager->isAvailable()) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing reconnect notification for device:" << deviceName
             << "credential:" << credentialName
             << "timeout:" << timeoutSeconds << "seconds";

    // Close any existing reconnect notification
    if (m_reconnectNotificationId != 0) {
        m_notificationManager->closeNotification(m_reconnectNotificationId);
        m_reconnectNotificationId = 0;
    }

    // Store state for updates
    m_reconnectExpirationTime = QDateTime::currentDateTime().addSecs(timeoutSeconds);
    m_reconnectDeviceName = deviceName;
    m_reconnectCredentialName = credentialName;

    // Format message
    QString const body = i18n("Timeout in %1s", timeoutSeconds);

    // Prepare hints with progress bar (100% at start)
    QVariantMap const hints = NotificationUtils::createNotificationHints(1, 100);

    // Add Cancel action
    QStringList actions;
    actions << QStringLiteral("cancel_reconnect") << i18n("Cancel");

    // Show notification without timeout - we'll update it manually
    m_reconnectNotificationId = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        m_reconnectNotificationId, // replaces_id
        QStringLiteral(":/icons/yubikey.svg"),
        i18n("Connect YubiKey '%1'", deviceName),
        body,
        actions,
        hints,
        0 // no timeout - we manage closing manually
    );

    qCDebug(NotificationOrchestratorLog) << "Reconnect notification shown with ID:" << m_reconnectNotificationId;

    // Start timer to update notification every second with progress bar
    m_reconnectUpdateTimer->start(1000);
}

void NotificationOrchestrator::closeReconnectNotification()
{
    if (m_reconnectNotificationId != 0 && m_notificationManager) {
        qCDebug(NotificationOrchestratorLog) << "Closing reconnect notification ID:" << m_reconnectNotificationId;
        m_notificationManager->closeNotification(m_reconnectNotificationId);
        m_reconnectNotificationId = 0;
    }

    // Stop update timer
    m_reconnectUpdateTimer->stop();
}

void NotificationOrchestrator::updateNotificationWithProgress(
    uint& notificationId,
    QTimer* updateTimer,
    const QDateTime& expirationTime,
    int totalSeconds,
    const QString& title,
    const std::function<QString(int)>& bodyFormatter,
    const std::function<void()>& onExpired)
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
    QString const body = bodyFormatter(progress.remainingSeconds);

    // Update hints with new progress value
    QVariantMap const hints = NotificationUtils::createNotificationHints(1, progress.progressPercent);

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
    int const totalSeconds = m_config->touchTimeout();

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

            QString const body = i18n("Operation cancelled");
            QVariantMap const hints = NotificationUtils::createNotificationHints(1, 0); // 0% - timeout reached

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
            QString const modifierList = m_currentModifiers.join(QStringLiteral(", "));
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

void NotificationOrchestrator::updateReconnectNotification()
{
    int const totalSeconds = m_config->deviceReconnectTimeout();

    // Use helper with custom body formatter
    updateNotificationWithProgress(
        m_reconnectNotificationId,
        m_reconnectUpdateTimer,
        m_reconnectExpirationTime,
        totalSeconds,
        i18n("Connect YubiKey '%1'", m_reconnectDeviceName),
        [](int remainingSeconds) {
            return i18n("Timeout in %1s", remainingSeconds);
        },
        [this]() {
            // Custom expiration behavior: notification already handled by ReconnectWorkflowCoordinator
            qCDebug(NotificationOrchestratorLog) << "Reconnect timeout reached";
            m_reconnectUpdateTimer->stop();
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
    } else if (id == m_reconnectNotificationId && actionKey == QStringLiteral("cancel_reconnect")) {
        qCDebug(NotificationOrchestratorLog) << "User cancelled reconnect operation via notification";
        closeReconnectNotification();
        Q_EMIT reconnectCancelled();
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

} // namespace Daemon
} // namespace YubiKeyOath
