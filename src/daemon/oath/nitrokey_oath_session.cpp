/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "nitrokey_oath_session.h"
#include "../logging_categories.h"
#include "../utils/secure_logging.h"
#include <QDateTime>

namespace YubiKeyOath {
namespace Daemon {
using namespace YubiKeyOath::Shared;

NitrokeyOathSession::NitrokeyOathSession(SCARDHANDLE cardHandle,
                                         DWORD protocol,
                                         const QString &deviceId,
                                         QObject *parent)
    : YkOathSession(cardHandle, protocol, deviceId, parent)
{
    // Override with Nitrokey-specific protocol implementation
    m_oathProtocol = std::make_unique<NitrokeySecretsOathProtocol>();
    qCDebug(YubiKeyOathDeviceLog) << "NitrokeyOathSession created for device" << deviceId;
}

NitrokeyOathSession::~NitrokeyOathSession()
{
    qCDebug(YubiKeyOathDeviceLog) << "NitrokeyOathSession destroyed for device" << m_deviceId;
}

Result<QString> NitrokeyOathSession::calculateCode(const QString &name, int period)
{
    qCDebug(YubiKeyOathDeviceLog) << "calculateCode() (Nitrokey) for" << name << "on device" << m_deviceId
                                   << "with period" << period;

    // Create challenge from current time with specified period
    const QByteArray challenge = OathProtocol::createTotpChallenge(period);

    // Use Nitrokey-specific CALCULATE command with Le byte (CCID Case 4)
    const QByteArray command = NitrokeySecretsOathProtocol::createCalculateCommand(name, challenge);
    const QByteArray response = sendApdu(command);

    if (response.isEmpty()) {
        qCDebug(YubiKeyOathDeviceLog) << "Empty response from CALCULATE";
        return Result<QString>::error(tr("Failed to communicate with device"));
    }

    // Check status word
    const quint16 sw = OathProtocol::getStatusWord(response);

    // Nitrokey-specific: Check for touch required (SecurityStatusNotSatisfied instead of ConditionsNotSatisfied)
    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Touch required (SW=6982, Nitrokey-specific)";
        Q_EMIT touchRequired();
        return Result<QString>::error(tr("Touch required"));
    }

    // Check for authentication required (also 0x6982, but happens when not authenticated)
    // This is ambiguous with touch requirement, but context differs:
    // - Touch required: happens during CALCULATE for touch-enabled credential
    // - Auth required: happens when device requires password authentication
    // We handle touch first (more specific), then auth (general case)
    if (sw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Password or touch required for CALCULATE (SW=6982)";
        return Result<QString>::error(tr("Password or touch required"));
    }

