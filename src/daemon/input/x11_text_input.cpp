/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "x11_text_input.h"
#include "../logging_categories.h"

#include <QApplication>
#include <QProcess>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

X11TextInput::X11TextInput(QObject *parent)
    : TextInputProvider(parent)
{
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

} // namespace YubiKey
} // namespace KRunner
