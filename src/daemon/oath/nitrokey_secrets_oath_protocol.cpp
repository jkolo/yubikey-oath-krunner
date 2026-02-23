/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "nitrokey_secrets_oath_protocol.h"
#include "../logging_categories.h"

#include <QDateTime>
#include <QDebug>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

// =============================================================================
// Nitrokey-Specific Command Creation
// =============================================================================

QByteArray NitrokeySecretsOathProtocol::createCalculateCommand(const QString &name, const QByteArray &challenge)
{
    QByteArray command;
    command.append(static_cast<char>(CLA));           // CLA = 0x00
    command.append(static_cast<char>(INS_CALCULATE)); // INS = 0xA2
    command.append((char)0x00);                       // P1
    command.append((char)0x01);                       // P2 = Request response

    // Data length: tag + length_byte + data for both NAME and CHALLENGE
    QByteArray const nameBytes = name.toUtf8();
    int const dataLen = static_cast<int>(1 + 1 + nameBytes.length() + 1 + 1 + challenge.length());
    command.append(static_cast<char>(dataLen)); // Lc

    // NAME tag + length + data
    command.append(static_cast<char>(TAG_NAME));
    command.append(static_cast<char>(nameBytes.length()));
    command.append(nameBytes);

    // CHALLENGE tag + length + data
    command.append(static_cast<char>(TAG_CHALLENGE));
    command.append(static_cast<char>(challenge.length()));
    command.append(challenge);

    // Le byte = 0x00 (expect maximum response - CCID Case 4 requirement)
    command.append((char)0x00);

    return command;
}

// =============================================================================
// Nitrokey-Specific Response Parsing
// =============================================================================

bool NitrokeySecretsOathProtocol::parseSelectResponse(const QByteArray &response,
                                                      QString &outDeviceId,
                                                      QByteArray &outChallenge,
                                                      Version &outFirmwareVersion,
                                                      bool &outRequiresPassword,
                                                      quint32 &outSerialNumber) const
{
    // Nitrokey includes TAG_SERIAL_NUMBER (0x8F) in SELECT response
    // Base implementation already handles this correctly
    return OathProtocol::parseSelectResponse(response, outDeviceId, outChallenge,
                                            outFirmwareVersion, outRequiresPassword,
                                            outSerialNumber);
}

QString NitrokeySecretsOathProtocol::parseCode(const QByteArray &response) const
{
    if (response.length() < 2) {
        return {};
    }

    // Check status word
    quint16 const sw = getStatusWord(response);

    // Nitrokey-specific: SecurityStatusNotSatisfied = touch required (vs YubiKey ConditionsNotSatisfied)
    if (sw == SW_SECURITY_STATUS_NOT_SATISFIED) {
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

QList<OathCredential> NitrokeySecretsOathProtocol::parseCalculateAllResponse(const QByteArray &response) const
{
    // Nitrokey 3 may not support CALCULATE_ALL (returns 0x6D00)
    // NitrokeyOathSession should use LIST v1 strategy instead
    // This method exists for compatibility but may not be called
    (void)response;  // Suppress unused parameter warning

    qCWarning(YubiKeyOathDeviceLog) << "Nitrokey parseCalculateAllResponse called - CALCULATE_ALL may not be supported";
    qCWarning(YubiKeyOathDeviceLog) << "Consider using LIST v1 strategy (createListCommandV1 + parseCredentialListV1)";

    // Return empty list - caller should use LIST v1
    return {};
}

// =============================================================================
// Nitrokey-Specific Extensions (LIST v1 Support)
// =============================================================================

QByteArray NitrokeySecretsOathProtocol::createListCommand()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));        // CLA = 0x00
    command.append(static_cast<char>(INS_LIST));   // INS = 0xA1
    command.append((char)0x00);                    // P1
    command.append((char)0x00);                    // P2
    command.append((char)0x00);                    // Le = 0x00 (expect maximum response - CCID Case 2 requirement)

    return command;
}

QByteArray NitrokeySecretsOathProtocol::createListCommandV1()
{
    QByteArray command;
    command.append(static_cast<char>(CLA));        // CLA = 0x00
    command.append(static_cast<char>(INS_LIST));   // INS = 0xA1
    command.append((char)0x00);                    // P1
    command.append((char)0x00);                    // P2
    command.append((char)0x01);                    // Lc = 1 data byte
    command.append((char)0x01);                    // Data = 0x01 (version 1 request)
    command.append((char)0x00);                    // Le = 0x00 (expect maximum response - CCID Case 4 requirement)

    return command;
}

QList<OathCredential> NitrokeySecretsOathProtocol::parseCredentialListV1(const QByteArray &response)
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

    int i = 0;
    while (i < data.length()) {
        if (i + 2 > data.length()) { break;
}

        quint8 const tag = data[i++];
        quint8 const length = data[i++];

        if (i + length > data.length()) { break;
}

        if (tag == TAG_NAME_LIST) { // TAG_NAME_LIST = 0x72
            QByteArray nameData = data.mid(i, length);

            // LIST v1 response format: [type+algo] [label...] [properties_byte]
            // NOTE: properties_byte is at the END (last byte)
            if (nameData.length() >= 3) { // Minimum: type+algo(1) + name(1) + properties(1)
                quint8 const nameAlgo = nameData[0];
                quint8 const properties = nameData[nameData.length() - 1]; // LAST BYTE
                QByteArray const nameBytes = nameData.mid(1, nameData.length() - 2); // Between first and last
                QString const name = QString::fromUtf8(nameBytes);

                OathCredential cred;
                cred.originalName = name;

                // Extract type from UPPER 4 bits of nameAlgo
                // Nitrokey format: upper=Kind (0x10=HOTP, 0x20=TOTP), lower=Algorithm (0x01=SHA1)
                quint8 const kindBits = nameAlgo & 0xF0;
                cred.type = (kindBits == 0x20) ? OathType::TOTP : OathType::HOTP;
                cred.isTotp = (cred.type == OathType::TOTP);

                // Extract algorithm from LOWER 4 bits of nameAlgo
                quint8 const algorithmBits = nameAlgo & 0x0F;
                cred.algorithm = static_cast<OathAlgorithm>(algorithmBits);

                // Extract touch_required from properties byte (Bit 0 = 0x01)
                cred.requiresTouch = (properties & 0x01) != 0;

                // Parse credential ID to extract period, issuer, and account
                int period = 30;
                QString issuer;
                QString account;
                parseCredentialId(name, cred.isTotp, period, issuer, account);

                cred.period = period;
                cred.issuer = issuer;
                cred.account = account;

                credentials.append(cred);
            }
        }

        i += length;
    }

    return credentials;
}

} // namespace Daemon
} // namespace YubiKeyOath
