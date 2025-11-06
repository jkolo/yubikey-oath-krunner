/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QClipboard>

// Forward declarations
class KSystemClipboard;
class QTimer;

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Manages clipboard operations
 *
 * Single Responsibility: Handle clipboard text operations with security features
 * - Marks copied data with x-kde-passwordManagerHint to prevent history storage
 * - Automatically clears clipboard after code expiration
 */
class ClipboardManager : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardManager(QObject *parent = nullptr);
    ~ClipboardManager() override = default;

    /**
     * @brief Copies sensitive text to system clipboard with security hints
     * @param text Text to copy
     * @param clearAfterSeconds Auto-clear timeout in seconds (0 = no auto-clear)
     * @return true if successful
     */
    virtual bool copyToClipboard(const QString &text, int clearAfterSeconds = 0);

    /**
     * @brief Manually clears clipboard if it contains our copied text
     */
    virtual void clearClipboard();

private Q_SLOTS:
    void onClearTimerTimeout();

private:
    KSystemClipboard *m_clipboard;
    QTimer *m_clearTimer;
    QString m_lastCopiedText;
};

} // namespace Daemon
} // namespace YubiKeyOath
