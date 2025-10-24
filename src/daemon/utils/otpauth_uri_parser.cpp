/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "otpauth_uri_parser.h"
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

Result<OathCredentialData> OtpauthUriParser::parse(const QString &uri)
{
    // Parse URI
    QUrl url(uri);

    if (!url.isValid()) {
        return Result<OathCredentialData>::error(i18n("Invalid URI format"));
    }

    // Check scheme
    if (url.scheme().toLower() != QStringLiteral("otpauth")) {
        return Result<OathCredentialData>::error(i18n("URI must start with otpauth://"));
    }

    // Parse type (host in URL)
    QString type = url.host().toLower();
    if (type != QStringLiteral("totp") && type != QStringLiteral("hotp")) {
        return Result<OathCredentialData>::error(i18n("Type must be 'totp' or 'hotp'"));
    }

    // Parse label from path (remove leading /)
    QString label = url.path();
    if (label.startsWith(QLatin1Char('/'))) {
        label = label.mid(1);
    }

    // URL decode label
    label = QUrl::fromPercentEncoding(label.toUtf8());

    if (label.isEmpty()) {
        return Result<OathCredentialData>::error(i18n("Label (account name) is required"));
    }

    // Parse query parameters
    QUrlQuery query(url);

    // Extract secret (required)
    QString secret = query.queryItemValue(QStringLiteral("secret"));
    if (secret.isEmpty()) {
        return Result<OathCredentialData>::error(i18n("Secret parameter is required"));
    }

    // Build credential data
    OathCredentialData data;
    data.secret = secret;
    data.type = (type == QStringLiteral("totp")) ? OathType::TOTP : OathType::HOTP;

    // Parse label into issuer and account
    // Format: "issuer:account" or just "account"
    int colonPos = label.indexOf(QLatin1Char(':'));
    if (colonPos >= 0) {
        data.issuer = label.left(colonPos);
        data.account = label.mid(colonPos + 1);
    } else {
        data.issuer = label;
        data.account = label;
    }

    // Override issuer if explicitly provided in query
    if (query.hasQueryItem(QStringLiteral("issuer"))) {
        QString issuerParam = query.queryItemValue(QStringLiteral("issuer"));
        if (!issuerParam.isEmpty()) {
            data.issuer = issuerParam;
        }
    }

    // Build full name
    data.name = data.issuer.isEmpty() ? data.account
                                      : data.issuer + QStringLiteral(":") + data.account;

    // Parse algorithm (optional, default SHA1)
    if (query.hasQueryItem(QStringLiteral("algorithm"))) {
        QString algo = query.queryItemValue(QStringLiteral("algorithm")).toUpper();
        if (algo == QStringLiteral("SHA1")) {
            data.algorithm = OathAlgorithm::SHA1;
        } else if (algo == QStringLiteral("SHA256")) {
            data.algorithm = OathAlgorithm::SHA256;
        } else if (algo == QStringLiteral("SHA512")) {
            data.algorithm = OathAlgorithm::SHA512;
        } else {
            return Result<OathCredentialData>::error(
                i18n("Invalid algorithm (must be SHA1, SHA256, or SHA512)"));
        }
    } else {
        data.algorithm = OathAlgorithm::SHA1; // Default
    }

    // Parse digits (optional, default 6)
    if (query.hasQueryItem(QStringLiteral("digits"))) {
        bool ok;
        int digits = query.queryItemValue(QStringLiteral("digits")).toInt(&ok);
        if (!ok || digits < 6 || digits > 8) {
            return Result<OathCredentialData>::error(
                i18n("Invalid digits (must be 6, 7, or 8)"));
        }
        data.digits = digits;
    } else {
        data.digits = 6; // Default
    }

    // Parse period for TOTP (optional, default 30)
    if (data.type == OathType::TOTP) {
        if (query.hasQueryItem(QStringLiteral("period"))) {
            bool ok;
            int period = query.queryItemValue(QStringLiteral("period")).toInt(&ok);
            if (!ok || period <= 0) {
                return Result<OathCredentialData>::error(
                    i18n("Invalid period (must be positive integer)"));
            }
            data.period = period;
        } else {
            data.period = 30; // Default
        }
    }

    // Parse counter for HOTP (required)
    if (data.type == OathType::HOTP) {
        if (!query.hasQueryItem(QStringLiteral("counter"))) {
            return Result<OathCredentialData>::error(
                i18n("Counter parameter is required for HOTP"));
        }
        bool ok;
        quint32 counter = query.queryItemValue(QStringLiteral("counter")).toUInt(&ok);
        if (!ok) {
            return Result<OathCredentialData>::error(
                i18n("Invalid counter value"));
        }
        data.counter = counter;
    }

    // Validate final data
    QString validationError = data.validate();
    if (!validationError.isEmpty()) {
        return Result<OathCredentialData>::error(validationError);
    }

    return Result<OathCredentialData>::success(data);
}

} // namespace YubiKey
} // namespace KRunner
