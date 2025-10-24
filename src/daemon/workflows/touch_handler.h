/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace KRunner {
namespace YubiKey {

/**
 * @brief Handles YubiKey touch operations and timeouts
 *
 * Single Responsibility: Manage touch-required credential operations
 */
class TouchHandler : public QObject
{
    Q_OBJECT

public:
    explicit TouchHandler(QObject *parent = nullptr);
    ~TouchHandler() override = default;

    /**
     * @brief Starts touch operation with timeout
     * @param credentialName Name of credential requiring touch
     * @param timeoutSeconds Timeout in seconds (0 = no timeout)
     */
    void startTouchOperation(const QString &credentialName, int timeoutSeconds);

    /**
     * @brief Cancels ongoing touch operation
     */
    void cancelTouchOperation();

    /**
     * @brief Checks if touch operation is active
     * @return true if waiting for touch
     */
    bool isTouchActive() const;

    /**
     * @brief Gets credential name waiting for touch
     * @return Credential name or empty string
     */
    QString waitingCredential() const;

Q_SIGNALS:
    /**
     * @brief Emitted when touch timeout expires
     * @param credentialName Credential that timed out
     */
    void touchTimedOut(const QString &credentialName);

private Q_SLOTS:
    void onTimeout();

private:
    QTimer *m_touchTimer;
    QString m_waitingForTouch;
};

} // namespace YubiKey
} // namespace KRunner
