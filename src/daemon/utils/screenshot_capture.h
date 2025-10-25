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
 * @brief Captures screenshots using KDE Spectacle
 *
 * Uses Spectacle D-Bus interface for automatic fullscreen screenshots.
 * Works on Wayland and X11 without requiring portal permissions.
 */
class ScreenshotCapture : public QObject
{
    Q_OBJECT

public:
    explicit ScreenshotCapture(QObject *parent = nullptr);
    ~ScreenshotCapture() override;

    /**
     * @brief Captures a fullscreen screenshot automatically
     * @param timeout Timeout in milliseconds (default 60000 = 60 seconds)
     * @return Result with screenshot file path on success, error message on failure
     *
     * This method:
     * 1. Calls Spectacle FullScreen D-Bus method (automatic, no user interaction)
     * 2. Waits for ScreenshotTaken signal with file path
     * 3. Returns local file path
     *
     * The method blocks until screenshot is captured or timeout occurs.
     * Screenshot file is saved by Spectacle.
     *
     * Possible errors:
     * - Spectacle not available
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
    void onSpectacleScreenshotTaken(const QString &filePath);
    void onSpectacleScreenshotFailed(const QString &errorMessage);

private:
    /**
     * @brief Ensures Spectacle D-Bus connection is valid
     * @return true if connection is ready, false otherwise
     *
     * Checks if D-Bus interface is valid. If not, attempts to recreate
     * connection if Spectacle service is available.
     */
    bool ensureSpectacleConnection();

    QDBusInterface *m_spectacleInterface;
    QString m_capturedFilePath;
    bool m_responseReceived;
    bool m_cancelled;
};

} // namespace YubiKey
} // namespace KRunner
