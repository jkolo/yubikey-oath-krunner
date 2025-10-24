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
{
    m_touchTimer->setSingleShot(true);
    connect(m_touchTimer, &QTimer::timeout, this, &TouchHandler::onTimeout);

    qCDebug(YubiKeyDaemonLog) << "TouchHandler: Initialized";
}

void TouchHandler::startTouchOperation(const QString &credentialName, int timeoutSeconds)
{
    qCDebug(YubiKeyDaemonLog) << "TouchHandler: Starting touch operation for:" << credentialName
             << "timeout:" << timeoutSeconds;

    m_waitingForTouch = credentialName;

    if (timeoutSeconds > 0) {
        m_touchTimer->start(timeoutSeconds * 1000);
    }
}

void TouchHandler::cancelTouchOperation()
{
    qCDebug(YubiKeyDaemonLog) << "TouchHandler: Cancelling touch operation for:" << m_waitingForTouch;

    m_touchTimer->stop();
    m_waitingForTouch.clear();
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
    qCDebug(YubiKeyDaemonLog) << "TouchHandler: Touch timeout for:" << m_waitingForTouch;

    QString credentialName = m_waitingForTouch;
    cancelTouchOperation();
    Q_EMIT touchTimedOut(credentialName);
}

} // namespace YubiKey
} // namespace KRunner
