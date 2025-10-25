/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "screenshot_capture.h"
#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

ScreenshotCapture::ScreenshotCapture(QObject *parent)
    : QObject(parent)
    , m_spectacleInterface(nullptr)
    , m_responseReceived(false)
    , m_cancelled(false)
{
    // Connect to Spectacle D-Bus interface
    m_spectacleInterface = new QDBusInterface(
        QStringLiteral("org.kde.Spectacle"),
        QStringLiteral("/"),
        QStringLiteral("org.kde.Spectacle"),
        QDBusConnection::sessionBus(),
        this
    );

    if (!m_spectacleInterface->isValid()) {
        qWarning() << "ScreenshotCapture: Failed to connect to Spectacle:"
                   << m_spectacleInterface->lastError().message();
    }
}

ScreenshotCapture::~ScreenshotCapture()
{
    // QObject parent will delete m_spectacleInterface
}

bool ScreenshotCapture::ensureSpectacleConnection()
{
    // If interface is already valid, we're good
    if (m_spectacleInterface && m_spectacleInterface->isValid()) {
        return true;
    }

    // Check if Spectacle D-Bus service is registered
    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    if (!iface) {
        qWarning() << "ScreenshotCapture: Cannot access D-Bus connection interface";
        return false;
    }

    const QString serviceName = QStringLiteral("org.kde.Spectacle");
    if (!iface->isServiceRegistered(serviceName)) {
        qDebug() << "ScreenshotCapture: Spectacle service not registered on D-Bus";
        return false;
    }

    // Service is available - recreate interface
    qDebug() << "ScreenshotCapture: Recreating Spectacle D-Bus interface";

    // Delete old interface if exists
    if (m_spectacleInterface) {
        delete m_spectacleInterface;
        m_spectacleInterface = nullptr;
    }

    // Create new interface
    m_spectacleInterface = new QDBusInterface(
        serviceName,
        QStringLiteral("/"),
        serviceName,
        QDBusConnection::sessionBus(),
        this
    );

    if (!m_spectacleInterface->isValid()) {
        qWarning() << "ScreenshotCapture: Failed to recreate Spectacle interface:"
                   << m_spectacleInterface->lastError().message();
        return false;
    }

    qDebug() << "ScreenshotCapture: Successfully connected to Spectacle";
    return true;
}

Result<QString> ScreenshotCapture::captureInteractive(int timeout)
{
    // Ensure Spectacle connection is valid (recreate if needed)
    if (!ensureSpectacleConnection()) {
        return Result<QString>::error(i18n("Spectacle not available"));
    }

    qDebug() << "ScreenshotCapture: Using Spectacle for fullscreen capture";

    // Reset state
    m_responseReceived = false;
    m_cancelled = false;
    m_capturedFilePath.clear();

    // Connect to ScreenshotTaken signal
    bool signalConnected = QDBusConnection::sessionBus().connect(
        QStringLiteral("org.kde.Spectacle"),
        QStringLiteral("/"),
        QStringLiteral("org.kde.Spectacle"),
        QStringLiteral("ScreenshotTaken"),
        this,
        SLOT(onSpectacleScreenshotTaken(QString))
    );

    if (!signalConnected) {
        qWarning() << "ScreenshotCapture: Failed to connect to Spectacle ScreenshotTaken signal";
        return Result<QString>::error(i18n("Failed to connect to Spectacle signal"));
    }

    // Connect to ScreenshotFailed signal
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.kde.Spectacle"),
        QStringLiteral("/"),
        QStringLiteral("org.kde.Spectacle"),
        QStringLiteral("ScreenshotFailed"),
        this,
        SLOT(onSpectacleScreenshotFailed(QString))
    );

    // Call FullScreen method (0 = don't include mouse pointer)
    QDBusReply<void> reply = m_spectacleInterface->call(QStringLiteral("FullScreen"), 0);

    if (!reply.isValid()) {
        qWarning() << "ScreenshotCapture: Spectacle FullScreen call failed:"
                   << reply.error().message();

        // Disconnect signals
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.kde.Spectacle"),
            QStringLiteral("/"),
            QStringLiteral("org.kde.Spectacle"),
            QStringLiteral("ScreenshotTaken"),
            this,
            SLOT(onSpectacleScreenshotTaken(QString))
        );
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.kde.Spectacle"),
            QStringLiteral("/"),
            QStringLiteral("org.kde.Spectacle"),
            QStringLiteral("ScreenshotFailed"),
            this,
            SLOT(onSpectacleScreenshotFailed(QString))
        );

        return Result<QString>::error(
            i18n("Failed to request Spectacle screenshot: %1", reply.error().message()));
    }

    qDebug() << "ScreenshotCapture: Spectacle FullScreen requested, waiting for signal...";

    // Wait for response with processEvents to keep UI responsive
    QElapsedTimer timer;
    timer.start();

    while (!m_responseReceived && timer.elapsed() < timeout) {
        // Process events to keep UI responsive (100ms max per iteration)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        // Small sleep to avoid busy-waiting
        QThread::msleep(50);
    }

    // Disconnect signals
    QDBusConnection::sessionBus().disconnect(
        QStringLiteral("org.kde.Spectacle"),
        QStringLiteral("/"),
        QStringLiteral("org.kde.Spectacle"),
        QStringLiteral("ScreenshotTaken"),
        this,
        SLOT(onSpectacleScreenshotTaken(QString))
    );
    QDBusConnection::sessionBus().disconnect(
        QStringLiteral("org.kde.Spectacle"),
        QStringLiteral("/"),
        QStringLiteral("org.kde.Spectacle"),
        QStringLiteral("ScreenshotFailed"),
        this,
        SLOT(onSpectacleScreenshotFailed(QString))
    );

    // Check result
    if (!m_responseReceived) {
        return Result<QString>::error(i18n("Timeout waiting for Spectacle screenshot"));
    }

    if (m_cancelled) {
        return Result<QString>::error(i18n("Spectacle screenshot failed"));
    }

    if (m_capturedFilePath.isEmpty()) {
        return Result<QString>::error(i18n("No screenshot file path received from Spectacle"));
    }

    qDebug() << "ScreenshotCapture: Spectacle captured screenshot:" << m_capturedFilePath;
    return Result<QString>::success(m_capturedFilePath);
}

void ScreenshotCapture::onSpectacleScreenshotTaken(const QString &filePath)
{
    qDebug() << "ScreenshotCapture: Spectacle ScreenshotTaken signal:" << filePath;

    m_responseReceived = true;
    m_capturedFilePath = filePath;
    Q_EMIT screenshotCaptured(filePath);
}

void ScreenshotCapture::onSpectacleScreenshotFailed(const QString &errorMessage)
{
    qWarning() << "ScreenshotCapture: Spectacle ScreenshotFailed signal:" << errorMessage;

    m_responseReceived = true;
    m_cancelled = true;
    Q_EMIT screenshotCancelled();
}

} // namespace YubiKey
} // namespace KRunner
