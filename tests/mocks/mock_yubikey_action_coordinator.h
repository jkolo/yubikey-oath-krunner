/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "daemon/actions/action_executor.h"
#include <QObject>
#include <QString>
#include <QStringList>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of YubiKeyActionCoordinator for testing
 *
 * Tracks action execution calls without actual YubiKey operations
 */
class MockYubiKeyActionCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit MockYubiKeyActionCoordinator(QObject *parent = nullptr)
        : QObject(parent)
        , m_executeActionResult(ActionExecutor::ActionResult::Success)
    {}

    ~MockYubiKeyActionCoordinator() override = default;

    /**
     * @brief Mock executeActionWithNotification
     */
    ActionExecutor::ActionResult executeActionWithNotification(
        const QString &code,
        const QString &credentialName,
        const QString &actionType)
    {
        m_lastCode = code;
        m_lastCredentialName = credentialName;
        m_lastActionType = actionType;
        m_callHistory.append(QString("executeActionWithNotification(%1, %2, %3)")
            .arg(code).arg(credentialName).arg(actionType));
        return m_executeActionResult;
    }

    // ========== Test Helper Methods ==========

    void setExecuteActionResult(ActionExecutor::ActionResult result)
    {
        m_executeActionResult = result;
    }

    QString lastCode() const { return m_lastCode; }
    QString lastCredentialName() const { return m_lastCredentialName; }
    QString lastActionType() const { return m_lastActionType; }

    QStringList callHistory() const { return m_callHistory; }
    int callCount() const { return m_callHistory.size(); }

    void reset()
    {
        m_callHistory.clear();
        m_lastCode.clear();
        m_lastCredentialName.clear();
        m_lastActionType.clear();
        m_executeActionResult = ActionExecutor::ActionResult::Success;
    }

private:
    ActionExecutor::ActionResult m_executeActionResult;
    QString m_lastCode;
    QString m_lastCredentialName;
    QString m_lastActionType;
    QStringList m_callHistory;
};

} // namespace Daemon
} // namespace YubiKeyOath
