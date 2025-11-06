/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "daemon/clipboard/clipboard_manager.h"
#include <QObject>
#include <QString>
#include <QStringList>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Mock implementation of ClipboardManager for testing
 *
 * Inherits from ClipboardManager and overrides methods for testing
 */
class MockClipboardManager : public ClipboardManager
{
    Q_OBJECT

public:
    explicit MockClipboardManager(QObject *parent = nullptr)
        : ClipboardManager(parent)
        , m_shouldSucceed(true)
        , m_lastCopiedText()
        , m_lastClearAfterSeconds(0)
        , m_copiedCount(0)
        , m_clearCount(0)
    {}

    ~MockClipboardManager() override;

    /**
     * @brief Mock clipboard copy operation
     * @param text Text to copy
     * @param clearAfterSeconds Auto-clear timeout in seconds
     * @return Success status (controlled by setShouldSucceed)
     */
    bool copyToClipboard(const QString &text, int clearAfterSeconds = 0) override
    {
        m_lastCopiedText = text;
        m_lastClearAfterSeconds = clearAfterSeconds;
        m_copiedCount++;

        // Record call for verification
        m_callHistory.append(QString("copyToClipboard(%1, %2)").arg(text).arg(clearAfterSeconds));

        // Don't call base class implementation
        return m_shouldSucceed;
    }

    /**
     * @brief Mock clipboard clear operation
     */
    void clearClipboard() override
    {
        m_lastCopiedText.clear();
        m_clearCount++;

        // Record call for verification
        m_callHistory.append(QStringLiteral("clearClipboard()"));

        // Don't call base class implementation
    }

    // ========== Test Helper Methods ==========

    /**
     * @brief Sets whether copyToClipboard should succeed
     * @param succeed If true, returns true; if false, returns false
     */
    void setShouldSucceed(bool succeed)
    {
        m_shouldSucceed = succeed;
    }

    /**
     * @brief Gets last copied text
     */
    QString lastCopiedText() const
    {
        return m_lastCopiedText;
    }

    /**
     * @brief Gets last clearAfterSeconds value
     */
    int lastClearAfterSeconds() const
    {
        return m_lastClearAfterSeconds;
    }

    /**
     * @brief Gets number of copy operations
     */
    int copiedCount() const
    {
        return m_copiedCount;
    }

    /**
     * @brief Gets number of clear operations
     */
    int clearCount() const
    {
        return m_clearCount;
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
        m_lastCopiedText.clear();
        m_lastClearAfterSeconds = 0;
        m_copiedCount = 0;
        m_clearCount = 0;
        m_shouldSucceed = true;
    }

private:
    bool m_shouldSucceed;
    QString m_lastCopiedText;
    int m_lastClearAfterSeconds;
    int m_copiedCount;
    int m_clearCount;
    QStringList m_callHistory;
};

} // namespace Daemon
} // namespace YubiKeyOath
