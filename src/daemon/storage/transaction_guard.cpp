/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "transaction_guard.h"
#include "../logging_categories.h"

#include <QSqlError>

namespace YubiKeyOath {
namespace Daemon {

TransactionGuard::TransactionGuard(QSqlDatabase &db)
    : m_db(db)
    , m_transactionStarted(m_db.transaction())
{
    if (!m_transactionStarted) {
        qCWarning(OathDatabaseLog) << "TransactionGuard: Failed to start transaction:"
                                      << m_db.lastError().text();
    } else {
        qCDebug(OathDatabaseLog) << "TransactionGuard: Transaction started";
    }
}

TransactionGuard::~TransactionGuard()
{
    // Auto-rollback if transaction was started but not committed
    if (m_transactionStarted && !m_committed) {
        qCDebug(OathDatabaseLog) << "TransactionGuard: Auto-rolling back uncommitted transaction";
        m_db.rollback();
    }
}

bool TransactionGuard::commit()
{
    if (!m_transactionStarted) {
        qCWarning(OathDatabaseLog) << "TransactionGuard: Cannot commit - transaction was not started";
        return false;
    }

    if (m_committed) {
        qCWarning(OathDatabaseLog) << "TransactionGuard: Cannot commit - already committed";
        return false;
    }

    if (!m_db.commit()) {
        qCWarning(OathDatabaseLog) << "TransactionGuard: Commit failed:"
                                      << m_db.lastError().text();
        qCDebug(OathDatabaseLog) << "TransactionGuard: Rolling back after failed commit";
        m_db.rollback();
        return false;
    }

    m_committed = true;
    qCDebug(OathDatabaseLog) << "TransactionGuard: Transaction committed successfully";
    return true;
}

} // namespace Daemon
} // namespace YubiKeyOath
