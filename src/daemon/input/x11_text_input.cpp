/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "x11_text_input.h"
#include "../logging_categories.h"

#include <QApplication>
#include <QProcess>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {

X11TextInput::X11TextInput(SecretStorage * /*secretStorage*/, QObject *parent)
    : TextInputProvider(parent)
{
    // Note: X11TextInput doesn't need SecretStorage (no token persistence required),
    // but accepts it for API consistency with other TextInputProvider implementations
}

bool X11TextInput::typeText(const QString &text)
{
    qCDebug(TextInputLog) << "X11TextInput: Typing text, length:" << text.length();

    QProcess process;
    QStringList args;
    args << QStringLiteral("type") << text;

    process.start(QStringLiteral("xdotool"), args);
    if (!process.waitForFinished(5000)) {
        qCWarning(TextInputLog) << "X11TextInput: xdotool failed or timed out";
        return false;
    }

    if (process.exitCode() != 0) {
        qCWarning(TextInputLog) << "X11TextInput: xdotool failed:" << process.readAllStandardError();
        return false;
    }

    qCDebug(TextInputLog) << "X11TextInput: Text typed successfully";
    return true;
}

bool X11TextInput::isCompatible() const
{
    return QApplication::platformName() == QStringLiteral("xcb");
}

QString X11TextInput::providerName() const
{
    return QStringLiteral("X11");
}

} // namespace Daemon
} // namespace YubiKeyOath
