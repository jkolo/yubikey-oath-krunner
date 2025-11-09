/*
 * SPDX-FileCopyrightText: 2025 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "relative_time_formatter.h"

#include <KLocalizedString>

namespace YubiKeyOath {
namespace Config {

QString RelativeTimeFormatter::formatRelativeTime(const QDateTime &dateTime)
{
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 seconds = dateTime.secsTo(now);

    if (seconds < 60) {
        return i18n("just now");
    }

    const qint64 minutes = seconds / 60;
    if (minutes < 60) {
        return i18np("1 minute ago", "%1 minutes ago", minutes);
    }

    const qint64 hours = minutes / 60;
    if (hours < 24) {
        return i18np("1 hour ago", "%1 hours ago", hours);
    }

    const qint64 days = hours / 24;
    if (days == 1) {
        return i18n("yesterday");
    }
    if (days < 7) {
        return i18np("1 day ago", "%1 days ago", days);
    }

    const qint64 weeks = days / 7;
    if (weeks < 4) {
        return i18np("1 week ago", "%1 weeks ago", weeks);
    }

    const qint64 months = days / 30;
    if (months < 12) {
        return i18np("1 month ago", "%1 months ago", months);
    }

    return dateTime.toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace Config
} // namespace YubiKeyOath
