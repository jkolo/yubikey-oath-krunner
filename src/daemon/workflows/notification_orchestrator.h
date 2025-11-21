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
#include "../../shared/types/yubikey_model.h"
#include "../../shared/types/device_model.h"

// Forward declarations for Qt/KDE classes (must be outside namespace)
class QTimer;
class KNotification;

namespace YubiKeyOath {
namespace Shared {
class ConfigurationProvider;
}

namespace Daemon {
using Shared::ConfigurationProvider;

class DBusNotificationManager;

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
     * - Device model-specific icon (YubiKey, Nitrokey, etc.)
     * - Automatically closes when timer reaches 0
     *
     * @param code The TOTP code that was copied (typically 6-8 digits)
     * @param credentialName Credential name to display (e.g., "Google:user@example.com")
     * @param expirationSeconds Seconds until code expires (typically 30)
     * @param deviceModel Device model for brand-specific icon
     *
     * @note Only one code notification can be active at a time. Calling this
     *       again replaces the existing notification.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void showCodeNotification(const QString &code,
                             const QString &credentialName,
                             int expirationSeconds,
                             const Shared::DeviceModel& deviceModel);

    /**
     * @brief Shows notification requesting device touch with timeout countdown
     *
     * Displays persistent notification with:
     * - Request to touch device (YubiKey/Nitrokey)
     * - Manual countdown timer (bypasses server 10-second limit)
     * - Device model-specific icon
     * - Cancel button that emits touchCancelled() signal
     * - Updates every second with remaining time
     *
     * @param credentialName Credential requiring touch
     * @param timeoutSeconds Touch timeout in seconds (typically 15)
     * @param deviceModel Device model for brand-specific icon
     *
     * @note Notification persists until closeTouchNotification() is called
     *       or user clicks cancel button.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void showTouchNotification(const QString &credentialName,
                              int timeoutSeconds,
                              const Shared::DeviceModel& deviceModel);

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

    /**
     * @brief Shows persistent notification that stays until closed
     *
     * Displays notification with no timeout - must be closed manually via closeNotification().
     * Useful for long-running operations (like reconnect).
     *
     * @param title Notification title
     * @param message Notification message body
     * @param type Notification urgency: 0=info (default), 1=warning/error
     * @return Notification ID (use with closeNotification to close)
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    uint showPersistentNotification(const QString &title, const QString &message, int type = 0);

    /**
     * @brief Closes notification by ID
     *
     * @param notificationId ID returned by showPersistentNotification()
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void closeNotification(uint notificationId);

    /**
     * @brief Shows notification requesting modifier key release with timeout countdown
     *
     * Displays persistent notification with:
     * - Request to release pressed modifier keys
     * - List of currently pressed modifiers
     * - Manual countdown timer (15 seconds)
     * - Updates every second with remaining time
     *
     * @param modifiers List of pressed modifier names (e.g., ["Shift", "Ctrl"])
     * @param timeoutSeconds Timeout in seconds (typically 15)
     *
     * @note Notification persists until closeModifierNotification() is called
     *       or timeout expires.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void showModifierReleaseNotification(const QStringList& modifiers, int timeoutSeconds);

    /**
     * @brief Closes active modifier release notification immediately
     *
     * Removes modifier notification from screen and stops countdown timer.
     * Safe to call even if no modifier notification is active.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void closeModifierNotification();

    /**
     * @brief Shows notification about cancelled type action due to modifier timeout
     *
     * Displays warning notification informing user that code input was cancelled
     * because modifier keys were held down for too long.
     *
     * @par Thread Safety
     * Must be called from main/UI thread.
     */
    void showModifierCancelNotification();

    /**
     * @brief Shows reconnect notification with countdown
     * @param deviceName Device name to display
     * @param credentialName Credential name to display
     * @param timeoutSeconds Timeout in seconds
     * @param deviceModel Device model for brand-specific icon
     *
     * Shows notification with message "Connect device {deviceName} to generate code for {credentialName}"
     * with Cancel button, countdown timer, and device model-specific icon.
     */
    void showReconnectNotification(const QString &deviceName,
                                   const QString &credentialName,
                                   int timeoutSeconds,
                                   const Shared::DeviceModel& deviceModel);

    /**
     * @brief Closes reconnect notification if active
     */
    void closeReconnectNotification();

Q_SIGNALS:
    /**
     * @brief Emitted when touch operation is cancelled by user
     */
    void touchCancelled();

    /**
     * @brief Emitted when reconnect operation is cancelled by user
     */
    void reconnectCancelled();

private Q_SLOTS:
    void updateCodeNotification();
    void updateTouchNotification();
    void updateModifierNotification();
    void updateReconnectNotification();
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
     * @param urgency Notification urgency level (0=Low, 1=Normal, 2=Critical)
     * @param iconName Optional device-specific icon theme name
     */
    void updateNotificationWithProgress(
        uint& notificationId,
        QTimer* updateTimer,
        const QDateTime& expirationTime,
        int totalSeconds,
        const QString& title,
        const std::function<QString(int)>& bodyFormatter,
        const std::function<void()>& onExpired = nullptr,
        uchar urgency = 1,
        const QString& iconName = QString()
    );

    /**
     * @brief Checks if notifications should be shown based on configuration and availability
     *
     * Helper method to reduce code duplication. Checks:
     * - Configuration setting (showNotifications())
     * - Notification manager availability
     * - D-Bus notification service availability
     *
     * @return true if notifications should be shown, false otherwise
     */
    bool shouldShowNotifications() const;

    DBusNotificationManager *m_notificationManager;
    const ConfigurationProvider *m_config;

    // Urgency levels for each notification type (for preserving during updates)
    uchar m_codeNotificationUrgency = 2;      // Critical
    uchar m_touchNotificationUrgency = 2;     // Critical
    uchar m_modifierNotificationUrgency = 1;  // Normal
    uchar m_reconnectNotificationUrgency = 2; // Critical

    // Code notification state
    QTimer *m_codeUpdateTimer;
    uint m_codeNotificationId = 0;
    QDateTime m_codeExpirationTime;
    int m_codeTotalSeconds = 0;
    QString m_currentCredentialName;
    QString m_currentCode;
    Shared::DeviceModel m_codeDeviceModel;
    QString m_codeIconName;

    // Touch notification state
    QPointer<KNotification> m_touchNotification;
    uint m_touchNotificationId = 0;
    QTimer *m_touchUpdateTimer;
    QDateTime m_touchExpirationTime;
    QString m_touchCredentialName;
    Shared::DeviceModel m_touchDeviceModel;
    QString m_touchIconName;

    // Modifier release notification state
    uint m_modifierNotificationId = 0;
    QTimer *m_modifierUpdateTimer;
    QDateTime m_modifierExpirationTime;
    QStringList m_currentModifiers;

    // Reconnect notification state
    uint m_reconnectNotificationId = 0;
    QTimer *m_reconnectUpdateTimer;
    QDateTime m_reconnectExpirationTime;
    QString m_reconnectDeviceName;
    QString m_reconnectCredentialName;
    Shared::DeviceModel m_reconnectDeviceModel;
    QString m_reconnectIconName;
};

} // namespace Daemon
} // namespace YubiKeyOath
