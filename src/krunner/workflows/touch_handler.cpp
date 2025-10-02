/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "touch_handler.h"
#include "../logging_categories.h"
#include <QDebug>

namespace KRunner {
namespace YubiKey {

TouchHandler::TouchHandler(QObject *parent)
    : QObject(parent)
    , m_touchTimer(new QTimer(this))
    , m_countdownTimer(new QTimer(this))
    , m_touchTimeoutRemaining(0)
{
    m_touchTimer->setSingleShot(true);
    connect(m_touchTimer, &QTimer::timeout, this, &TouchHandler::onTimeout);
    connect(m_countdownTimer, &QTimer::timeout, this, &TouchHandler::onCountdownUpdate);

    qCDebug(YubiKeyRunnerLog) << "TouchHandler: Initialized";
}

void TouchHandler::startTouchOperation(const QString &credentialName, int timeoutSeconds)
{
    qCDebug(YubiKeyRunnerLog) << "TouchHandler: Starting touch operation for:" << credentialName
             << "timeout:" << timeoutSeconds;

    m_waitingForTouch = credentialName;
    m_touchTimeoutRemaining = timeoutSeconds;

    if (timeoutSeconds > 0) {
        // Emit initial countdown value immediately
        Q_EMIT touchCountdownUpdate(m_touchTimeoutRemaining);

        m_touchTimer->start(timeoutSeconds * 1000);
        m_countdownTimer->start(1000); // Update every second
    }
}

void TouchHandler::cancelTouchOperation()
{
    qCDebug(YubiKeyRunnerLog) << "TouchHandler: Cancelling touch operation for:" << m_waitingForTouch;

    m_touchTimer->stop();
    m_countdownTimer->stop();
    m_waitingForTouch.clear();
    m_touchTimeoutRemaining = 0;
}

bool TouchHandler::isTouchActive() const
{
    return !m_waitingForTouch.isEmpty();
}

QString TouchHandler::waitingCredential() const
{
    return m_waitingForTouch;
}

void TouchHandler::onTimeout()
{
    qCDebug(YubiKeyRunnerLog) << "TouchHandler: Touch timeout for:" << m_waitingForTouch;

    QString credentialName = m_waitingForTouch;
    cancelTouchOperation();
    Q_EMIT touchTimedOut(credentialName);
}

void TouchHandler::onCountdownUpdate()
{
    m_touchTimeoutRemaining--;

    if (m_touchTimeoutRemaining < 0) {
        m_countdownTimer->stop();
        return;
    }

    Q_EMIT touchCountdownUpdate(m_touchTimeoutRemaining);
}

} // namespace YubiKey
} // namespace KRunner
