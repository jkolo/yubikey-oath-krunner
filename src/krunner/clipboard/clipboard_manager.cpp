/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clipboard_manager.h"
#include "../logging_categories.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTimer>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

ClipboardManager::ClipboardManager(QObject *parent)
    : QObject(parent)
    , m_clipboard(QGuiApplication::clipboard())
    , m_clearTimer(new QTimer(this))
{
    m_clearTimer->setSingleShot(true);
    connect(m_clearTimer, &QTimer::timeout, this, &ClipboardManager::onClearTimerTimeout);
    qCDebug(YubiKeyRunnerLog) << "ClipboardManager: Initialized with security features";
}

bool ClipboardManager::copyToClipboard(const QString &text, int clearAfterSeconds)
{
    qCDebug(YubiKeyRunnerLog) << "ClipboardManager: Copying sensitive text to clipboard"
             << "length:" << text.length()
             << "auto-clear:" << clearAfterSeconds << "seconds";

    if (!m_clipboard) {
        qCWarning(YubiKeyRunnerLog) << "ClipboardManager: Clipboard not available";
        return false;
    }

    // Create MIME data with security hint for KDE Plasma's Klipper
    // This prevents the password/OTP from being stored in clipboard history
    QMimeData *mimeData = new QMimeData();
    mimeData->setText(text);

    // Add x-kde-passwordManagerHint to mark this as sensitive data
    // Klipper will not store this in history
    const QByteArray hint = "secret";
    mimeData->setData(QStringLiteral("x-kde-passwordManagerHint"), hint);

    m_clipboard->setMimeData(mimeData, QClipboard::Clipboard);
    m_lastCopiedText = text;

    qCDebug(YubiKeyRunnerLog) << "ClipboardManager: Text copied successfully with x-kde-passwordManagerHint";

    // Setup auto-clear timer if requested
    if (clearAfterSeconds > 0) {
        m_clearTimer->start(clearAfterSeconds * 1000);
        qCDebug(YubiKeyRunnerLog) << "ClipboardManager: Auto-clear scheduled in" << clearAfterSeconds << "seconds";
    } else {
        m_clearTimer->stop();
    }

    return true;
}

void ClipboardManager::clearClipboard()
{
    if (!m_clipboard) {
        qCWarning(YubiKeyRunnerLog) << "ClipboardManager: Clipboard not available";
        return;
    }

    // Only clear if clipboard still contains our text
    if (m_clipboard->text() == m_lastCopiedText) {
        m_clipboard->clear();
        qCDebug(YubiKeyRunnerLog) << "ClipboardManager: Clipboard cleared (contained our text)";
    } else {
        qCDebug(YubiKeyRunnerLog) << "ClipboardManager: Clipboard not cleared (content changed by user)";
    }

    m_lastCopiedText.clear();
    m_clearTimer->stop();
}

void ClipboardManager::onClearTimerTimeout()
{
    qCDebug(YubiKeyRunnerLog) << "ClipboardManager: Auto-clear timer expired";
    clearClipboard();
}

} // namespace YubiKey
} // namespace KRunner
