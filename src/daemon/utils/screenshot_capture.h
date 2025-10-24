/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include "common/result.h"

// Forward declarations
class QDBusInterface;
class QDBusConnection;

namespace KRunner {
namespace YubiKey {

/**
 * @brief Captures screenshots using XDG Desktop Portal
 *
 * Uses org.freedesktop.portal.Desktop Screenshot interface.
 * This works across Wayland and X11 with proper sandboxing.
 */
class ScreenshotCapture : public QObject
{
    Q_OBJECT

public:
    explicit ScreenshotCapture(QObject *parent = nullptr);
    ~ScreenshotCapture() override;

    /**
     * @brief Captures a screenshot with interactive window selection
     * @param timeout Timeout in milliseconds (default 60000 = 60 seconds)
     * @return Result with screenshot file path on success, error message on failure
     *
     * This method:
     * 1. Calls XDG Portal Screenshot method with interactive=true
     * 2. User selects window/area via portal dialog
     * 3. Waits for Response signal with screenshot URI
     * 4. Returns local file path (converts file:// URI)
     *
     * The method blocks until user selects window or cancels.
     * Screenshot file is temporary and will be cleaned up by portal.
     *
     * Possible errors:
     * - Portal not available
     * - User cancelled selection
     * - Screenshot failed
     * - Timeout waiting for response
     */
    Result<QString> captureInteractive(int timeout = 60000);

Q_SIGNALS:
    /**
     * @brief Emitted when screenshot capture completes
     * @param filePath Path to captured screenshot, or empty on error
     */
    void screenshotCaptured(const QString &filePath);

    /**
     * @brief Emitted when screenshot capture is cancelled
     */
    void screenshotCancelled();

private Q_SLOTS:
    void onPortalResponse(uint response, const QVariantMap &results);

private:
    QDBusInterface *m_portalInterface;
    QString m_requestPath;
    QString m_capturedFilePath;
    bool m_responseReceived;
    bool m_cancelled;
};

} // namespace YubiKey
} // namespace KRunner
