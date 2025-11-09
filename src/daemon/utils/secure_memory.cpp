/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "secure_memory.h"

#include <cstring>

// Check for explicit_bzero availability
#if defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
constexpr bool HAVE_EXPLICIT_BZERO = true;
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__)
constexpr bool HAVE_EXPLICIT_BZERO = true;
#else
constexpr bool HAVE_EXPLICIT_BZERO = false;
#endif

namespace YubiKeyOath {
namespace Daemon {

/**
 * @brief Securely zero memory
 * @param ptr Pointer to memory
 * @param size Size in bytes
 *
 * Uses explicit_bzero if available, otherwise volatile memset
 * to prevent compiler optimization from removing the memset call.
 */
static void secure_zero(void *ptr, size_t size)
{
    if (!ptr || size == 0) {
        return;
    }

    if constexpr (HAVE_EXPLICIT_BZERO) {
        // Use explicit_bzero (glibc 2.25+, BSD)
        explicit_bzero(ptr, size);
    } else {
        // Fallback: volatile pointer to prevent optimization
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast,modernize-use-auto,misc-const-correctness)
        volatile unsigned char *p = static_cast<volatile unsigned char *>(ptr);
        for (size_t i = 0; i < size; ++i) {
            p[i] = 0;
        }
    }
}

void SecureMemory::wipeString(QString &str)
{
    if (str.isEmpty()) {
        return;
    }

    // QString uses UTF-16 internally (QChar is 2 bytes)
    // We need to wipe the underlying data buffer

    // Get mutable data pointer (detaches if shared)
    // NOLINTNEXTLINE(misc-const-correctness) - we need to modify data
    QChar *data = str.data();
    const qsizetype length = str.length();

    // Wipe the character buffer
    secure_zero(data, static_cast<size_t>(length) * sizeof(QChar));

    // Clear the string (deallocates)
    str.clear();
}

void SecureMemory::wipeByteArray(QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }

    // Get mutable data pointer (detaches if shared)
    // NOLINTNEXTLINE(misc-const-correctness) - we need to modify data
    char *ptr = data.data();
    const qsizetype size = data.size();

    // Wipe the buffer
    secure_zero(ptr, static_cast<size_t>(size));

    // Clear the array (deallocates)
    data.clear();
}

} // namespace Daemon
} // namespace YubiKeyOath
