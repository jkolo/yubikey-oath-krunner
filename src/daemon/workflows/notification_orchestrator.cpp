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
#include "../../shared/utils/yubikey_icon_resolver.h"

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
{
    // Initialize timers (owned by this QObject)
    m_code.timer = new QTimer(this);
    m_code.urgency = NotificationUrgency::Critical;
    m_touch.timer = new QTimer(this);
    m_touch.urgency = NotificationUrgency::Critical;
    m_modifier.timer = new QTimer(this);
    m_modifier.urgency = NotificationUrgency::Normal;
    m_reconnect.timer = new QTimer(this);
    m_reconnect.urgency = NotificationUrgency::Critical;

    connect(m_code.timer, &QTimer::timeout, this, &NotificationOrchestrator::updateCodeNotification);
    connect(m_touch.timer, &QTimer::timeout, this, &NotificationOrchestrator::updateTouchNotification);
    connect(m_modifier.timer, &QTimer::timeout, this, &NotificationOrchestrator::updateModifierNotification);
    connect(m_reconnect.timer, &QTimer::timeout, this, &NotificationOrchestrator::updateReconnectNotification);

    connect(m_notificationManager, &DBusNotificationManager::actionInvoked,
            this, &NotificationOrchestrator::onNotificationActionInvoked);
    connect(m_notificationManager, &DBusNotificationManager::notificationClosed,
            this, &NotificationOrchestrator::onNotificationClosed);
}

NotificationOrchestrator::~NotificationOrchestrator() = default;

void NotificationOrchestrator::closeTimedNotification(TimedNotificationState &state)
{
    if (state.id != 0 && m_notificationManager) {
        m_notificationManager->closeNotification(state.id);
        state.id = 0;
    }
    state.timer->stop();
}

void NotificationOrchestrator::showCodeNotification(const QString &code,
                                                    const QString &credentialName,
                                                    int expirationSeconds,
                                                    const DeviceModel& deviceModel)
{
    if (!m_config->showNotifications() || !m_notificationManager || !m_notificationManager->isAvailable()) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing code notification for:" << credentialName
             << "expiration:" << expirationSeconds << "seconds"
             << "brand:" << brandName(deviceModel.brand)
             << "model:" << deviceModel.modelString;

    // Get model-specific icon theme name
    const QString iconName = YubiKeyIconResolver::getIconName(deviceModel);

    // Store state for updates
    m_code.expirationTime = QDateTime::currentDateTime().addSecs(expirationSeconds);
    m_code.iconName = iconName;
    m_codeTotalSeconds = expirationSeconds;
    m_currentCredentialName = credentialName;
    m_currentCode = code;
    m_codeDeviceModel = deviceModel;

    // Format notification body: "CODE (copied) • expires in XXs"
    QString const body = i18n("%1 (copied) • expires in %2s", code, expirationSeconds);

    // Prepare hints: critical urgency (bypasses DND), progress bar, device icon
    QVariantMap const hints = NotificationUtils::createNotificationHints(
        NotificationUrgency::Critical,
        100,
        iconName
    );

    // Show notification without timeout - we'll close it manually
    m_code.id = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        m_code.id, // replaces_id
        iconName, // Device-specific icon (also in image-path hint for compatibility)
        credentialName,
        body,
        QStringList(), // No actions
        hints,
        0 // no timeout - we manage closing manually
    );

    qCDebug(NotificationOrchestratorLog) << "Code notification shown with ID:" << m_code.id
                                         << "device icon:" << iconName;

    // Start timer to update notification every second with progress bar
    m_code.timer->start(1000);
}

void NotificationOrchestrator::showTouchNotification(const QString &credentialName,
                                                     int timeoutSeconds,
                                                     const DeviceModel& deviceModel)
{
    if (!m_config->showNotifications() || !m_notificationManager || !m_notificationManager->isAvailable()) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing touch notification for:" << credentialName
             << "timeout:" << timeoutSeconds << "seconds"
             << "brand:" << brandName(deviceModel.brand)
             << "model:" << deviceModel.modelString;

    // Close any existing touch notification
    closeTimedNotification(m_touch);

    // Get model-specific icon theme name
    const QString iconName = YubiKeyIconResolver::getIconName(deviceModel);

    // Store state for updates
    m_touch.expirationTime = QDateTime::currentDateTime().addSecs(timeoutSeconds);
    m_touch.iconName = iconName;
    m_touchCredentialName = credentialName;
    m_touchDeviceModel = deviceModel;

    // Format message - simple and concise
    QString const body = i18n("Timeout in %1s", timeoutSeconds);

    // Prepare hints: critical urgency (bypasses DND), progress bar, device icon
    QVariantMap const hints = NotificationUtils::createNotificationHints(
        NotificationUrgency::Critical,
        100,
        iconName
    );

    // Add Cancel action
    QStringList actions;
    actions << QStringLiteral("cancel") << i18n("Cancel");

    // Show notification without timeout - we'll update it manually
    m_touch.id = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        m_touch.id, // replaces_id
        iconName, // Device-specific icon (also in image-path hint for compatibility)
        i18n("Please touch your YubiKey"),
        body,
        actions,
        hints,
        0 // no timeout - we manage closing manually
    );

    qCDebug(NotificationOrchestratorLog) << "Touch notification shown with ID:" << m_touch.id
                                         << "device icon:" << iconName;

    // Start timer to update notification every second with progress bar
    m_touch.timer->start(1000);
}

