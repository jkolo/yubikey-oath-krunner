/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "card_reader_monitor.h"
#include "../logging_categories.h"
#include <QDebug>
#include <QMutexLocker>
#include <QStringList>
#include <vector>
#include <cstring>

namespace YubiKeyOath {
namespace Daemon {

// PC/SC state flags
#ifndef SCARD_STATE_UNAWARE
#define SCARD_STATE_UNAWARE 0x00000000
#endif
#ifndef SCARD_STATE_CHANGED
#define SCARD_STATE_CHANGED 0x00000002
#endif
#ifndef SCARD_STATE_PRESENT
#define SCARD_STATE_PRESENT 0x00000020
#endif
#ifndef SCARD_STATE_EMPTY
#define SCARD_STATE_EMPTY 0x00000010
#endif

// PC/SC error codes
#ifndef SCARD_E_UNKNOWN_READER
#define SCARD_E_UNKNOWN_READER ((LONG)0x80100009)
#endif

CardReaderMonitor::CardReaderMonitor(QObject *parent)
    : QThread(parent)
{
    qCDebug(CardReaderMonitorLog) << "Constructor called";
}

CardReaderMonitor::~CardReaderMonitor()
{
    qCDebug(CardReaderMonitorLog) << "Destructor called";
    stopMonitoring();
}

void CardReaderMonitor::startMonitoring(SCARDCONTEXT context)
{
    qCDebug(CardReaderMonitorLog) << "startMonitoring() called";

    QMutexLocker locker(&m_mutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks

    if (m_running) {
        qCDebug(CardReaderMonitorLog) << "Already running";
        return;
    }

    m_context = context;
    m_running = true;

    qCDebug(CardReaderMonitorLog) << "Starting thread";
    start();
}

void CardReaderMonitor::stopMonitoring()
{
    qCDebug(CardReaderMonitorLog) << "stopMonitoring() called";

    if (!m_running) {
        qCDebug(CardReaderMonitorLog) << "Not running";
        return;
    }

    m_running = false;

    // Cancel blocking SCardGetStatusChange()
    if (m_context) {
        qCDebug(CardReaderMonitorLog) << "Calling SCardCancel() to interrupt blocking call";
        SCardCancel(m_context);
    }

    // Wait for thread to finish
    if (isRunning()) {
        qCDebug(CardReaderMonitorLog) << "Waiting for thread to finish";
        wait(5000); // 5 second timeout
    }

    qCDebug(CardReaderMonitorLog) << "Stopped";
}

void CardReaderMonitor::run()
{
    qCDebug(CardReaderMonitorLog) << "Thread started";

    while (m_running) {
        // Monitor for reader changes (PnP) - detects USB YubiKey plug/unplug
        if (!monitorReaderChanges()) {
            break; // Error or cancelled
        }

        // Also monitor all existing readers for card insertion/removal
        // This is needed for NFC readers where YubiKey appears as a card
        if (!monitorAllReadersForCardChanges()) {
            break; // Error or cancelled
        }
    }

    qCDebug(CardReaderMonitorLog) << "Thread finished";
}

bool CardReaderMonitor::monitorReaderChanges()
{
    if (!m_running || !m_context) {
        return false;
    }

    SCARD_READERSTATE readerState;
    memset(&readerState, 0, sizeof(readerState));

    readerState.szReader = PNP_NOTIFICATION;
    readerState.dwCurrentState = m_lastPnPState;

    qCDebug(CardReaderMonitorLog) << "Monitoring for reader changes (PnP)";

    // Wait for reader change with 1 second timeout
    // (short timeout so we can check m_running frequently)
    const LONG result = SCardGetStatusChange(m_context, 1000, &readerState, 1);

    if (result == SCARD_E_TIMEOUT) {
        // Timeout is normal - no changes detected
        return true;
    }

    if (result == SCARD_E_CANCELLED) {
        qCDebug(CardReaderMonitorLog) << "SCardGetStatusChange cancelled (reader changes)";
        return false;
    }

    if (result != SCARD_S_SUCCESS) {
        qCWarning(CardReaderMonitorLog) << "SCardGetStatusChange failed (reader changes):"
                   << QString::number(result, 16);
        msleep(1000); // Wait before retry
        return true;
    }

    // Check if state changed
    if (readerState.dwEventState & SCARD_STATE_CHANGED) {
        qCDebug(CardReaderMonitorLog) << "Reader change detected - emitting readerListChanged()";
        Q_EMIT readerListChanged();

        // Update state for next iteration (clear CHANGED flag)
        m_lastPnPState = readerState.dwEventState & ~SCARD_STATE_CHANGED;
    }

    return true;
}

bool CardReaderMonitor::monitorCardChanges()
{
    if (!m_running || !m_context) {
        return false;
    }

    QMutexLocker locker(&m_mutex);  // NOLINT(misc-const-correctness) - QMutexLocker destructor unlocks
    const QString readerName = m_readerName;
    locker.unlock();

    if (readerName.isEmpty()) {
        return true;
    }

    SCARD_READERSTATE readerState;
    memset(&readerState, 0, sizeof(readerState));

    const QByteArray readerNameBytes = readerName.toUtf8();
    readerState.szReader = readerNameBytes.constData();
    readerState.dwCurrentState = m_lastReaderState;

    qCDebug(CardReaderMonitorLog) << "Monitoring reader for card changes:" << readerName
             << "last state:" << QString::number(m_lastReaderState, 16);

    // Wait for card change with 1 second timeout
    const LONG result = SCardGetStatusChange(m_context, 1000, &readerState, 1);

    if (result == SCARD_E_TIMEOUT) {
        // Timeout is normal - no changes detected
        return true;
    }

    if (result == SCARD_E_CANCELLED) {
        qCDebug(CardReaderMonitorLog) << "SCardGetStatusChange cancelled (card changes)";
        return false;
    }

    if (result != SCARD_S_SUCCESS) {
        qCWarning(CardReaderMonitorLog) << "SCardGetStatusChange failed (card changes):"
                   << QString::number(result, 16);
        msleep(1000); // Wait before retry
        return true;
    }

    // Check if state changed
    const DWORD eventState = readerState.dwEventState;
    const DWORD currentState = m_lastReaderState;

    qCDebug(CardReaderMonitorLog) << "State event - current:" << QString::number(currentState, 16)
             << "event:" << QString::number(eventState, 16)
             << "changed:" << bool(eventState & SCARD_STATE_CHANGED)
             << "present:" << bool(eventState & SCARD_STATE_PRESENT)
             << "empty:" << bool(eventState & SCARD_STATE_EMPTY);

    // Only process if state actually changed
    if (!(eventState & SCARD_STATE_CHANGED)) {
        qCDebug(CardReaderMonitorLog) << "No state change detected, skipping";
        return true;
    }

    // If this is first check after setReaderName (UNAWARE), just initialize state
    // without emitting signals - we're learning the current state, not detecting a change
    if (currentState == SCARD_STATE_UNAWARE) {
        qCDebug(CardReaderMonitorLog) << "Initial state detection - present:"
                 << bool(eventState & SCARD_STATE_PRESENT)
                 << "- not emitting signals";
        m_lastReaderState = eventState & ~SCARD_STATE_CHANGED;
        return true;
    }

    // Detect card insertion - card wasn't present before, now it is
    if ((eventState & SCARD_STATE_PRESENT) && !(currentState & SCARD_STATE_PRESENT)) {
        qCDebug(CardReaderMonitorLog) << "Card inserted into" << readerName;
        Q_EMIT cardInserted(readerName);
    }

    // Detect card removal - card was present before, now it's not
    // Don't check SCARD_STATE_EMPTY as it's not reliably set by all implementations
    if ((currentState & SCARD_STATE_PRESENT) && !(eventState & SCARD_STATE_PRESENT)) {
        qCDebug(CardReaderMonitorLog) << "Card removed from" << readerName;
        Q_EMIT cardRemoved(readerName);
    }

    // Update last known state (clear CHANGED flag to avoid re-processing)
    m_lastReaderState = eventState & ~SCARD_STATE_CHANGED;

    return true;
}

bool CardReaderMonitor::monitorAllReadersForCardChanges()
{
    if (!m_running || !m_context) {
        return false;
    }

    // Get list of all PC/SC readers
    DWORD readersLen = 0;
    LONG result = SCardListReaders(m_context, nullptr, nullptr, &readersLen);

    if (result == SCARD_E_NO_READERS_AVAILABLE) {
        // No readers available - this is normal, just continue
        msleep(1000);
        return true;
    }

    if (result != SCARD_S_SUCCESS) {
        qCWarning(CardReaderMonitorLog) << "SCardListReaders failed (get length):"
                   << QString::number(result, 16);
        msleep(1000);
        return true;
    }

    if (readersLen == 0) {
        // No readers - continue monitoring
        msleep(1000);
        return true;
    }

    // Allocate buffer and get reader names
    std::vector<char> readersBuffer(readersLen);
    result = SCardListReaders(m_context, nullptr, readersBuffer.data(), &readersLen);

    if (result != SCARD_S_SUCCESS) {
        qCWarning(CardReaderMonitorLog) << "SCardListReaders failed (get data):"
                   << QString::number(result, 16);
        msleep(1000);
        return true;
    }

    // Parse reader names (null-separated list, double-null terminated)
    QStringList currentReaders;
    const char *ptr = readersBuffer.data();
    while (*ptr) {
        const QString readerName = QString::fromUtf8(ptr);
        currentReaders.append(readerName);
        ptr += strlen(ptr) + 1;
    }

    if (currentReaders.isEmpty()) {
        msleep(1000);
        return true;
    }

    qCDebug(CardReaderMonitorLog) << "Monitoring" << currentReaders.size() << "readers for card changes";

    // Build array of SCARD_READERSTATE structures
    std::vector<SCARD_READERSTATE> readerStates;
    std::vector<QByteArray> readerNameBytes; // Keep data alive

    for (const QString &readerName : currentReaders) {
        SCARD_READERSTATE state;
        memset(&state, 0, sizeof(state));

        // Store reader name bytes (need to keep alive during SCardGetStatusChange)
        readerNameBytes.push_back(readerName.toUtf8());
        state.szReader = readerNameBytes.back().constData();

        // Get previous state for this reader, or UNAWARE if first time
        if (m_allReaderStates.contains(readerName)) {
            state.dwCurrentState = m_allReaderStates[readerName];
        } else {
            state.dwCurrentState = SCARD_STATE_UNAWARE;
        }

        readerStates.push_back(state);
    }

    // Monitor all readers with 1 second timeout
    result = SCardGetStatusChange(m_context, 1000, readerStates.data(), readerStates.size());

    if (result == SCARD_E_TIMEOUT) {
        // Timeout is normal - no changes detected
        return true;
    }

    if (result == SCARD_E_CANCELLED) {
        qCDebug(CardReaderMonitorLog) << "SCardGetStatusChange cancelled (all readers)";
        return false;
    }

    if (result == SCARD_E_UNKNOWN_READER) {
        // Reader list changed - clear cached states and retry next iteration
        qCDebug(CardReaderMonitorLog) << "Reader list changed - clearing cached states";
        m_allReaderStates.clear();
        return true;
    }

    if (result != SCARD_S_SUCCESS) {
        qCWarning(CardReaderMonitorLog) << "SCardGetStatusChange failed (all readers):"
                   << QString::number(result, 16);
        msleep(1000);
        return true;
    }

    // Process state changes for each reader
    for (size_t i = 0; i < readerStates.size(); i++) {
        const SCARD_READERSTATE &state = readerStates[i];
        const QString &readerName = currentReaders[static_cast<qsizetype>(i)];
        const DWORD eventState = state.dwEventState;
        const DWORD currentState = state.dwCurrentState;

        // Check if state changed
        if (!(eventState & SCARD_STATE_CHANGED)) {
            continue;
        }

        qCDebug(CardReaderMonitorLog) << "Reader" << readerName << "state changed - current:"
                 << QString::number(currentState, 16) << "event:" << QString::number(eventState, 16)
                 << "present:" << bool(eventState & SCARD_STATE_PRESENT);

        // If this is first check for this reader (UNAWARE), just initialize state
        if (currentState == SCARD_STATE_UNAWARE) {
            qCDebug(CardReaderMonitorLog) << "Initial state detection for" << readerName
                     << "- present:" << bool(eventState & SCARD_STATE_PRESENT)
                     << "- not emitting signals";
            m_allReaderStates[readerName] = eventState & ~SCARD_STATE_CHANGED;
            continue;
        }

        // Detect card insertion - card wasn't present before, now it is
        if ((eventState & SCARD_STATE_PRESENT) && !(currentState & SCARD_STATE_PRESENT)) {
            qCDebug(CardReaderMonitorLog) << "Card inserted into" << readerName;
            Q_EMIT cardInserted(readerName);
        }

        // Detect card removal - card was present before, now it's not
        if ((currentState & SCARD_STATE_PRESENT) && !(eventState & SCARD_STATE_PRESENT)) {
            qCDebug(CardReaderMonitorLog) << "Card removed from" << readerName;
            Q_EMIT cardRemoved(readerName);
        }

        // Update cached state for this reader (clear CHANGED flag)
        m_allReaderStates[readerName] = eventState & ~SCARD_STATE_CHANGED;
    }

    return true;
}

} // namespace Daemon
} // namespace YubiKeyOath
