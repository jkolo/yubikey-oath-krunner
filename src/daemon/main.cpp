/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_dbus_service.h"
#include "types/yubikey_value_types.h"
#include "types/device_state.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>
#include <KLocalizedString>

int main(int argc, char *argv[])
{
    // NOLINTNEXTLINE(misc-const-correctness) - QApplication is modified internally by Qt
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("yubikey-oath-daemon"));
    app.setOrganizationName(QStringLiteral("jkolo"));
    app.setOrganizationDomain(QStringLiteral("jkolo.pl"));

    // Daemon should continue running even when dialogs are closed
    app.setQuitOnLastWindowClosed(false);

    // Set translation domain for i18n
    KLocalizedString::setApplicationDomain("yubikey_oath");

    // Set desktop file name for XDG Portal app ID
    app.setDesktopFileName(QStringLiteral("pl.jkolo.yubikey.oath.daemon"));

    // Register custom types for Qt's metatype system (required for signals/slots)
    // Note: D-Bus type registration is now handled by OathDBusService
    qRegisterMetaType<YubiKeyOath::Shared::DeviceInfo>("YubiKeyOath::Shared::DeviceInfo");
    qRegisterMetaType<YubiKeyOath::Shared::CredentialInfo>("YubiKeyOath::Shared::CredentialInfo");
    qRegisterMetaType<YubiKeyOath::Shared::GenerateCodeResult>("YubiKeyOath::Shared::GenerateCodeResult");
    qRegisterMetaType<YubiKeyOath::Shared::AddCredentialResult>("YubiKeyOath::Shared::AddCredentialResult");
    qRegisterMetaType<QList<YubiKeyOath::Shared::DeviceInfo>>("QList<YubiKeyOath::Shared::DeviceInfo>");
    qRegisterMetaType<QList<YubiKeyOath::Shared::CredentialInfo>>("QList<YubiKeyOath::Shared::CredentialInfo>");
    qRegisterMetaType<YubiKeyOath::Shared::DeviceState>("YubiKeyOath::Shared::DeviceState");

    // Create service
    const YubiKeyOath::Daemon::OathDBusService service;

    // Register on session bus
    QDBusConnection connection = QDBusConnection::sessionBus();

    if (!connection.registerService(QStringLiteral("pl.jkolo.yubikey.oath.daemon"))) {
        qCritical() << "Could not register D-Bus service:"
                    << connection.lastError().message();
        return 1;
    }

    return app.exec();
}
