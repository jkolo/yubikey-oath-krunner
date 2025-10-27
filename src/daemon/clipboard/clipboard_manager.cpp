/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "clipboard_manager.h"
#include "../logging_categories.h"

#include <KSystemClipboard>
#include <QMimeData>
#include <QTimer>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {

ClipboardManager::ClipboardManager(QObject *parent)
    : QObject(parent)
    , m_clipboard(KSystemClipboard::instance())
    , m_clearTimer(new QTimer(this))
{
    m_clearTimer->setSingleShot(true);
    connect(m_clearTimer, &QTimer::timeout, this, &ClipboardManager::onClearTimerTimeout);
    qCDebug(YubiKeyDaemonLog) << "ClipboardManager: Initialized with KSystemClipboard for Wayland support";
}

bool ClipboardManager::copyToClipboard(const QString &text, int clearAfterSeconds)
{
    qCDebug(YubiKeyDaemonLog) << "ClipboardManager: Copying sensitive text to clipboard"
             << "length:" << text.length()
             << "auto-clear:" << clearAfterSeconds << "seconds";

    if (!m_clipboard) {
        qCWarning(YubiKeyDaemonLog) << "ClipboardManager: Clipboard not available";
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

    // Verify clipboard content was actually set
    const QMimeData *clipboardMime = m_clipboard->mimeData(QClipboard::Clipboard);
    QString clipboardContent = clipboardMime ? clipboardMime->text() : QString();
    if (clipboardContent == text) {
        qCDebug(YubiKeyDaemonLog) << "ClipboardManager: Text copied successfully with KSystemClipboard - VERIFIED in clipboard";
    } else {
        qCWarning(YubiKeyDaemonLog) << "ClipboardManager: MISMATCH!"
                                     << "Expected:" << text
                                     << "Got:" << clipboardContent;
    }

    // Setup auto-clear timer if requested
    if (clearAfterSeconds > 0) {
        m_clearTimer->start(clearAfterSeconds * 1000);
        qCDebug(YubiKeyDaemonLog) << "ClipboardManager: Auto-clear scheduled in" << clearAfterSeconds << "seconds";
    } else {
        m_clearTimer->stop();
    }

    return true;
}

void ClipboardManager::clearClipboard()
{
    if (!m_clipboard) {
        qCWarning(YubiKeyDaemonLog) << "ClipboardManager: Clipboard not available";
        return;
    }

    // Only clear if clipboard still contains our text
    QString currentContent = m_clipboard->text(QClipboard::Clipboard);
    qCDebug(YubiKeyDaemonLog) << "ClipboardManager: clearClipboard() - current content:" << currentContent;
    qCDebug(YubiKeyDaemonLog) << "ClipboardManager: clearClipboard() - our last text:" << m_lastCopiedText;
    qCDebug(YubiKeyDaemonLog) << "ClipboardManager: clearClipboard() - match:" << (currentContent == m_lastCopiedText);
    if (currentContent == m_lastCopiedText) {
        m_clipboard->clear(QClipboard::Clipboard);
        qCDebug(YubiKeyDaemonLog) << "ClipboardManager: Clipboard cleared (contained our text)";
    } else {
        qCDebug(YubiKeyDaemonLog) << "ClipboardManager: Clipboard not cleared (content changed by user)";
    }

    m_lastCopiedText.clear();
    m_clearTimer->stop();
}

void ClipboardManager::onClearTimerTimeout()
{
    qCDebug(YubiKeyDaemonLog) << "ClipboardManager: Auto-clear timer expired";
    clearClipboard();
}

} // namespace Daemon
} // namespace YubiKeyOath
