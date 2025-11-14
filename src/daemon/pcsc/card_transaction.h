/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include "i_oath_selector.h"

// Forward declarations for PC/SC types
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief RAII guard for PC/SC transactions with automatic OATH applet selection
 *
 * Purpose:
 * - Provides temporary exclusive access to smart card during OATH operations
 * - Automatically executes BEGIN_TRANSACTION + SELECT OATH in constructor
 * - Automatically executes END_TRANSACTION in destructor (exception-safe)
 * - Enables multi-application card sharing (GnuPG, ykman, etc.) between operations
 *
 * PC/SC Best Practice Pattern (per Microsoft & PC/SC Lite):
 * 1. Connect with SCARD_SHARE_SHARED (allows multiple apps to have connections)
 * 2. Use SCardBeginTransaction() for temporary exclusive access during operation
 * 3. Other applications wait (blocked) until SCardEndTransaction()
 * 4. After transaction ends, other apps can perform their operations
 *
 * Why SELECT OATH in constructor:
 * - Other applications (GnuPG, PIV tools) may SELECT different applets between operations
 * - Each transaction must start with known state: OATH applet selected
 * - Eliminates race conditions where OATH commands go to wrong applet
 *
 * Usage:
 * @code
 * Result<QString> generateCode() {
 *     CardTransaction txn(m_cardHandle, m_session);
 *     if (!txn.isValid()) {
 *         return Result<QString>::failure(txn.errorMessage());
 *     }
 *
 *     // OATH applet is now selected, transaction active
 *     // Other apps are blocked from sending APDU
 *     const QByteArray response = sendApdu(calculateCmd);
 *     // ... parse response
 *
 *     // Destructor automatically calls SCardEndTransaction
 *     // Other apps can now access card
 *     return Result<QString>::success(code);
 * }
 * @endcode
 *
 * Thread Safety:
 * - NOT thread-safe - caller must serialize access with mutex
 * - Single transaction per thread
 *
 * Exception Safety:
 * - Destructor ALWAYS calls SCardEndTransaction (even during stack unwinding)
 * - Guaranteed cleanup via RAII pattern
 */
class CardTransaction
{
public:
    /**
     * @brief Begins PC/SC transaction and selects OATH applet
     * @param cardHandle PC/SC card handle
     * @param session OATH session interface (for SELECT OATH operation)
     * @param skipOathSelect If true, skip automatic SELECT OATH (for non-OATH operations)
     *
     * Constructor performs:
     * 1. SCardBeginTransaction(cardHandle) - blocks other apps
     * 2. session->selectOathApplication() - unless skipOathSelect=true
     *
     * If either operation fails, isValid() returns false and errorMessage() contains details.
     */
    explicit CardTransaction(SCARDHANDLE cardHandle,
                           IOathSelector *session,
                           bool skipOathSelect = false);

    /**
     * @brief Ends PC/SC transaction (unblocks other applications)
     *
     * Destructor performs:
     * - SCardEndTransaction(cardHandle, SCARD_LEAVE_CARD)
     *
     * This is called automatically when CardTransaction goes out of scope,
     * even during exception unwinding (RAII guarantee).
     */
    ~CardTransaction();

    // Non-copyable (RAII semantics - single owner)
    CardTransaction(const CardTransaction &) = delete;
    CardTransaction &operator=(const CardTransaction &) = delete;

    // Movable (transfer ownership)
    CardTransaction(CardTransaction &&other) noexcept;
    CardTransaction &operator=(CardTransaction &&other) noexcept;

    /**
     * @brief Check if transaction was successfully started
     * @return true if BEGIN_TRANSACTION and SELECT OATH succeeded
     *
     * Always check this after construction:
     * @code
     * CardTransaction txn(handle, session);
     * if (!txn.isValid()) {
     *     return Result<T>::failure(txn.errorMessage());
     * }
     * @endcode
     */
    bool isValid() const { return m_transactionStarted && m_error.isEmpty(); }

    /**
     * @brief Get error message if transaction failed
     * @return Error description, or empty string if isValid() == true
     */
    QString errorMessage() const { return m_error; }

private:
    SCARDHANDLE m_cardHandle;
    bool m_transactionStarted;
    QString m_error;
};

} // namespace Daemon
} // namespace YubiKeyOath
