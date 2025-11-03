/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QSqlDatabase>

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief RAII guard for database transactions
 *
 * Single Responsibility: Automatic transaction lifecycle management
 *
 * This class uses RAII (Resource Acquisition Is Initialization) pattern
 * to ensure proper transaction handling:
 * - Constructor: begins transaction
 * - Destructor: auto-rollback if not committed
 * - commit(): explicit commit
 *
 * @par Benefits
 * - Exception safe: auto-rollback on exceptions
 * - No manual cleanup needed
 * - Prevents resource leaks
 * - Clear transaction boundaries
 *
 * @par Usage Example
 * @code
 * TransactionGuard guard(database);
 *
 * if (!deleteOldData()) {
 *     return false; // guard auto-rollbacks in destructor
 * }
 *
 * if (!insertNewData()) {
 *     return false; // guard auto-rollbacks in destructor
 * }
 *
 * return guard.commit(); // explicit commit on success
 * @endcode
 *
 * @note Thread-safe: each guard operates on its own database connection
 */
class TransactionGuard
{
public:
    /**
     * @brief Constructs guard and begins transaction
     * @param db Database to manage transaction for
     *
     * Automatically starts transaction via db.transaction().
     * If transaction fails to start, isValid() returns false.
     */
    explicit TransactionGuard(QSqlDatabase &db);

    /**
     * @brief Destructor - auto-rollback if not committed
     *
     * If commit() was not called, automatically rolls back transaction.
     * This ensures cleanup even on early returns or exceptions.
     *
     * @note Always succeeds, never throws
     */
    ~TransactionGuard();

    // Disable copy and move - guard must be unique per transaction
    TransactionGuard(const TransactionGuard &) = delete;
    TransactionGuard &operator=(const TransactionGuard &) = delete;
    TransactionGuard(TransactionGuard &&) = delete;
    TransactionGuard &operator=(TransactionGuard &&) = delete;

    /**
     * @brief Commits transaction
     * @return true if commit successful, false on failure
     *
     * On success: marks transaction as committed (no rollback in destructor)
     * On failure: attempts rollback and returns false
     *
     * @note Can only be called once per guard
     */
    bool commit();

    /**
     * @brief Checks if transaction was started successfully
     * @return true if transaction is active and valid
     *
     * Returns false if:
     * - Transaction failed to start in constructor
     * - Transaction was already committed
     * - Transaction was already rolled back
     */
    bool isValid() const { return m_transactionStarted && !m_committed; }

private:
    QSqlDatabase &m_db;
    bool m_transactionStarted = false;
    bool m_committed = false;
};

} // namespace Daemon
} // namespace YubiKeyOath
