/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QDBusConnection>
#include <QTimer>

/**
 * @brief Helper class for creating isolated D-Bus sessions for E2E tests
 *
 * Manages a private D-Bus session bus for testing, ensuring tests don't
 * interfere with the user's production daemon or other tests.
 *
 * Features:
 * - Automatic dbus-daemon lifecycle management
 * - Daemon process management on test bus
 * - RAII cleanup (bus and daemon killed on destruction)
 * - Connection factory for creating test connections
 *
 * Usage:
 * @code
 * TestDbusSession testBus;
 * QVERIFY(testBus.start());
 *
 * QVERIFY(testBus.startDaemon("/usr/bin/yubikey-oath-daemon"));
 *
 * QDBusConnection connection = testBus.createConnection("test-conn");
 * OathManagerProxy manager(connection);
 * // ... test code
 *
 * // Cleanup automatic via RAII
 * @endcode
 *
 * Alternative: Use dbus-run-session wrapper in CMake:
 * @code
 * add_test(NAME test_e2e COMMAND dbus-run-session -- $<TARGET_FILE:test_e2e>)
 * @endcode
 */
class TestDbusSession : public QObject {
    Q_OBJECT

public:
    explicit TestDbusSession(QObject* parent = nullptr)
        : QObject(parent)
    {}

    ~TestDbusSession() {
        stop();
    }

    /**
     * @brief Start isolated D-Bus session
     * @return true on success
     */
    bool start();

    /**
     * @brief Stop D-Bus session and cleanup
     */
    void stop();

    /**
     * @brief Start daemon on test bus
     * @param daemonPath Absolute path to daemon binary
     * @param args Optional command-line arguments
     * @param waitMs Time to wait for daemon startup (default: 1000ms)
     * @return true on success
     */
    bool startDaemon(const QString& daemonPath,
                    const QStringList& args = {},
                    int waitMs = 1000);

    /**
     * @brief Stop daemon gracefully
     * @param waitMs Time to wait for graceful shutdown before kill (default: 3000ms)
     */
    void stopDaemon(int waitMs = 3000);

    /**
     * @brief Create D-Bus connection to test bus
     * @param name Connection name (unique per test)
     * @return QDBusConnection connected to test bus
     */
    QDBusConnection createConnection(const QString& name = QStringLiteral("test-connection"));

    /**
     * @brief Get test bus address
     * @return D-Bus address (e.g., "unix:path=/tmp/dbus-abc123")
     */
    QString address() const { return m_busAddress; }

    /**
     * @brief Check if D-Bus session is running
     */
    bool isRunning() const {
        return m_dbusProcess && m_dbusProcess->state() == QProcess::Running;
    }

    /**
     * @brief Check if daemon is running on test bus
     */
    bool isDaemonRunning() const {
        return m_daemonProcess && m_daemonProcess->state() == QProcess::Running;
    }

    /**
     * @brief Get daemon process for monitoring
     */
    QProcess* daemonProcess() const { return m_daemonProcess; }

private:
    QProcess* m_dbusProcess = nullptr;
    QProcess* m_daemonProcess = nullptr;
    QString m_busAddress;
};
