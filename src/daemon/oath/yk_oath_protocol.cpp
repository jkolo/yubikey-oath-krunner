/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yk_oath_protocol.h"
#include "../logging_categories.h"

#include <QDateTime>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

// =============================================================================
// YubiKey-Specific Response Parsing
// =============================================================================

bool YKOathProtocol::parseSelectResponse(const QByteArray &response,
                                         QString &outDeviceId,
                                         QByteArray &outChallenge,
                                         Version &outFirmwareVersion,
                                         bool &outRequiresPassword,
                                         quint32 &outSerialNumber) const
{
    if (response.length() < 2) {
        return false;
    }

    // Check status word
    quint16 const sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        return false;
    }

    // Parse TLV data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    // Look for TAG_NAME_SALT (0x71), TAG_CHALLENGE (0x74), TAG_VERSION (0x79), and TAG_SERIAL_NUMBER (0x8F)
    QByteArray nameSalt;
    QByteArray serialNumber;
    bool hasChallengeTag = false;

    // Initialize output parameters
    outRequiresPassword = false;
    outSerialNumber = 0;

    int pos = 0;
    while (pos < data.length() - 1) {
        if (pos + 2 > data.length()) { break;
}

        quint8 const tag = data[pos];
        quint8 const len = data[pos + 1];

        if (pos + 2 + len > data.length()) { break;
}

        if (tag == TAG_NAME_SALT) {
            // Extract name/salt for device ID (fallback if no serial)
            nameSalt = data.mid(pos + 2, len);
        } else if (tag == TAG_CHALLENGE) {
            // Extract challenge - presence indicates password is required
            outChallenge = data.mid(pos + 2, len);
            hasChallengeTag = true;
        } else if (tag == TAG_VERSION) {
            // Extract firmware version (3 bytes: major, minor, patch)
            if (len == 3) {
                int const major = static_cast<quint8>(data[pos + 2]);
                int const minor = static_cast<quint8>(data[pos + 3]);
                int const patch = static_cast<quint8>(data[pos + 4]);
                outFirmwareVersion = Version(major, minor, patch);
            }
        } else if (tag == TAG_SERIAL_NUMBER) {
            // Extract serial number (4 bytes, big-endian) - Nitrokey 3
            // YubiKey does NOT send this tag - uses Management API instead
            if (len == 4) {
                serialNumber = data.mid(pos + 2, len);
                outSerialNumber = (static_cast<quint8>(serialNumber[0]) << 24) |
                                 (static_cast<quint8>(serialNumber[1]) << 16) |
                                 (static_cast<quint8>(serialNumber[2]) << 8) |
                                 (static_cast<quint8>(serialNumber[3]));
            }
        }
        // Ignore TAG_ALGORITHM (0x7B) - YubiKey-specific, not needed

        pos += 2 + len;
    }

    // Device ID priority: serial number > name/salt
    if (!serialNumber.isEmpty()) {
        // Nitrokey: 4 bytes = 8 hex chars, pad to 16 for database compatibility
        // YubiKey: doesn't send TAG_SERIAL_NUMBER, uses TAG_NAME instead
        const QString serialHex = QString::fromLatin1(serialNumber.toHex());
        outDeviceId = serialHex.rightJustified(16, QLatin1Char('0'));  // "218a715f" → "00000000218a715f"
    } else if (!nameSalt.isEmpty()) {
        outDeviceId = QString::fromLatin1(nameSalt.toHex());
    }

    // Password requirement detection: TAG_CHALLENGE presence
    outRequiresPassword = hasChallengeTag;

    return !outDeviceId.isEmpty();
}

QString YKOathProtocol::parseCode(const QByteArray &response) const
{
    if (response.length() < 2) {
        return {};
    }

    // Check status word
    quint16 const sw = getStatusWord(response);

    // YubiKey-specific: touch required (vs Nitrokey SW_SECURITY_STATUS_NOT_SATISFIED)
    if (sw == SW_CONDITIONS_NOT_SATISFIED) {
        return {}; // Caller should detect this via status word
    }

    if (!isSuccess(sw)) {
        return {};
    }

    // Parse TLV data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    int i = 0;
    while (i < data.length()) {
        if (i + 2 > data.length()) { break;
}

        quint8 const tag = data[i++];
        quint8 const length = data[i++];

        if (i + length > data.length()) { break;
}

        if (tag == TAG_TOTP_RESPONSE && length >= 5) { // TAG_TOTP_RESPONSE = 0x76
            // First byte is number of digits
            quint8 const digits = data[i];

            // Next 4 bytes are the code value (big-endian)
            quint32 codeValue = 0;
            for (int j = 0; j < 4; ++j) {
                codeValue = (codeValue << 8) | static_cast<quint8>(data[i + 1 + j]);
            }

            // Format code with proper digit count
            return formatCode(data.mid(i, length), digits);
        }

        i += length;
    }

    return {};
}

