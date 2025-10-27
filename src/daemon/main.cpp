/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_dbus_service.h"
#include "types/yubikey_value_types.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMetaType>
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

    // Log Qt platform plugin and clipboard availability
    qWarning() << "Qt platform:" << QApplication::platformName();
    qWarning() << "Wayland display:" << qgetenv("WAYLAND_DISPLAY");
    qWarning() << "X11 display:" << qgetenv("DISPLAY");

    // Register custom types for D-Bus marshaling
    qRegisterMetaType<YubiKeyOath::Shared::DeviceInfo>("YubiKeyOath::Shared::DeviceInfo");
    qRegisterMetaType<YubiKeyOath::Shared::CredentialInfo>("YubiKeyOath::Shared::CredentialInfo");
    qRegisterMetaType<YubiKeyOath::Shared::GenerateCodeResult>("YubiKeyOath::Shared::GenerateCodeResult");
    qRegisterMetaType<YubiKeyOath::Shared::AddCredentialResult>("YubiKeyOath::Shared::AddCredentialResult");
    qRegisterMetaType<QList<YubiKeyOath::Shared::DeviceInfo>>("QList<YubiKeyOath::Shared::DeviceInfo>");
    qRegisterMetaType<QList<YubiKeyOath::Shared::CredentialInfo>>("QList<YubiKeyOath::Shared::CredentialInfo>");

    qDBusRegisterMetaType<YubiKeyOath::Shared::DeviceInfo>();
    qDBusRegisterMetaType<YubiKeyOath::Shared::CredentialInfo>();
    qDBusRegisterMetaType<YubiKeyOath::Shared::GenerateCodeResult>();
    qDBusRegisterMetaType<YubiKeyOath::Shared::AddCredentialResult>();
    qDBusRegisterMetaType<QList<YubiKeyOath::Shared::DeviceInfo>>();
    qDBusRegisterMetaType<QList<YubiKeyOath::Shared::CredentialInfo>>();

    // Create service
    YubiKeyOath::Daemon::YubiKeyDBusService service;

    // Register on session bus
    QDBusConnection connection = QDBusConnection::sessionBus();

    if (!connection.registerService(QStringLiteral("pl.jkolo.yubikey.oath.daemon"))) {
        qCritical() << "Could not register D-Bus service:"
                    << connection.lastError().message();
        return 1;
    }

    // Legacy /Device interface removed - use hierarchical architecture instead:
    // Manager: /pl/jkolo/yubikey/oath
    // Devices: /pl/jkolo/yubikey/oath/devices/<deviceId>
    // Credentials: /pl/jkolo/yubikey/oath/devices/<deviceId>/credentials/<credentialId>

    qInfo() << "YubiKey OATH daemon started successfully";
    qInfo() << "D-Bus service: pl.jkolo.yubikey.oath.daemon";
    qInfo() << "D-Bus architecture: hierarchical (ObjectManager pattern)";

    return app.exec();
}
