/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include "common/result.h"

namespace KRunner {
namespace YubiKey {

/**
 * @brief Parser for QR codes in image files
 *
 * Uses QZXing library to decode QR codes from image files.
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

private:
    QrCodeParser() = delete; // Utility class - no instances
};

} // namespace YubiKey
} // namespace KRunner
