/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "screenshot_capturer.h"
#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
#include <QImage>
#include <QPointer>
#include <QMap>
#include <QtConcurrent>
#include <KLocalizedString>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <utility>  // std::exchange
#include <array>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

// =============================================================================
// ScopedFileDescriptor implementation
// =============================================================================

ScopedFileDescriptor::ScopedFileDescriptor(int fd) noexcept
    : m_fd(fd)
{
}

ScopedFileDescriptor::~ScopedFileDescriptor() noexcept
{
    close();
}

ScopedFileDescriptor::ScopedFileDescriptor(ScopedFileDescriptor&& other) noexcept
    : m_fd(std::exchange(other.m_fd, -1))
{
}

ScopedFileDescriptor& ScopedFileDescriptor::operator=(ScopedFileDescriptor&& other) noexcept
{
    if (this != &other) {
        close();
        m_fd = std::exchange(other.m_fd, -1);
    }
    return *this;
}

int ScopedFileDescriptor::release() noexcept
{
    return std::exchange(m_fd, -1);
}

void ScopedFileDescriptor::close() noexcept
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

// =============================================================================
// ScreenshotCapturer implementation
// =============================================================================

ScreenshotCapturer::ScreenshotCapturer(QObject *parent)
    : QObject(parent)
{
}

ScreenshotCapturer::~ScreenshotCapturer() = default;

ScreenshotCapturer::PipeReadResult ScreenshotCapturer::readPipeData(int fd, int timeout) noexcept
{
    QByteArray imageData;
    std::array<char, PIPE_BUFFER_SIZE> buffer{};

    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeout) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        struct timeval tv{};
        tv.tv_sec = SELECT_TIMEOUT_SEC;
        tv.tv_usec = 0;

        const int selectResult = select(fd + 1, &readfds, nullptr, nullptr, &tv);

        if (selectResult < 0) {
            qWarning() << "ScreenshotCapture: select() failed:" << strerror(errno);
            return {.success = false, .data = QByteArray()};
        }

        if (selectResult == 0) {
            // Timeout - check if we got some data
            if (!imageData.isEmpty()) {
                // Got data but nothing more coming - probably EOF
                break;
            }
            continue;
        }

        const ssize_t bytesRead = read(fd, buffer.data(), PIPE_BUFFER_SIZE);

        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available yet, continue
                continue;
            }
            qWarning() << "ScreenshotCapture: read() failed:" << strerror(errno);
            return {.success = false, .data = QByteArray()};
        }

        if (bytesRead == 0) {
            // EOF - compositor finished writing
            qDebug() << "ScreenshotCapture: EOF reached, total bytes:" << imageData.size();
            break;
        }

        imageData.append(buffer.data(), bytesRead);
    }

    if (imageData.isEmpty()) {
        qWarning() << "ScreenshotCapture: No data received from pipe (timeout or empty)";
        return {.success = false, .data = QByteArray()};
    }

    qDebug() << "ScreenshotCapture: Received" << imageData.size() << "bytes";
    return {.success = true, .data = imageData};
}

QImage ScreenshotCapturer::imageFromData(const QByteArray& data,
                                         int width,
                                         int height,
                                         const QString& format) noexcept
{
    const int expectedSize = width * height * 4;

    if (data.size() != expectedSize) {
        qWarning() << "ScreenshotCapture: Data size mismatch";
        qWarning() << "  Expected:" << expectedSize << "bytes (" << width << "x" << height << "x 4)";
        qWarning() << "  Received:" << data.size() << "bytes";
        return {};
    }

    // Static map for format conversion
    static const QMap<QString, QImage::Format> formatMap = {
        {QStringLiteral("ARGB32"), QImage::Format_ARGB32},
        {QStringLiteral("argb8888"), QImage::Format_ARGB32},
        {QStringLiteral("RGB32"), QImage::Format_RGB32},
        {QStringLiteral("RGBA8888"), QImage::Format_RGBA8888}
    };

    const QImage::Format imageFormat = formatMap.value(format, QImage::Format_ARGB32);

    if (!formatMap.contains(format)) {
        qDebug() << "ScreenshotCapture: Unknown format" << format << ", assuming ARGB32";
    }

    QImage image(width, height, imageFormat);

    if (image.isNull()) {
        qWarning() << "ScreenshotCapture: Failed to create QImage with dimensions" << width << "x" << height;
        return {};
    }

    // Copy pixel data to QImage
    memcpy(image.bits(), data.constData(), data.size());

    qDebug() << "ScreenshotCapture: Screenshot created successfully";
    qDebug() << "  - Size:" << image.width() << "x" << image.height();
    qDebug() << "  - Format:" << image.format();

    return image;
}

