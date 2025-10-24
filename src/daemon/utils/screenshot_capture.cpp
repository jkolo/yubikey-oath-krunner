/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "screenshot_capture.h"
#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QDebug>
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

ScreenshotCapture::ScreenshotCapture(QObject *parent)
    : QObject(parent)
    , m_portalInterface(nullptr)
    , m_responseReceived(false)
    , m_cancelled(false)
{
    // Create D-Bus interface to XDG Desktop Portal
    m_portalInterface = new QDBusInterface(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Screenshot"),
        QDBusConnection::sessionBus(),
        this
    );

    if (!m_portalInterface->isValid()) {
        qWarning() << "ScreenshotCapture: Failed to connect to XDG Desktop Portal:"
                   << m_portalInterface->lastError().message();
    }
}

ScreenshotCapture::~ScreenshotCapture()
{
    // QObject parent will delete m_portalInterface
}

Result<QString> ScreenshotCapture::captureInteractive(int timeout)
{
    if (!m_portalInterface || !m_portalInterface->isValid()) {
        return Result<QString>::error(i18n("XDG Desktop Portal not available"));
    }

    qDebug() << "ScreenshotCapture: Starting interactive capture";

    // Reset state
    m_responseReceived = false;
    m_cancelled = false;
    m_capturedFilePath.clear();
    m_requestPath.clear();

    // Prepare Screenshot method call
    // Screenshot(parent_window: s, options: a{sv}) -> handle: o
    QString parentWindow; // Empty = no parent window
    QVariantMap options;
    options[QStringLiteral("modal")] = false;        // Non-modal capture
    options[QStringLiteral("interactive")] = false;  // Capture full screen without dialog

    // Call Screenshot method
    QDBusReply<QDBusObjectPath> reply = m_portalInterface->call(
        QStringLiteral("Screenshot"),
        parentWindow,
        options
    );

    if (!reply.isValid()) {
        qWarning() << "ScreenshotCapture: Screenshot call failed:"
                   << reply.error().message();
        return Result<QString>::error(
            i18n("Failed to request screenshot: %1", reply.error().message()));
    }

    // Get request handle path
    m_requestPath = reply.value().path();
    qDebug() << "ScreenshotCapture: Got request handle:" << m_requestPath;

    // Subscribe to Response signal on the request handle
    // Signal: Response(response: u, results: a{sv})
    bool signalConnected = QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        m_requestPath,
        QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"),
        this,
        SLOT(onPortalResponse(uint, QVariantMap))
    );

    if (!signalConnected) {
        qWarning() << "ScreenshotCapture: Failed to connect to Response signal";
        return Result<QString>::error(i18n("Failed to connect to portal signal"));
    }

    // Wait for response using event loop with timeout
    QEventLoop loop;
    QTimer timeoutTimer;

    // Connect completion signals
    connect(this, &ScreenshotCapture::screenshotCaptured, &loop, &QEventLoop::quit);
    connect(this, &ScreenshotCapture::screenshotCancelled, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeoutTimer.setSingleShot(true);
    timeoutTimer.start(timeout);

    qDebug() << "ScreenshotCapture: Waiting for user to select window...";
    loop.exec(); // Block until signal or timeout

    // Disconnect signal
    QDBusConnection::sessionBus().disconnect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        m_requestPath,
        QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"),
        this,
        SLOT(onPortalResponse(uint, QVariantMap))
    );

    // Check result
    if (!m_responseReceived) {
        return Result<QString>::error(i18n("Timeout waiting for screenshot"));
    }

    if (m_cancelled) {
        return Result<QString>::error(i18n("Screenshot cancelled by user"));
    }

    if (m_capturedFilePath.isEmpty()) {
        return Result<QString>::error(i18n("No screenshot file path received"));
    }

    qDebug() << "ScreenshotCapture: Successfully captured screenshot:" << m_capturedFilePath;
    return Result<QString>::success(m_capturedFilePath);
}

void ScreenshotCapture::onPortalResponse(uint response, const QVariantMap &results)
{
    qDebug() << "ScreenshotCapture: Received Response signal, code:" << response;

    m_responseReceived = true;

    // Response codes:
    // 0 = success
    // 1 = user cancelled
    // 2 = other error

    if (response != 0) {
        qWarning() << "ScreenshotCapture: User cancelled or error occurred, code:" << response;
        m_cancelled = true;
        Q_EMIT screenshotCancelled();
        return;
    }

    // Extract URI from results
    if (!results.contains(QStringLiteral("uri"))) {
        qWarning() << "ScreenshotCapture: Response missing 'uri' field";
        Q_EMIT screenshotCancelled();
        return;
    }

    QString uri = results[QStringLiteral("uri")].toString();
    qDebug() << "ScreenshotCapture: Got screenshot URI:" << uri;

    // Convert file:// URI to local path
    QUrl url(uri);
    if (url.isLocalFile()) {
        m_capturedFilePath = url.toLocalFile();
    } else {
        m_capturedFilePath = uri;
    }

    Q_EMIT screenshotCaptured(m_capturedFilePath);
}

} // namespace YubiKey
} // namespace KRunner
