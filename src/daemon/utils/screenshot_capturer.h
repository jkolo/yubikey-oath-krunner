/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include "common/result.h"

// Forward declarations
class QDBusInterface;
class QDBusConnection;

namespace YubiKeyOath {
namespace Daemon {
using Shared::Result;

/**
 * @brief RAII wrapper for Unix file descriptors
 *
 * Ensures automatic cleanup of file descriptors to prevent resource leaks.
 * Move-only type with proper ownership semantics.
 */
class ScopedFileDescriptor
{
public:
    explicit ScopedFileDescriptor(int fd = -1) noexcept;
    ~ScopedFileDescriptor() noexcept;

    // Move semantics
    ScopedFileDescriptor(ScopedFileDescriptor&& other) noexcept;
    ScopedFileDescriptor& operator=(ScopedFileDescriptor&& other) noexcept;

    // Delete copy semantics
    ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
    ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;

    int get() const noexcept { return m_fd; }
    int release() noexcept;
    bool isValid() const noexcept { return m_fd >= 0; }
    void close() noexcept;

private:
    int m_fd;
};

/**
 * @brief Service for capturing screenshots using KWin ScreenShot2 D-Bus API
 *
 * KWin ScreenShot2 backend (Plasma 6.0+):
 * - No external dependencies
 * - Direct D-Bus communication with compositor
 * - In-memory processing (no temp files for security)
 *
 * Works on Wayland and X11. Requires X-KDE-DBUS-Restricted-Interfaces permission.
 */
class ScreenshotCapturer : public QObject
{
    Q_OBJECT

public:
    explicit ScreenshotCapturer(QObject *parent = nullptr);
    ~ScreenshotCapturer() override;

    /**
     * @brief Captures a fullscreen screenshot asynchronously (in-memory)
     * @param timeout Timeout in milliseconds (default 60000 = 60 seconds)
     *
     * This method:
     * 1. Uses KWin ScreenShot2 D-Bus API (Plasma 6.0+)
     * 2. Captures fullscreen screenshot asynchronously
     * 3. Emits captured(QImage) on success
     * 4. Emits cancelled() on failure
     *
     * Implementation:
     * - Creates Unix pipe and calls D-Bus CaptureWorkspace (non-blocking)
     * - Reads raw RGBA pixel data from pipe in background thread
     * - Constructs QImage from metadata (width/height/format) + pixel data
     * - All processing happens in memory (no temp files for security)
     *
     * Possible errors (emitted via cancelled):
     * - Failed to create pipe
     * - D-Bus call failed (access denied, KWin not available)
     * - Failed to read or decode image data
     * - Timeout waiting for data
     */
    void captureFullscreen(int timeout = 60000);

Q_SIGNALS:
    /**
     * @brief Emitted when screenshot capture completes
     * @param image Captured screenshot as QImage
     */
    void captured(const QImage &image);

    /**
     * @brief Emitted when screenshot capture is cancelled
     */
    void cancelled();

private:
    /**
     * @brief Performs the screenshot capture operation
     * @param timeout Timeout in milliseconds
     *
     * Implementation: Creates Unix pipe, calls D-Bus CaptureWorkspace,
     * then reads pixel data in background thread.
     * Emits captured() or cancelled() signals.
     */
    void performCapture(int timeout);

    /**
     * @brief Result of pipe reading operation
     */
    struct PipeReadResult {
        bool success;
        QByteArray data;
    };

    /**
     * @brief Reads raw pixel data from Unix pipe
     * @param fd File descriptor to read from
     * @param timeout Timeout in milliseconds
     * @return Result with data on success, empty on failure
     *
     * Reads data in chunks using select() for timeout handling.
     * Automatically handles EAGAIN/EWOULDBLOCK and EOF.
     */
    static PipeReadResult readPipeData(int fd, int timeout) noexcept;

    /**
     * @brief Creates QImage from raw pixel data
     * @param data Raw RGBA/ARGB pixel bytes
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param format KWin format string (e.g., "ARGB32", "RGBA8888")
     * @return QImage on success, null QImage on failure
     *
     * Validates data size matches expected dimensions and converts
     * KWin format strings to QImage::Format enum values.
     * Follows Qt idiom: ClassName::fromSource() (like QImage::fromData).
     */
    static QImage imageFromData(const QByteArray& data,
                                int width,
                                int height,
                                const QString& format) noexcept;

    // Constants for timeouts and buffer sizes
    static constexpr int DEFAULT_TIMEOUT_MS = 60000;
    static constexpr int MAX_TIMEOUT_MS = 300000;  // 5 minutes
    static constexpr int PIPE_BUFFER_SIZE = 4096;
    static constexpr int SELECT_TIMEOUT_SEC = 1;
};

} // namespace Daemon
} // namespace YubiKeyOath
