/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qr_code_parser.h"
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
    QImage image(imagePath);
    if (image.isNull()) {
        return Result<QString>::error(i18n("Failed to load image: %1", imagePath));
    }

    qDebug() << "QrCodeParser: Processing image from file" << imagePath
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

    qDebug() << "QrCodeParser: Processing in-memory image"
             << "size:" << image.width() << "x" << image.height()
             << "format:" << image.format();

    // Try with RGB first (better quality)
    QImage rgbImage = image.convertToFormat(QImage::Format_RGB888);

    // Decode QR code using ZXing-C++
    ZXing::ImageView zxingImage{
        rgbImage.bits(),
        rgbImage.width(),
        rgbImage.height(),
        ZXing::ImageFormat::RGB,
        rgbImage.bytesPerLine()
    };

    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTryHarder(true);
    options.setTryRotate(true);
    options.setTryInvert(true);

    auto result = ZXing::ReadBarcode(zxingImage, options);

    if (!result.isValid()) {
        qDebug() << "QrCodeParser: Failed with RGB, trying grayscale...";

        // Try again with grayscale
        QImage grayImage = image.convertToFormat(QImage::Format_Grayscale8);
        ZXing::ImageView grayZxingImage{
            grayImage.bits(),
            grayImage.width(),
            grayImage.height(),
            ZXing::ImageFormat::Lum,
            grayImage.bytesPerLine()
        };

        result = ZXing::ReadBarcode(grayZxingImage, options);

        if (!result.isValid()) {
            return Result<QString>::error(i18n("No QR code found in image or failed to decode"));
        }
    }

    QString decodedText = QString::fromStdString(result.text());

    qDebug() << "QrCodeParser: Successfully decoded QR code, length:" << decodedText.length();

    return Result<QString>::success(decodedText);
}

} // namespace Daemon
} // namespace YubiKeyOath
