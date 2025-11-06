/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qr_code_parser.h"
#include "../logging_categories.h"
#include <QFile>
#include <QImage>
#include <QDebug>
#include <KLocalizedString>
#include <ZXing/ReadBarcode.h>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

Result<QString> QrCodeParser::parse(const QString &imagePath)
{
    // Check if file exists
    if (!QFile::exists(imagePath)) {
        return Result<QString>::error(i18n("Image file not found: %1", imagePath));
    }

    // Load image
    const QImage image(imagePath);
    if (image.isNull()) {
        return Result<QString>::error(i18n("Failed to load image: %1", imagePath));
    }

    qCDebug(QrCodeParserLog) << "Processing image from file" << imagePath
             << "size:" << image.width() << "x" << image.height();

    // Delegate to in-memory parsing
    return parse(image);
}

Result<QString> QrCodeParser::parse(const QImage &image)
{
    // Check if image is valid
    if (image.isNull()) {
        return Result<QString>::error(i18n("Image is null or invalid"));
    }

    qCDebug(QrCodeParserLog) << "Processing in-memory image"
             << "size:" << image.width() << "x" << image.height()
             << "format:" << image.format();

    // Try with RGB first (better quality)
    const QImage rgbImage = image.convertToFormat(QImage::Format_RGB888);

    // Decode QR code using ZXing-C++
    const ZXing::ImageView zxingImage{
        rgbImage.bits(),
        rgbImage.width(),
        rgbImage.height(),
        ZXing::ImageFormat::RGB,
        static_cast<int>(rgbImage.bytesPerLine())
    };

    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTryHarder(true);
    options.setTryRotate(true);
    options.setTryInvert(true);

    auto result = ZXing::ReadBarcode(zxingImage, options);

    if (!result.isValid()) {
        qCDebug(QrCodeParserLog) << "Failed with RGB, trying grayscale...";

        // Try again with grayscale
        const QImage grayImage = image.convertToFormat(QImage::Format_Grayscale8);
        const ZXing::ImageView grayZxingImage{
            grayImage.bits(),
            grayImage.width(),
            grayImage.height(),
            ZXing::ImageFormat::Lum,
            static_cast<int>(grayImage.bytesPerLine())
        };

        result = ZXing::ReadBarcode(grayZxingImage, options);

        if (!result.isValid()) {
            return Result<QString>::error(i18n("No QR code found in image or failed to decode"));
        }
    }

    const QString decodedText = QString::fromStdString(result.text());

    qCDebug(QrCodeParserLog) << "Successfully decoded QR code, length:" << decodedText.length();

    return Result<QString>::success(decodedText);
}

} // namespace Daemon
} // namespace YubiKeyOath
