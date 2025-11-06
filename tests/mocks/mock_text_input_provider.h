/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "daemon/input/text_input_provider.h"
#include <QObject>
#include <QString>
#include <QStringList>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of TextInputProvider for testing
 *
 * Allows controlling return values and tracking method calls
 */
class MockTextInputProvider : public QObject, public TextInputProvider
{
    Q_OBJECT

public:
    explicit MockTextInputProvider(QObject *parent = nullptr)
        : QObject(parent)
        , m_typeTextResult(true)
        , m_isCompatibleResult(true)
        , m_providerNameResult(QStringLiteral("MockProvider"))
        , m_isWaitingForPermission(false)
        , m_wasPermissionRejected(false)
        , m_typeTextCallCount(0)
        , m_lastTypedText()
    {}

    ~MockTextInputProvider() override;

    // ========== TextInputProvider Interface ==========

    bool typeText(const QString &text) override
    {
        m_lastTypedText = text;
        m_typeTextCallCount++;

        // Record call for verification
        m_callHistory.append(QString("typeText(%1)").arg(text));

        return m_typeTextResult;
    }

    bool isCompatible() const override
    {
        return m_isCompatibleResult;
    }

    QString providerName() const override
    {
        return m_providerNameResult;
    }

    bool isWaitingForPermission() const override
    {
        return m_isWaitingForPermission;
    }

    bool wasPermissionRejected() const override
    {
        return m_wasPermissionRejected;
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Sets return value for typeText()
     * @param result true for success, false for failure
     */
    void setTypeTextResult(bool result)
    {
        m_typeTextResult = result;
    }

    /**
     * @brief Sets return value for isCompatible()
     */
    void setIsCompatibleResult(bool result)
    {
        m_isCompatibleResult = result;
    }

    /**
     * @brief Sets return value for providerName()
     */
    void setProviderName(const QString &name)
    {
        m_providerNameResult = name;
    }

    /**
     * @brief Sets waiting for permission state
     * @param waiting true if waiting for permission dialog
     */
    void setWaitingForPermission(bool waiting)
    {
        m_isWaitingForPermission = waiting;
    }

    /**
     * @brief Sets permission rejected state
     * @param rejected true if user rejected permission
     */
    void setPermissionRejected(bool rejected)
    {
        m_wasPermissionRejected = rejected;
    }

    /**
     * @brief Gets last typed text
     */
    QString lastTypedText() const
    {
        return m_lastTypedText;
    }

    /**
     * @brief Gets number of typeText() calls
     */
    int typeTextCallCount() const
    {
        return m_typeTextCallCount;
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
     * @brief Clears all tracking data
     */
    void reset()
    {
        m_callHistory.clear();
        m_lastTypedText.clear();
        m_typeTextCallCount = 0;
        m_typeTextResult = true;
        m_isCompatibleResult = true;
        m_providerNameResult = QStringLiteral("MockProvider");
        m_isWaitingForPermission = false;
        m_wasPermissionRejected = false;
    }

private:
    bool m_typeTextResult;
    bool m_isCompatibleResult;
    QString m_providerNameResult;
    bool m_isWaitingForPermission;
    bool m_wasPermissionRejected;
    int m_typeTextCallCount;
    QString m_lastTypedText;
    QStringList m_callHistory;
};

} // namespace Daemon
} // namespace YubiKeyOath