QList<OathCredential> YKOathProtocol::parseCalculateAllResponse(const QByteArray &response) const
{
    QList<OathCredential> credentials;

    if (response.length() < 2) {
        return credentials;
    }

    // Check status word
    quint16 const sw = getStatusWord(response);
    if (!isSuccess(sw)) {
        return credentials;
    }

    // Parse TLV data (excluding status word)
    QByteArray data = response.left(response.length() - 2);

    // YubiKey CALCULATE ALL response format: NAME (0x71) followed by RESPONSE (0x76) or HOTP (0x77) or TOUCH (0x7c)
    // This is YubiKey-specific format - Nitrokey uses different LIST v1 format
    int i = 0;
    while (i < data.length()) {
        if (i + 2 > data.length()) { break;
}

        quint8 const tag = data[i++];
        quint8 const length = data[i++];

        if (i + length > data.length()) { break;
}

        if (tag == TAG_NAME) { // TAG_NAME = 0x71
            // Parse credential name (no algorithm byte in CALCULATE ALL response)
            QByteArray const nameBytes = data.mid(i, length);
            QString const name = QString::fromUtf8(nameBytes);

            OathCredential cred;
            cred.originalName = name;
            cred.isTotp = true; // Assume TOTP by default (updated below if HOTP)

            // Parse credential ID to extract period, issuer, and account
            int period = 30;
            QString issuer;
            QString account;
            parseCredentialId(name, cred.isTotp, period, issuer, account);

            cred.period = period;
            cred.issuer = issuer;
            cred.account = account;

            credentials.append(cred);
            i += length;

            // Next should be RESPONSE tag (0x76), HOTP (0x77), or TOUCH (0x7c)
            if (i + 2 <= data.length()) {
                quint8 const respTag = data[i++];
                quint8 const respLength = data[i++];

                if (i + respLength > data.length()) { break;
}

                if (respTag == TAG_TOUCH) {
                    // Touch required
                    if (!credentials.isEmpty()) {
                        credentials.last().requiresTouch = true;
                    }
                } else if (respTag == TAG_HOTP) {
                    // HOTP credential - no response to avoid incrementing counter
                    if (!credentials.isEmpty()) {
                        credentials.last().isTotp = false;
                        credentials.last().type = OathType::HOTP;

                        // Re-parse credential ID with isTotp=false to get period=0
                        int hotpPeriod = 0;
                        QString hotpIssuer;
                        QString hotpAccount;
                        parseCredentialId(credentials.last().originalName, false, hotpPeriod, hotpIssuer, hotpAccount);
                        credentials.last().period = hotpPeriod; // Should be 0 for HOTP
                    }
                } else if (respTag == TAG_TOTP_RESPONSE && respLength >= 5) {
                    // Parse code
                    quint8 const digits = data[i];
                    quint32 codeValue = 0;
                    for (int j = 0; j < 4; ++j) {
                        codeValue = (codeValue << 8) | static_cast<quint8>(data[i + 1 + j]);
                    }

                    QString const code = formatCode(data.mid(i, respLength), digits);

                    if (!credentials.isEmpty()) {
                        credentials.last().code = code;
                        credentials.last().digits = static_cast<int>(digits);
                        credentials.last().type = OathType::TOTP;

                        // Calculate validity using actual period extracted from credential name
                        qint64 const currentTime = QDateTime::currentSecsSinceEpoch();
                        int const credPeriod = credentials.last().period; // Period extracted from name
                        qint64 const timeInPeriod = currentTime % credPeriod;
                        qint64 const validityRemaining = credPeriod - timeInPeriod;
                        credentials.last().validUntil = currentTime + validityRemaining;
                    }
                }

                i += respLength;
            }
        } else {
            // Skip unknown tags
            i += length;
        }
    }

    return credentials;
}

} // namespace Daemon
} // namespace YubiKeyOath
