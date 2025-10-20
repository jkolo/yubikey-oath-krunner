/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QPointer>
#include <functional>

// Forward declarations for Qt/KDE classes (must be outside namespace)
class QTimer;
class KNotification;

namespace KRunner {
namespace YubiKey {

class DBusNotificationManager;
class ConfigurationProvider;

/**
 * @brief Orchestrates all notification display and updates
 *
 * Single Responsibility: Manage all types of notifications (code, touch, typing, errors)
 * Dependency Inversion: Depends on ConfigurationProvider interface
 *
 * @par Notification Types
 * - Code notifications: Show copied code with countdown timer and progress bar
 * - Touch notifications: Request YubiKey touch with manual countdown (bypasses 10s limit)
 * - Simple notifications: Info/warning messages without timers
 *
 * @par Design Pattern
 * Uses DBusNotificationManager for D-Bus communication, avoiding KNotification
 * server limitations. Implements manual countdown with QTimer for precise control.
 *
 * @par Thread Safety
 * All public methods must be called from the main/UI thread (QObject-based).
 *
 * @par Usage Example
 * @code
 * NotificationOrchestrator *notif = new NotificationOrchestrator(dbusManager, config);
 *
 * // Show code notification with 30-second countdown
 * notif->showCodeNotification("123456", "Google:user@example.com", 30);
 *
 * // Show touch request with 15-second timeout
 * connect(notif, &NotificationOrchestrator::touchCancelled,
 *         this, &MyClass::onUserCancelledTouch);
 * notif->showTouchNotification("Google:user@example.com", 15);
 *
 * // Close touch notification when done
 * notif->closeTouchNotification();
 *
 * // Show simple error message
 * notif->showSimpleNotification("Error", "Failed to connect", 1);
 * @endcode
 */
class NotificationOrchestrator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs notification orchestrator
     *
     * @param notificationManager D-Bus notification manager for direct communication
     * @param config Configuration provider for notification settings
     * @param parent Parent QObject for automatic cleanup
     */
    explicit NotificationOrchestrator(DBusNotificationManager *notificationManager,
                                      const ConfigurationProvider *config,
                                      QObject *parent = nullptr);
    ~NotificationOrchestrator() override;

    /**
     * @brief Shows notification about copied TOTP code with expiration countdown
     *
     * Displays notification with:
     * - TOTP code and credential name
     * - Live countdown timer (updates every second)
     * - Progress bar showing time remaining
     * - Automatically closes when timer reaches 0
     *
     * @param code The TOTP code that was copied (typically 6-8 digits)
     * @param credentialName Credential name to display (e.g., "Google:user@example.com")
     * @param expirationSeconds Seconds until code expires (typically 30)
     *
     * @note Only one code notification can be active at a time. Calling this
     *       again replaces the existing notification.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void showCodeNotification(const QString &code,
                             const QString &credentialName,
                             int expirationSeconds);

    /**
     * @brief Shows notification requesting YubiKey touch with timeout countdown
     *
     * Displays persistent notification with:
     * - Request to touch YubiKey
     * - Manual countdown timer (bypasses server 10-second limit)
     * - Cancel button that emits touchCancelled() signal
     * - Updates every second with remaining time
     *
     * @param credentialName Credential requiring touch
     * @param timeoutSeconds Touch timeout in seconds (typically 15)
     *
     * @note Notification persists until closeTouchNotification() is called
     *       or user clicks cancel button.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void showTouchNotification(const QString &credentialName, int timeoutSeconds);

    /**
     * @brief Closes active touch notification immediately
     *
     * Removes touch notification from screen and stops countdown timer.
     * Safe to call even if no touch notification is active.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void closeTouchNotification();

    /**
     * @brief Shows simple one-time notification without timer
     *
     * Displays basic notification with title and message. No countdown,
     * no progress bar, just informational message.
     *
     * @param title Notification title
     * @param message Notification message body
     * @param type Notification urgency: 0=info (default), 1=warning/error
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void showSimpleNotification(const QString &title, const QString &message, int type = 0);

Q_SIGNALS:
    /**
     * @brief Emitted when touch operation is cancelled by user
     */
    void touchCancelled();

private Q_SLOTS:
    void updateCodeNotification();
    void updateTouchNotification();
    void onNotificationActionInvoked(uint id, const QString &actionKey);
    void onNotificationClosed(uint id, uint reason);

private:
    /**
     * @brief Helper for updating countdown notifications
     *
     * Centralized logic for notification updates with progress bars and
     * countdown timers. Reduces code duplication between code and touch notifications.
     *
     * @param notificationId Reference to notification ID (updated on success)
     * @param updateTimer Timer to stop on expiration
     * @param expirationTime When notification should expire
     * @param totalSeconds Total countdown duration
     * @param title Notification title
     * @param bodyFormatter Function to format body text from remaining seconds
     * @param onExpired Callback when timer expires (optional)
     */
    void updateNotificationWithProgress(
        uint& notificationId,
        QTimer* updateTimer,
        const QDateTime& expirationTime,
        int totalSeconds,
        const QString& title,
        std::function<QString(int)> bodyFormatter,
        std::function<void()> onExpired = nullptr
    );

    DBusNotificationManager *m_notificationManager;
    const ConfigurationProvider *m_config;

    // Code notification state
    QTimer *m_codeUpdateTimer;
    uint m_codeNotificationId = 0;
    QDateTime m_codeExpirationTime;
    QString m_currentCredentialName;
    QString m_currentCode;

    // Touch notification state
    QPointer<KNotification> m_touchNotification;
    uint m_touchNotificationId = 0;
    QTimer *m_touchUpdateTimer;
    QDateTime m_touchExpirationTime;
    QString m_touchCredentialName;
};

} // namespace YubiKey
} // namespace KRunner
