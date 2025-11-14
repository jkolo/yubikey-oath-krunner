/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "test_dbus_session.h"
#include <QTest>
#include <QProcessEnvironment>
#include <QDebug>
#include <QDBusError>

bool TestDbusSession::start()
{
    if (m_dbusProcess) {
        qWarning() << "TestDbusSession: D-Bus session already started";
        return false;
    }

    // Start dbus-daemon in foreground mode
    m_dbusProcess = new QProcess(this);
    m_dbusProcess->setProgram(QStringLiteral("dbus-daemon"));
    m_dbusProcess->setArguments({
        QStringLiteral("--session"),
        QStringLiteral("--nofork"),
        QStringLiteral("--print-address")
    });

    // Forward stderr for debugging
    m_dbusProcess->setProcessChannelMode(QProcess::SeparateChannels);

    m_dbusProcess->start();

    if (!m_dbusProcess->waitForStarted(5000)) {
        qCritical() << "TestDbusSession: Failed to start dbus-daemon:"
                    << m_dbusProcess->errorString();
        delete m_dbusProcess;
        m_dbusProcess = nullptr;
        return false;
    }

    // Read bus address from stdout
    if (!m_dbusProcess->waitForReadyRead(2000)) {
        qCritical() << "TestDbusSession: dbus-daemon did not output address";
        stop();
        return false;
    }

    m_busAddress = QString::fromUtf8(m_dbusProcess->readLine()).trimmed();

    if (m_busAddress.isEmpty()) {
        qCritical() << "TestDbusSession: Failed to read D-Bus address";
        stop();
        return false;
    }

    qDebug() << "TestDbusSession: D-Bus session started at:" << m_busAddress;
    return true;
}

void TestDbusSession::stop()
{
    // Stop daemon first
    stopDaemon();

    // Stop D-Bus daemon
    if (m_dbusProcess) {
        m_dbusProcess->terminate();

        if (!m_dbusProcess->waitForFinished(2000)) {
            qWarning() << "TestDbusSession: dbus-daemon did not terminate, killing";
            m_dbusProcess->kill();
            m_dbusProcess->waitForFinished(1000);
        }

        delete m_dbusProcess;
        m_dbusProcess = nullptr;
    }

    m_busAddress.clear();
}

bool TestDbusSession::startDaemon(const QString& daemonPath,
                                  const QStringList& args,
                                  int waitMs)
{
    if (m_busAddress.isEmpty()) {
        qWarning() << "TestDbusSession: D-Bus session not started, call start() first";
        return false;
    }

    if (m_daemonProcess) {
        qWarning() << "TestDbusSession: Daemon already started";
        return false;
    }

    // Create daemon process
    m_daemonProcess = new QProcess(this);
    m_daemonProcess->setProgram(daemonPath);
    m_daemonProcess->setArguments(args);

    // Set environment to use test bus
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), m_busAddress);

    // Enable debug logging
    env.insert(QStringLiteral("QT_LOGGING_RULES"), QStringLiteral("pl.jkolo.yubikey.oath.daemon.*=true"));
    env.insert(QStringLiteral("QT_LOGGING_TO_CONSOLE"), QStringLiteral("1"));

    m_daemonProcess->setProcessEnvironment(env);

    // Forward output for debugging
    m_daemonProcess->setProcessChannelMode(QProcess::ForwardedChannels);

    m_daemonProcess->start();

    if (!m_daemonProcess->waitForStarted(5000)) {
        qCritical() << "TestDbusSession: Failed to start daemon:"
                    << m_daemonProcess->errorString();
        delete m_daemonProcess;
        m_daemonProcess = nullptr;
        return false;
    }

    qDebug() << "TestDbusSession: Daemon started, waiting" << waitMs << "ms for initialization";

    // Wait for daemon initialization
    QTest::qWait(waitMs);

    // Check if daemon is still running
    if (m_daemonProcess->state() != QProcess::Running) {
        qCritical() << "TestDbusSession: Daemon exited during initialization"
                    << "Exit code:" << m_daemonProcess->exitCode();
        delete m_daemonProcess;
        m_daemonProcess = nullptr;
        return false;
    }

    qDebug() << "TestDbusSession: Daemon running successfully";
    return true;
}

void TestDbusSession::stopDaemon(int waitMs)
{
    if (!m_daemonProcess) {
        return;
    }

    qDebug() << "TestDbusSession: Stopping daemon";

    m_daemonProcess->terminate();

    if (!m_daemonProcess->waitForFinished(waitMs)) {
        qWarning() << "TestDbusSession: Daemon did not terminate gracefully, killing";
        m_daemonProcess->kill();
        m_daemonProcess->waitForFinished(1000);
    }

    delete m_daemonProcess;
    m_daemonProcess = nullptr;
}

QDBusConnection TestDbusSession::createConnection(const QString& name)
{
    if (m_busAddress.isEmpty()) {
        qWarning() << "TestDbusSession: D-Bus session not started";
        return QDBusConnection(QStringLiteral("invalid"));
    }

    // Connect to test bus
    QDBusConnection connection = QDBusConnection::connectToBus(m_busAddress, name);

    if (!connection.isConnected()) {
        qCritical() << "TestDbusSession: Failed to connect to test bus:"
                    << connection.lastError().message();
    } else {
        qDebug() << "TestDbusSession: Created connection" << name << "to test bus";
    }

    return connection;
}
