/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of TouchHandler for testing
 *
 * Allows manual control of touch operations and timeout behavior
 */
class MockTouchHandler : public QObject
{
    Q_OBJECT

public:
    explicit MockTouchHandler(QObject *parent = nullptr)
        : QObject(parent)
        , m_touchActive(false)
        , m_waitingForTouch()
        , m_manualTimeoutControl(false)
    {}

    ~MockTouchHandler() override = default;

    /**
     * @brief Starts touch operation
     * @param credentialName Name of credential requiring touch
     * @param timeoutSeconds Timeout in seconds (0 = no timeout)
     *
     * If manualTimeoutControl is false (default), automatically triggers
     * timeout after the specified delay. If true, timeout must be triggered
     * manually via triggerTimeout().
     */
    void startTouchOperation(const QString &credentialName, int timeoutSeconds)
    {
        m_touchActive = true;
        m_waitingForTouch = credentialName;
        m_lastTimeoutSeconds = timeoutSeconds;

        // Record call for verification
        m_callHistory.append(QString("startTouchOperation(%1, %2)").arg(credentialName).arg(timeoutSeconds));

        // Auto-trigger timeout if not in manual mode and timeout > 0
        if (!m_manualTimeoutControl && timeoutSeconds > 0) {
            QTimer::singleShot(0, this, [this, credentialName]() {
                if (m_touchActive) {
                    triggerTimeout();
                }
            });
        }
    }

    /**
     * @brief Cancels ongoing touch operation
     */
    void cancelTouchOperation()
    {
        m_touchActive = false;
        m_waitingForTouch.clear();

        // Record call for verification
        m_callHistory.append(QStringLiteral("cancelTouchOperation()"));
    }

    /**
     * @brief Checks if touch operation is active
     * @return true if waiting for touch
     */
    bool isTouchActive() const
    {
        return m_touchActive;
    }

    /**
     * @brief Gets credential name waiting for touch
     * @return Credential name or empty string
     */
    QString waitingCredential() const
    {
        return m_waitingForTouch;
    }

    /**
     * @brief Alias for waitingCredential (for compatibility)
     */
    QString waitingForTouch() const
    {
        return m_waitingForTouch;
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Manually triggers touch timeout
     *
     * Emits touchTimedOut signal and resets state
     */
    void triggerTimeout()
    {
        if (m_touchActive) {
            QString const cred = m_waitingForTouch;
            m_touchActive = false;
            m_waitingForTouch.clear();
            Q_EMIT touchTimedOut(cred);
        }
    }

    /**
     * @brief Sets whether timeout should be triggered manually
     * @param manual If true, timeout must be triggered via triggerTimeout()
     *
     * Default: false (auto-trigger on next event loop iteration)
     */
    void setManualTimeoutControl(bool manual)
    {
        m_manualTimeoutControl = manual;
    }

    /**
     * @brief Gets call history for verification
     * @return List of method calls with arguments
     */
    QStringList callHistory() const
    {
        return m_callHistory;
    }

    /**
     * @brief Clears call history
     */
    void clearCallHistory()
    {
        m_callHistory.clear();
    }

    /**
     * @brief Gets last timeout value passed to startTouchOperation
     */
    int lastTimeoutSeconds() const
    {
        return m_lastTimeoutSeconds;
    }

Q_SIGNALS:
    /**
     * @brief Emitted when touch timeout expires
     * @param credentialName Credential that timed out
     */
    void touchTimedOut(const QString &credentialName);

private:
    bool m_touchActive;
    QString m_waitingForTouch;
    bool m_manualTimeoutControl;
    int m_lastTimeoutSeconds = 0;
    QStringList m_callHistory;
};

} // namespace Daemon
} // namespace YubiKeyOath
