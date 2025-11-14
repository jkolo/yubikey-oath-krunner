/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "card_transaction.h"
#include "../logging_categories.h"

#include <QLoggingCategory>

namespace YubiKeyOath {
namespace Daemon {

CardTransaction::CardTransaction(SCARDHANDLE cardHandle,
                               IOathSelector *session,
                               bool skipOathSelect)
    : m_cardHandle(cardHandle)
    , m_transactionStarted(false)
{
    // Validate card handle
    if (m_cardHandle == 0) {
        m_error = QStringLiteral("Invalid card handle: 0");
        qCWarning(YubiKeyPcscLog) << m_error;
        return;
    }

    // Step 1: Begin PC/SC transaction (blocks other applications)
    qCDebug(YubiKeyPcscLog) << "Beginning PC/SC transaction for card handle" << cardHandle;

    const LONG result = SCardBeginTransaction(m_cardHandle);

    if (result != SCARD_S_SUCCESS) {
        m_error = QStringLiteral("SCardBeginTransaction failed: 0x%1").arg(result, 0, 16);
        qCWarning(YubiKeyPcscLog) << m_error;
        return;
    }

    m_transactionStarted = true;
    qCDebug(YubiKeyPcscLog) << "PC/SC transaction started successfully";

    // Step 2: Select OATH applet (unless skipped for non-OATH operations)
    if (!skipOathSelect) {
        // Validate session pointer when SELECT is required
        if (!session) {
            m_error = QStringLiteral("Session pointer is null but SELECT OATH is required");
            qCWarning(YubiKeyPcscLog) << m_error;
            // Keep m_transactionStarted = true so destructor will call EndTransaction
            return;
        }

        qCDebug(YubiKeyPcscLog) << "Selecting OATH applet";

        QByteArray challenge;
        Version firmwareVersion;
        auto selectResult = session->selectOathApplication(challenge, firmwareVersion);

        if (selectResult.isError()) {
            m_error = QStringLiteral("SELECT OATH failed: %1").arg(selectResult.error());
            qCWarning(YubiKeyPcscLog) << m_error;
            // Keep m_transactionStarted = true so destructor will call EndTransaction
            return;
        }

        qCDebug(YubiKeyPcscLog) << "OATH applet selected successfully";
    } else {
        qCDebug(YubiKeyPcscLog) << "Skipping OATH applet selection (skipOathSelect=true)";
    }
}

CardTransaction::~CardTransaction()
{
    if (m_transactionStarted && m_cardHandle != 0) {
        qCDebug(YubiKeyPcscLog) << "Ending PC/SC transaction for card handle" << m_cardHandle;

        const LONG result = SCardEndTransaction(m_cardHandle, SCARD_LEAVE_CARD);

        if (result != SCARD_S_SUCCESS) {
            qCWarning(YubiKeyPcscLog)
                << "SCardEndTransaction failed: 0x" << Qt::hex << result
                << "(continuing anyway to avoid resource leak)";
        } else {
            qCDebug(YubiKeyPcscLog) << "PC/SC transaction ended successfully";
        }
    }
}

// Move constructor
CardTransaction::CardTransaction(CardTransaction &&other) noexcept
    : m_cardHandle(other.m_cardHandle)
    , m_transactionStarted(other.m_transactionStarted)
    , m_error(std::move(other.m_error))
{
    // Invalidate the source object (prevent double-EndTransaction)
    other.m_cardHandle = 0;
    other.m_transactionStarted = false;
}

// Move assignment
CardTransaction &CardTransaction::operator=(CardTransaction &&other) noexcept
{
    if (this != &other) {
        // End our own transaction first (if active)
        if (m_transactionStarted && m_cardHandle != 0) {
            qCDebug(YubiKeyPcscLog) << "Move assignment: ending existing transaction for card handle" << m_cardHandle;
            const LONG result = SCardEndTransaction(m_cardHandle, SCARD_LEAVE_CARD);
            if (result != SCARD_S_SUCCESS) {
                qCWarning(YubiKeyPcscLog) << "Move assignment: SCardEndTransaction failed: 0x" << Qt::hex << result;
            }
        }

        // Transfer ownership from other
        m_cardHandle = other.m_cardHandle;
        m_transactionStarted = other.m_transactionStarted;
        m_error = std::move(other.m_error);

        // Invalidate source
        other.m_cardHandle = 0;
        other.m_transactionStarted = false;
    }
    return *this;
}

} // namespace Daemon
} // namespace YubiKeyOath
