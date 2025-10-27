/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QThread>
#include <QString>
#include <QMutex>
#include <QMap>
#include <atomic>

extern "C" {
#include <winscard.h>
}

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Monitors PC/SC card readers for connect/disconnect events
 *
 * Single Responsibility: Event-driven monitoring of smart card reader changes
 * - Uses SCardGetStatusChange() in background thread
 * - Emits signals when readers/cards appear or disappear
 * - Replaces polling-based detection
 *
 * Thread Safety: Runs in separate thread, uses signals for communication
 */
class CardReaderMonitor : public QThread
{
    Q_OBJECT

public:
    explicit CardReaderMonitor(QObject *parent = nullptr);
    ~CardReaderMonitor() override;

    /**
     * @brief Starts monitoring with given PC/SC context
     * @param context Active SCARDCONTEXT to use
     */
    void startMonitoring(SCARDCONTEXT context);

    /**
     * @brief Stops monitoring gracefully
     */
    void stopMonitoring();

Q_SIGNALS:
    /**
     * @brief Emitted when the list of readers changes (device added/removed)
     */
    void readerListChanged();

    /**
     * @brief Emitted when a new card reader is connected
     * @param readerName Name of the newly connected reader
     */
    void readerConnected(const QString &readerName);

    /**
     * @brief Emitted when a card reader is disconnected
     * @param readerName Name of the disconnected reader
     */
    void readerDisconnected(const QString &readerName);

    /**
     * @brief Emitted when a card is inserted into monitored reader
     * @param readerName Name of the reader with inserted card
     */
    void cardInserted(const QString &readerName);

    /**
     * @brief Emitted when a card is removed from monitored reader
     * @param readerName Name of the reader with removed card
     */
    void cardRemoved(const QString &readerName);

protected:
    /**
     * @brief Thread main loop - monitors using SCardGetStatusChange()
     */
    void run() override;

private:
    /**
     * @brief Monitors for new reader additions using PnP notification
     * @return true if should continue monitoring
     */
    bool monitorReaderChanges();

    /**
     * @brief Monitors specific reader for card insertion/removal
     * @return true if should continue monitoring
     */
    bool monitorCardChanges();

    /**
     * @brief Monitors all readers for card insertion/removal (for NFC detection)
     * @return true if should continue monitoring
     */
    bool monitorAllReadersForCardChanges();

    SCARDCONTEXT m_context = 0;
    QString m_readerName;
    QMutex m_mutex;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_hasReader{false};

    // Reader state tracking
    DWORD m_lastReaderState = SCARD_STATE_UNAWARE;      // For specific reader monitoring
    DWORD m_lastPnPState = SCARD_STATE_UNAWARE;         // For PnP reader list monitoring
    QMap<QString, DWORD> m_allReaderStates;             // Track state for all readers (for NFC)

    // Special reader name for PnP notifications
    static constexpr const char *PNP_NOTIFICATION = "\\\\?PnP?\\Notification";
};

} // namespace Daemon
} // namespace YubiKeyOath
