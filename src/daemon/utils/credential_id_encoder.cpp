/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "credential_id_encoder.h"

#include <QCryptographicHash>
#include <QHash>

namespace YubiKeyOath {
namespace Daemon {

QString CredentialIdEncoder::encode(const QString &credentialName)
{
    // Encode credential name for use in D-Bus object path
    // D-Bus paths allow only: [A-Za-z0-9_/]
    // Use transliteration for Unicode characters and special character mappings

    // Transliteration map for common Unicode characters
    static const QHash<QChar, QString> translitMap = {
        // Polish characters (lowercase)
        {QChar(0x0105), QStringLiteral("a")},  // ą
        {QChar(0x0107), QStringLiteral("c")},  // ć
        {QChar(0x0119), QStringLiteral("e")},  // ę
        {QChar(0x0142), QStringLiteral("l")},  // ł
        {QChar(0x0144), QStringLiteral("n")},  // ń
        {QChar(0x00F3), QStringLiteral("o")},  // ó
        {QChar(0x015B), QStringLiteral("s")},  // ś
        {QChar(0x017A), QStringLiteral("z")},  // ź
        {QChar(0x017C), QStringLiteral("z")},  // ż
        // Polish characters (uppercase)
        {QChar(0x0104), QStringLiteral("a")},  // Ą
        {QChar(0x0106), QStringLiteral("c")},  // Ć
        {QChar(0x0118), QStringLiteral("e")},  // Ę
        {QChar(0x0141), QStringLiteral("l")},  // Ł
        {QChar(0x0143), QStringLiteral("n")},  // Ń
        {QChar(0x00D3), QStringLiteral("o")},  // Ó
        {QChar(0x015A), QStringLiteral("s")},  // Ś
        {QChar(0x0179), QStringLiteral("z")},  // Ź
        {QChar(0x017B), QStringLiteral("z")},  // Ż
        // Common special characters with readable mappings
        {QLatin1Char('@'), QStringLiteral("_at_")},
        {QLatin1Char('.'), QStringLiteral("_dot_")},
        {QLatin1Char(':'), QStringLiteral("_colon_")},
        {QLatin1Char(' '), QStringLiteral("_")},
        {QLatin1Char('('), QStringLiteral("_")},
        {QLatin1Char(')'), QStringLiteral("_")},
        {QLatin1Char('-'), QStringLiteral("_")},
        {QLatin1Char('+'), QStringLiteral("_plus_")},
        {QLatin1Char('='), QStringLiteral("_eq_")},
        {QLatin1Char('/'), QStringLiteral("_slash_")},
        {QLatin1Char('\\'), QStringLiteral("_backslash_")},
        {QLatin1Char('&'), QStringLiteral("_and_")},
        {QLatin1Char('%'), QStringLiteral("_percent_")},
        {QLatin1Char('#'), QStringLiteral("_hash_")},
        {QLatin1Char('!'), QStringLiteral("_excl_")},
        {QLatin1Char('?'), QStringLiteral("_q_")},
        {QLatin1Char('*'), QStringLiteral("_star_")},
        {QLatin1Char(','), QStringLiteral("_")},
        {QLatin1Char(';'), QStringLiteral("_")},
        {QLatin1Char('\''), QStringLiteral("_")},
        {QLatin1Char('"'), QStringLiteral("_")},
        {QLatin1Char('['), QStringLiteral("_")},
        {QLatin1Char(']'), QStringLiteral("_")},
        {QLatin1Char('{'), QStringLiteral("_")},
        {QLatin1Char('}'), QStringLiteral("_")},
        {QLatin1Char('<'), QStringLiteral("_lt_")},
        {QLatin1Char('>'), QStringLiteral("_gt_")},
        {QLatin1Char('|'), QStringLiteral("_pipe_")},
        {QLatin1Char('~'), QStringLiteral("_tilde_")},
        {QLatin1Char('`'), QStringLiteral("_")},
    };

    QString encoded;
    encoded.reserve(credentialName.length() * 3); // Reserve space for worst case

    for (const QChar &ch : credentialName) {
        // Check if it's ASCII letter or number or underscore
        if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
            (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
            (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
            ch == QLatin1Char('_')) {
            // Keep ASCII alphanumeric and underscore as-is (lowercase)
            encoded.append(ch.toLower());
        } else if (translitMap.contains(ch)) {
            // Use transliteration mapping
            encoded.append(translitMap.value(ch));
        } else if (ch.unicode() < 128) {
            // Other ASCII characters not in map - replace with underscore
            encoded.append(QLatin1Char('_'));
        } else {
            // Unicode character not in map - encode as _uXXXX
            encoded.append(QString::fromLatin1("_u%1").arg(
                static_cast<ushort>(ch.unicode()), 4, 16, QLatin1Char('0')));
        }
    }

    // If starts with digit, prepend 'c'
    if (!encoded.isEmpty() && encoded[0].isDigit()) {
        encoded.prepend(QLatin1Char('c'));
    }

    // Truncate if too long (max 255 chars for D-Bus element)
    if (encoded.length() > 200) {
        // Use hash for very long names
        const QByteArray hash = QCryptographicHash::hash(credentialName.toUtf8(),
                                                          QCryptographicHash::Sha256);
        encoded = QString::fromLatin1("cred_") + QString::fromLatin1(hash.toHex().left(16));
    }

    return encoded;
}

} // namespace Daemon
} // namespace YubiKeyOath
