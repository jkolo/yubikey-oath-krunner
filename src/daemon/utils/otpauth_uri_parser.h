/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>
#include "types/oath_credential_data.h"
#include "common/result.h"

namespace YubiKeyOath {
namespace Daemon {
using Shared::Result;
using Shared::OathCredentialData;

/**
 * @brief Parser for otpauth:// URI format
 *
 * Parses URIs in the format:
 *   otpauth://TYPE/LABEL?secret=SECRET&issuer=ISSUER&algorithm=ALGO&digits=N&period=P&counter=C
 *
 * Where:
 * - TYPE: totp | hotp
 * - LABEL: issuer:account (issuer part is optional)
 * - secret: Base32-encoded secret (required)
 * - issuer: Service name (optional, overrides issuer from LABEL)
 * - algorithm: SHA1 | SHA256 | SHA512 (default: SHA1)
 * - digits: 6 | 7 | 8 (default: 6)
 * - period: Seconds for TOTP validity (default: 30)
 * - counter: Initial counter for HOTP (required for HOTP)
 *
 * Spec: https://github.com/google/google-authenticator/wiki/Key-Uri-Format
 */
class OtpauthUriParser
{
public:
    /**
     * @brief Parses otpauth:// URI into OathCredentialData
     * @param uri otpauth:// URI string
     * @return Result with OathCredentialData on success, error message on failure
     *
     * Example URIs:
     * - otpauth://totp/Example:user@example.com?secret=JBSWY3DPEHPK3PXP&issuer=Example
     * - otpauth://hotp/ACME:john@example.com?secret=HXDMVJECJJWSRB3HWIZR4IFUGFTMXBOZ&issuer=ACME&counter=0
     */
    static Result<OathCredentialData> parse(const QString &uri);

private:
    OtpauthUriParser() = delete; // Utility class - no instances
};

} // namespace Daemon
} // namespace YubiKeyOath
