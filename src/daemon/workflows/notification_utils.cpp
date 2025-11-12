/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "notification_utils.h"
#include <QVariant>

namespace YubiKeyOath {
namespace Daemon {

QVariantMap NotificationUtils::createNotificationHints(uchar urgency,
                                                        int progressValue,
                                                        const QString& iconName)
{
    QVariantMap hints;

    // CRITICAL: D-Bus notification spec requires 'urgency' hint as BYTE type (D-Bus signature 'y')
    // QVariant::fromValue<uchar> preserves the byte type; direct assignment would create INT type
    // Wrong type causes D-Bus errors: "Expected 'y' (byte), got 'i' (int32)" in notification daemon
    // Example of WRONG approach: hints["urgency"] = urgency;  // Would be int, breaks D-Bus!
    hints[QStringLiteral("urgency")] = QVariant::fromValue(urgency);

    // Progress value (0-100 percent)
    hints[QStringLiteral("value")] = progressValue;

    // Optional image-path hint for device-specific icons
    // Uses freedesktop.org icon theme name (e.g., "yubikey-5c-nfc")
    // System automatically selects appropriate size and fallback
    if (!iconName.isEmpty()) {
        hints[QStringLiteral("image-path")] = iconName;
    }

    return hints;
}

} // namespace Daemon
} // namespace YubiKeyOath
