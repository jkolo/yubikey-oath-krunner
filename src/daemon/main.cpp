/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikey_dbus_service.h"
#include "../shared/dbus/yubikey_dbus_types.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMetaType>
#include <QDebug>

int main(int argc, char *argv[])
{
    // NOLINTNEXTLINE(misc-const-correctness) - QCoreApplication is modified internally by Qt
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("yubikey-oath-daemon"));
    app.setOrganizationName(QStringLiteral("KDE"));
    app.setOrganizationDomain(QStringLiteral("kde.org"));

    // Register custom types for D-Bus marshaling
    qRegisterMetaType<KRunner::YubiKey::DeviceInfo>("KRunner::YubiKey::DeviceInfo");
    qRegisterMetaType<KRunner::YubiKey::CredentialInfo>("KRunner::YubiKey::CredentialInfo");
    qRegisterMetaType<KRunner::YubiKey::GenerateCodeResult>("KRunner::YubiKey::GenerateCodeResult");
    qRegisterMetaType<QList<KRunner::YubiKey::DeviceInfo>>("QList<KRunner::YubiKey::DeviceInfo>");
    qRegisterMetaType<QList<KRunner::YubiKey::CredentialInfo>>("QList<KRunner::YubiKey::CredentialInfo>");

    qDBusRegisterMetaType<KRunner::YubiKey::DeviceInfo>();
    qDBusRegisterMetaType<KRunner::YubiKey::CredentialInfo>();
    qDBusRegisterMetaType<KRunner::YubiKey::GenerateCodeResult>();
    qDBusRegisterMetaType<QList<KRunner::YubiKey::DeviceInfo>>();
    qDBusRegisterMetaType<QList<KRunner::YubiKey::CredentialInfo>>();

    // Create service
    KRunner::YubiKey::YubiKeyDBusService service;

    // Register on session bus
    QDBusConnection connection = QDBusConnection::sessionBus();

    if (!connection.registerService(QStringLiteral("org.kde.plasma.krunner.yubikey"))) {
        qCritical() << "Could not register D-Bus service:"
                    << connection.lastError().message();
        return 1;
    }

    if (!connection.registerObject(QStringLiteral("/Device"),
                                   &service,
                                   QDBusConnection::ExportAllSlots |
                                   QDBusConnection::ExportAllSignals)) {
        qCritical() << "Could not register D-Bus object:"
                    << connection.lastError().message();
        return 1;
    }

    qInfo() << "YubiKey OATH daemon started successfully";
    qInfo() << "D-Bus service: org.kde.plasma.krunner.yubikey";
    qInfo() << "D-Bus object path: /Device";

    return app.exec();
}
