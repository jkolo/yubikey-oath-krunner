/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include <QImage>
#include "common/result.h"

namespace YubiKeyOath {
namespace Daemon {
using Shared::Result;

/**
 * @brief Parser for QR codes in images
 *
 * Uses ZXing library to decode QR codes from images (in-memory or from files).
 * Supports common image formats: PNG, JPG, BMP, etc.
 */
class QrCodeParser
{
public:
    /**
     * @brief Decodes QR code from image file
     * @param imagePath Path to image file
     * @return Result with decoded string on success, error message on failure
     *
     * The decoded string is typically an otpauth:// URI but could be any text.
     * Use OtpauthUriParser to parse the result if it's an OATH URI.
     *
     * Possible errors:
     * - File not found
     * - Failed to load image
     * - No QR code found in image
     * - Failed to decode QR code
     */
    static Result<QString> parse(const QString &imagePath);

    /**
     * @brief Decodes QR code from in-memory image
     * @param image QImage to decode
     * @return Result with decoded string on success, error message on failure
     *
     * Preferred method for security-sensitive screenshots (no disk I/O).
     * The decoded string is typically an otpauth:// URI but could be any text.
     * Use OtpauthUriParser to parse the result if it's an OATH URI.
     *
     * Possible errors:
     * - Image is null
     * - No QR code found in image
     * - Failed to decode QR code
     */
    static Result<QString> parse(const QImage &image);

private:
    QrCodeParser() = delete; // Utility class - no instances
};

} // namespace Daemon
} // namespace YubiKeyOath
