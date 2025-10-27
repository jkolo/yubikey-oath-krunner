/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "notification_utils.h"

namespace YubiKeyOath {
namespace Daemon {

QVariantMap NotificationUtils::createNotificationHints(int urgency, int progressValue)
{
    QVariantMap hints;
    hints[QStringLiteral("urgency")] = urgency;
    hints[QStringLiteral("value")] = progressValue;
    return hints;
}

} // namespace Daemon
} // namespace YubiKeyOath