    // Parse code
    const QString code = m_oathProtocol->parseCode(response);
    if (code.isEmpty()) {
        return Result<QString>::error(tr("Failed to parse TOTP code from response"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "Code generated successfully";
    return Result<QString>::success(code);
}

Result<QList<OathCredential>> NitrokeyOathSession::calculateAll()
{
    qCDebug(YubiKeyOathDeviceLog) << "calculateAll() (Nitrokey) for device" << m_deviceId;

    // Nitrokey Strategy: Use LIST v1 (includes touch_required flag in properties byte)
    // (CALCULATE_ALL not supported on Nitrokey)
    qCDebug(YubiKeyOathDeviceLog) << "Using LIST v1 strategy (Nitrokey-specific)";

    // Send LIST v1 command
    const QByteArray listCommand = NitrokeySecretsOathProtocol::createListCommandV1();

    qCDebug(YubiKeyOathDeviceLog) << "Sending LIST v1 command";

    const QByteArray listResponse = sendApdu(listCommand);

    if (listResponse.isEmpty()) {
        qCWarning(YubiKeyOathDeviceLog) << "Empty response from LIST v1";
        return Result<QList<OathCredential>>::error(tr("Failed to list credentials"));
    }

    qCDebug(YubiKeyOathDeviceLog) << "LIST v1 response received:" << listResponse.length() << "bytes";

    const quint16 listSw = OathProtocol::getStatusWord(listResponse);

    // Check for session loss (applet not selected)
    // NOTE: With CardTransaction doing SELECT before each operation, this should not happen
    if (listSw == OathProtocol::SW_INS_NOT_SUPPORTED || listSw == OathProtocol::SW_CLA_NOT_SUPPORTED) {
        qCWarning(YubiKeyOathDeviceLog) << "Session lost (SW=" << QString::number(listSw, 16) << ")";
        return Result<QList<OathCredential>>::error(tr("Session lost"));
    }

    // Check for authentication requirement
    if (listSw == OathProtocol::SW_SECURITY_STATUS_NOT_SATISFIED) {
        qCDebug(YubiKeyOathDeviceLog) << "Password required for LIST";
        return Result<QList<OathCredential>>::error(tr("Password required"));
    }

    // Check for LIST v1 not supported (fallback to standard LIST)
    if (listSw == OathProtocol::SW_CONDITIONS_NOT_SATISFIED) {
        qCInfo(YubiKeyOathDeviceLog) << "LIST v1 not supported (SW=6985), falling back to standard LIST";

        // Fallback: Use standard LIST (Nitrokey CCID requires Le byte)
        const QByteArray stdListCommand = NitrokeySecretsOathProtocol::createListCommand();
        qCDebug(YubiKeyOathDeviceLog) << "Sending standard LIST command";

        const QByteArray stdListResponse = sendApdu(stdListCommand);

        if (stdListResponse.isEmpty()) {
            qCWarning(YubiKeyOathDeviceLog) << "Standard LIST failed: empty response";
            return Result<QList<OathCredential>>::error(tr("Failed to list credentials"));
        }

        qCDebug(YubiKeyOathDeviceLog) << "Standard LIST response received:" << stdListResponse.length() << "bytes";

        const quint16 stdListSw = OathProtocol::getStatusWord(stdListResponse);

        if (stdListSw != 0x9000) {
            qCWarning(YubiKeyOathDeviceLog) << "Standard LIST failed: SW=" << QString::number(stdListSw, 16);
            return Result<QList<OathCredential>>::error(tr("Failed to list credentials (SW: %1)").arg(QString::number(stdListSw, 16)));
        }

        // Parse with standard parser (no properties byte)
        QList<OathCredential> stdCredentials = m_oathProtocol->parseCredentialList(stdListResponse);

        // Set device ID for all credentials
        for (auto &cred : stdCredentials) {
            cred.deviceId = m_deviceId;
            // Note: touch_required flag NOT available in standard LIST
            cred.requiresTouch = false;
        }

        qCInfo(YubiKeyOathDeviceLog) << "Listed" << stdCredentials.size() << "credentials via standard LIST (no touch flags)";

        // Standard LIST doesn't provide touch flags - return credentials without codes
        // If a credential requires touch, error will be detected on-demand via generateCode()
        // This prevents blocking on touch-required credentials during initialization
        return Result<QList<OathCredential>>::success(stdCredentials);
    }

    // Check for success
    if (listSw != 0x9000) {
        qCWarning(YubiKeyOathDeviceLog) << "LIST v1 failed: SW=" << QString::number(listSw, 16);
        return Result<QList<OathCredential>>::error(tr("Failed to list credentials (SW: %1)").arg(QString::number(listSw, 16)));
    }

    // Parse credentials from LIST v1 response
    QList<OathCredential> credentials = NitrokeySecretsOathProtocol::parseCredentialListV1(listResponse);

    // Set device ID for all credentials
    for (auto &cred : credentials) {
        cred.deviceId = m_deviceId;
    }

    qCDebug(YubiKeyOathDeviceLog) << "Listed" << credentials.size() << "credentials via LIST v1";

    // Debug: Log each credential with touch flag
    for (const auto& cred : credentials) {
        qCDebug(YubiKeyOathDeviceLog) << "  Credential:" << cred.originalName
                                       << "touch=" << cred.requiresTouch
                                       << "type=" << (cred.isTotp ? "TOTP" : "HOTP")
                                       << "period=" << cred.period;
    }

    // Nitrokey LIST v1 already provides all metadata including requiresTouch flag
    // No need to calculate codes here - codes will be generated on-demand via generateCode()
    // This prevents blocking on touch-required credentials during initialization
    qCInfo(YubiKeyOathDeviceLog) << "LIST v1 returned" << credentials.size()
                                 << "credentials with metadata (codes generated on-demand)";
    return Result<QList<OathCredential>>::success(credentials);
}

} // namespace Daemon
} // namespace YubiKeyOath