void NotificationOrchestrator::closeTouchNotification()
{
    closeTimedNotification(m_touch);

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

    // type: 0 = info, 1 = warning/error
    const uchar urgency = (type == 1) ? NotificationUrgency::Critical : NotificationUrgency::Normal;

    // Prepare hints with proper urgency type
    const QVariantMap hints = NotificationUtils::createNotificationHints(urgency);

    // Show notification with 5 second timeout (auto-close)
    m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        0, // replaces_id - don't replace anything
        YubiKeyIconResolver::getGenericIconName(),
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
    const uchar urgency = (type == 1) ? NotificationUrgency::Critical : NotificationUrgency::Normal;

    // Prepare hints with proper urgency type
    const QVariantMap hints = NotificationUtils::createNotificationHints(urgency);

    // Show notification with NO timeout - must be closed manually
    const uint notificationId = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        0, // replaces_id - don't replace anything
        YubiKeyIconResolver::getGenericIconName(),
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
    closeTimedNotification(m_modifier);

    // Store state for updates
    m_modifier.expirationTime = QDateTime::currentDateTime().addSecs(timeoutSeconds);
    m_currentModifiers = modifiers;

    // Format message
    QString const modifierList = modifiers.join(QStringLiteral(", "));
    QString body = i18n("Pressed keys: %1\n", modifierList);
    body += i18n("Timeout in %1s", timeoutSeconds);

    // Prepare hints: normal urgency (informational), progress bar
    QVariantMap const hints = NotificationUtils::createNotificationHints(NotificationUrgency::Normal, 100);

    // Show notification without timeout - we'll update it manually
    m_modifier.id = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        m_modifier.id, // replaces_id
        YubiKeyIconResolver::getGenericIconName(),
        i18n("Please release modifier keys"),
        body,
        QStringList(), // No actions
        hints,
        0 // no timeout - we manage closing manually
    );

    qCDebug(NotificationOrchestratorLog) << "Modifier notification shown with ID:" << m_modifier.id;

    // Start timer to update notification every second with progress bar
    m_modifier.timer->start(1000);
}

void NotificationOrchestrator::closeModifierNotification()
{
    closeTimedNotification(m_modifier);
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
    notification->setComponentName(QStringLiteral("krunner_yubikey"));
    notification->setTitle(i18n("Code Input Cancelled"));
    notification->setText(i18n("Modifier keys were held down for too long"));
    notification->setIconName(YubiKeyIconResolver::getGenericIconName());

    notification->sendEvent();
}