void ScreenshotCapturer::performCapture(int timeout)
{
    qDebug() << "ScreenshotCapture: Using KWin ScreenShot2 for async capture";

    // 1. Connect to KWin ScreenShot2 interface
    QDBusInterface kwinInterface(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/org/kde/KWin/ScreenShot2"),
        QStringLiteral("org.kde.KWin.ScreenShot2"),
        QDBusConnection::sessionBus()
    );

    if (!kwinInterface.isValid()) {
        const QString error = kwinInterface.lastError().message();
        qWarning() << "ScreenshotCapture: Failed to connect to KWin ScreenShot2:" << error;
        Q_EMIT cancelled();
        return;
    }

    // 2. Create Unix pipe for data transfer with RAII
    std::array<int, 2> pipeFds{};
    // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay) - POSIX pipe2() API requirement
    if (pipe2(pipeFds.data(), O_CLOEXEC | O_NONBLOCK) != 0) {
        qWarning() << "ScreenshotCapture: Failed to create pipe:" << strerror(errno);
        Q_EMIT cancelled();
        return;
    }

    ScopedFileDescriptor readFd(pipeFds[0]);
    ScopedFileDescriptor writeFd(pipeFds[1]);

    qDebug() << "ScreenshotCapture: Created pipe [read:" << readFd.get() << ", write:" << writeFd.get() << "]";

    // 3. Create D-Bus file descriptor (write end)
    const QDBusUnixFileDescriptor dbusWriteFd(writeFd.get());
    if (!dbusWriteFd.isValid()) {
        qWarning() << "ScreenshotCapture: Failed to create valid D-Bus file descriptor";
        Q_EMIT cancelled();
        return;
    }

    // 4. Prepare screenshot options
    const QVariantMap options = {
        {QStringLiteral("include-cursor"), false},
        {QStringLiteral("native-resolution"), true}
    };

    qDebug() << "ScreenshotCapture: Calling CaptureWorkspace with pipe FD";

    // 5. Call CaptureWorkspace (async - compositor writes to pipe)
    const QDBusReply<QVariantMap> reply = kwinInterface.call(
        QStringLiteral("CaptureWorkspace"),
        QVariant::fromValue(options),
        QVariant::fromValue(dbusWriteFd)
    );

    // Close write end - compositor has its own copy
    writeFd.close();

    // 6. Check for D-Bus errors
    if (!reply.isValid()) {
        const QDBusError& error = reply.error();
        qWarning() << "ScreenshotCapture: KWin CaptureWorkspace failed:"
                   << error.name() << error.message();
        Q_EMIT cancelled();
        return;
    }

    // 7. Extract metadata (const correctness)
    const QVariantMap metadata = reply.value();
    const int width = metadata.value(QStringLiteral("width")).toInt();
    const int height = metadata.value(QStringLiteral("height")).toInt();
    const QString format = metadata.value(QStringLiteral("format")).toString();

    qDebug() << "ScreenshotCapture: Metadata from KWin:";
    qDebug() << "  - Width:" << width;
    qDebug() << "  - Height:" << height;
    qDebug() << "  - Format:" << format;

    if (width <= 0 || height <= 0) {
        qWarning() << "ScreenshotCapture: Invalid dimensions:" << width << "x" << height;
        Q_EMIT cancelled();
        return;
    }

    // 8. Read pipe data in background thread (async)
    qDebug() << "ScreenshotCapture: Starting async pipe read...";

    // Transfer ownership of FD to background thread
    const int fd = readFd.release();

    // Simplified lambda using extracted methods
    const QFuture<QPair<bool, QImage>> future = QtConcurrent::run(
        [fd, width, height, format, timeout]() -> QPair<bool, QImage>
    {
        const ScopedFileDescriptor scopedFd(fd);  // RAII ensures cleanup

        qDebug() << "ScreenshotCapture: Background thread reading from pipe...";

        // Read raw pixel data from pipe
        auto [success, data] = readPipeData(scopedFd.get(), timeout);
        if (!success) {
            return qMakePair(false, QImage());
        }

        // Create QImage from pixel data
        const QImage image = imageFromData(data, width, height, format);

        return qMakePair(!image.isNull(), image);
        // scopedFd automatically closes FD on scope exit
    });

    // 9. Setup watcher with race condition protection
    // Watcher without parent to avoid double-delete
    auto *watcher = new QFutureWatcher<QPair<bool, QImage>>();

    // Use QPointer for safe access to 'this'
    const QPointer<ScreenshotCapturer> self(this);

    connect(watcher, &QFutureWatcher<QPair<bool, QImage>>::finished,
            this, [self, watcher]()
    {
        // Check if object still exists
        if (!self) {
            watcher->deleteLater();
            return;
        }

        const QPair<bool, QImage> result = watcher->result();

        if (result.first && !result.second.isNull()) {
            qDebug() << "ScreenshotCapturer: Emitting captured signal";
            Q_EMIT self->captured(result.second);
        } else {
            qWarning() << "ScreenshotCapturer: Screenshot capture failed, emitting cancelled";
            Q_EMIT self->cancelled();
        }

        watcher->deleteLater();
    });

    watcher->setFuture(future);

    qDebug() << "ScreenshotCapturer: Async capture initiated, returning to UI thread";
}

void ScreenshotCapturer::captureFullscreen(int timeout)
{
    // Validate and clamp timeout to reasonable range
    if (timeout <= 0) {
        qWarning() << "ScreenshotCapturer: Invalid timeout" << timeout << "ms, using default" << DEFAULT_TIMEOUT_MS << "ms";
        timeout = DEFAULT_TIMEOUT_MS;
    } else if (timeout > MAX_TIMEOUT_MS) {
        qWarning() << "ScreenshotCapturer: Timeout" << timeout << "ms exceeds maximum, capping at" << MAX_TIMEOUT_MS << "ms";
        timeout = MAX_TIMEOUT_MS;
    }

    // KWin ScreenShot2 is the only supported backend
    performCapture(timeout);
}

} // namespace Daemon
} // namespace YubiKeyOath