void NotificationOrchestrator::showReconnectNotification(const QString &deviceName,
                                                         const QString &credentialName,
                                                         int timeoutSeconds,
                                                         const DeviceModel& deviceModel)
{
    if (!m_config->showNotifications() || !m_notificationManager || !m_notificationManager->isAvailable()) {
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Showing reconnect notification for device:" << deviceName
             << "credential:" << credentialName
             << "timeout:" << timeoutSeconds << "seconds"
             << "brand:" << brandName(deviceModel.brand)
             << "model:" << deviceModel.modelString;

    // Close any existing reconnect notification
    closeTimedNotification(m_reconnect);

    // Get model-specific icon theme name (may be generic if device offline)
    const QString iconName = YubiKeyIconResolver::getIconName(deviceModel);

    // Store state for updates
    m_reconnect.expirationTime = QDateTime::currentDateTime().addSecs(timeoutSeconds);
    m_reconnect.iconName = iconName;
    m_reconnectDeviceName = deviceName;
    m_reconnectCredentialName = credentialName;
    m_reconnectDeviceModel = deviceModel;

    // Format message
    QString const body = i18n("Timeout in %1s", timeoutSeconds);

    // Prepare hints: critical urgency (bypasses DND), progress bar, device icon
    QVariantMap const hints = NotificationUtils::createNotificationHints(
        NotificationUrgency::Critical,
        100,
        iconName
    );

    // Add Cancel action
    QStringList actions;
    actions << QStringLiteral("cancel_reconnect") << i18n("Cancel");

    // Show notification without timeout - we'll update it manually
    m_reconnect.id = m_notificationManager->showNotification(
        QStringLiteral("YubiKey OATH"),
        m_reconnect.id, // replaces_id
        iconName, // Device-specific icon (also in image-path hint for compatibility)
        i18n("Connect YubiKey '%1'", deviceName),
        body,
        actions,
        hints,
        0 // no timeout - we manage closing manually
    );

    qCDebug(NotificationOrchestratorLog) << "Reconnect notification shown with ID:" << m_reconnect.id
                                         << "device icon:" << iconName;

    // Start timer to update notification every second with progress bar
    m_reconnect.timer->start(1000);
}

void NotificationOrchestrator::closeReconnectNotification()
{
    closeTimedNotification(m_reconnect);
}

void NotificationOrchestrator::updateNotificationWithProgress(
    TimedNotificationState& state,
    int totalSeconds,
    const QString& title,
    const std::function<QString(int)>& bodyFormatter,
    const std::function<void()>& onExpired)
{
    if (state.id == 0 || !m_notificationManager) {
        state.timer->stop();
        return;
    }

    // Calculate timer progress using helper
    auto progress = NotificationHelper::calculateTimerProgress(state.expirationTime, totalSeconds);

    if (progress.expired) {
        // Time's up - handle expiration
        if (onExpired) {
            onExpired();
        } else {
            // Default behavior: close notification
            qCDebug(NotificationOrchestratorLog) << "Notification expired, closing";
            m_notificationManager->closeNotification(state.id);
            state.id = 0;
            state.timer->stop();
        }
        return;
    }

    qCDebug(NotificationOrchestratorLog) << "Updating notification - remaining:" << progress.remainingSeconds
             << "progress:" << progress.progressPercent << "%"
             << "urgency:" << state.urgency;

    // Format body using provided formatter
    QString const body = bodyFormatter(progress.remainingSeconds);

    // Update hints with progress, urgency, and device icon (from state)
    QVariantMap const hints = NotificationUtils::createNotificationHints(state.urgency, progress.progressPercent, state.iconName);

    state.id = m_notificationManager->updateNotification(
        state.id,
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
        m_code,
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
        m_touch,
        totalSeconds,
        i18n("Please touch your YubiKey"),
        [](int remainingSeconds) {
            return i18n("Timeout in %1s", remainingSeconds);
        },
        [this]() {
            // Custom expiration behavior: show timeout message
            qCDebug(NotificationOrchestratorLog) << "Touch timeout, showing timeout message";
            m_touch.timer->stop();

            QString const body = i18n("Operation cancelled");
            QVariantMap const hints = NotificationUtils::createNotificationHints(1, 0); // 0% - timeout reached

            m_notificationManager->updateNotification(
                m_touch.id,
                i18n("Touch Timeout"),
                body,
                hints,
                5000 // Auto-close after 5 seconds
            );

            m_touch.id = 0;
        }
    );
}

void NotificationOrchestrator::updateModifierNotification()
{
    constexpr int MODIFIER_TIMEOUT_SECONDS = 15;

    // Use helper with custom body formatter and expiration handler
    updateNotificationWithProgress(
        m_modifier,
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
            closeTimedNotification(m_modifier);
        }
    );
}

void NotificationOrchestrator::updateReconnectNotification()
{
    int const totalSeconds = m_config->deviceReconnectTimeout();

    // Use helper with custom body formatter
    updateNotificationWithProgress(
        m_reconnect,
        totalSeconds,
        i18n("Connect YubiKey '%1'", m_reconnectDeviceName),
        [](int remainingSeconds) {
            return i18n("Timeout in %1s", remainingSeconds);
        },
        [this]() {
            // Custom expiration behavior: notification already handled by ReconnectWorkflowCoordinator
            qCDebug(NotificationOrchestratorLog) << "Reconnect timeout reached";
            m_reconnect.timer->stop();
        }
    );
}

void NotificationOrchestrator::onNotificationActionInvoked(uint id, const QString &actionKey)
{
    qCDebug(NotificationOrchestratorLog) << "Notification action invoked - ID:" << id << "action:" << actionKey;

    if (id == m_touch.id && actionKey == QStringLiteral("cancel")) {
        qCDebug(NotificationOrchestratorLog) << "User cancelled touch operation via notification";
        closeTouchNotification();
        Q_EMIT touchCancelled();
    } else if (id == m_reconnect.id && actionKey == QStringLiteral("cancel_reconnect")) {
        qCDebug(NotificationOrchestratorLog) << "User cancelled reconnect operation via notification";
        closeReconnectNotification();
        Q_EMIT reconnectCancelled();
    }
}

void NotificationOrchestrator::onNotificationClosed(uint id, uint reason)
{
    qCDebug(NotificationOrchestratorLog) << "Notification closed - ID:" << id << "reason:" << reason;

    if (id == m_code.id) {
        qCDebug(NotificationOrchestratorLog) << "Code notification closed";
        m_code.id = 0;
        m_code.timer->stop();
    } else if (id == m_touch.id) {
        qCDebug(NotificationOrchestratorLog) << "Touch notification closed";
        m_touch.id = 0;
        m_touch.timer->stop();
    } else if (id == m_modifier.id) {
        qCDebug(NotificationOrchestratorLog) << "Modifier notification closed";
        m_modifier.id = 0;
        m_modifier.timer->stop();
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
