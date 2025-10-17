/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "match_builder.h"
#include "../config/configuration_provider.h"
#include "../formatting/credential_formatter.h"
#include "../formatting/display_strategies/flexible_display_strategy.h"
#include "../../shared/dbus/yubikey_dbus_client.h"
#include "../logging_categories.h"

#include <KLocalizedString>
#include <QDebug>
#include <QMap>

namespace KRunner {
namespace YubiKey {

MatchBuilder::MatchBuilder(KRunner::AbstractRunner *runner,
                          const ConfigurationProvider *config,
                          const KRunner::Actions &actions)
    : m_runner(runner)
    , m_config(config)
    , m_actions(actions)
{
}

KRunner::QueryMatch MatchBuilder::buildCredentialMatch(const CredentialInfo &credential,
                                                       const QString &query,
                                                       YubiKeyDBusClient *dbusClient)
{
    qCDebug(MatchBuilderLog) << "Building match for credential:" << credential.name;

    KRunner::QueryMatch match(m_runner);

    // Get display preferences from config
    bool showUsername = m_config->showUsername();
    bool showCode = m_config->showCode();
    bool showDeviceName = m_config->showDeviceName();
    bool showDeviceOnlyWhenMultiple = m_config->showDeviceNameOnlyWhenMultiple();

    qCDebug(MatchBuilderLog) << "Display preferences - username:" << showUsername
             << "code:" << showCode
             << "deviceName:" << showDeviceName
             << "onlyWhenMultiple:" << showDeviceOnlyWhenMultiple;

    // Get device information
    QList<DeviceInfo> devices = dbusClient->listDevices();
    QMap<QString, QString> deviceIdToName;
    int connectedDeviceCount = 0;

    for (const auto &device : devices) {
        deviceIdToName[device.deviceId] = device.deviceName;
        if (device.isConnected) {
            connectedDeviceCount++;
        }
    }

    qCDebug(MatchBuilderLog) << "Found" << devices.size() << "devices,"
             << connectedDeviceCount << "connected";

    // Prepare match data
    QStringList data;
    QString credentialName = credential.name;
    QString displayName;
    QString code;
    QString requiresTouch = credential.requiresTouch ? QStringLiteral("true") : QStringLiteral("false");
    QString isPasswordError = QStringLiteral("false");

    // Generate code if requested and credential doesn't require touch
    if (showCode && !credential.requiresTouch) {
        qCDebug(MatchBuilderLog) << "Generating code for non-touch credential:" << credential.name
                 << "on device:" << credential.deviceId;
        GenerateCodeResult codeResult = dbusClient->generateCode(credential.deviceId, credential.name);
        if (!codeResult.code.isEmpty()) {
            code = codeResult.code;
            qCDebug(MatchBuilderLog) << "Generated code:" << code;
        } else {
            qCDebug(MatchBuilderLog) << "Failed to generate code via D-Bus";
        }
    }

    // Get device name for this credential
    QString deviceName = deviceIdToName.value(credential.deviceId, QString());
    qCDebug(MatchBuilderLog) << "Device name for credential:" << deviceName;

    // Format display name using FlexibleDisplayStrategy
    if (showCode && credential.requiresTouch) {
        // Special case: show touch indicator when code display is enabled
        OathCredential tempCred;
        tempCred.name = credential.name;
        tempCred.issuer = credential.issuer;
        tempCred.username = credential.username;
        tempCred.requiresTouch = credential.requiresTouch;

        displayName = FlexibleDisplayStrategy::formatWithCode(
            tempCred,
            code,
            credential.requiresTouch,
            showUsername,
            showCode,
            showDeviceName,
            deviceName,
            connectedDeviceCount,
            showDeviceOnlyWhenMultiple);
    } else {
        // Standard formatting
        displayName = CredentialFormatter::formatDisplayName(
            credential,
            showUsername,
            showCode,
            showDeviceName,
            deviceName,
            connectedDeviceCount,
            showDeviceOnlyWhenMultiple);
    }

    qCDebug(MatchBuilderLog) << "Formatted displayName:" << displayName;

    // Set match data (index: 0=name, 1=display, 2=code, 3=touch, 4=pwdError, 5=deviceId)
    data << credentialName << displayName << code << requiresTouch << isPasswordError << credential.deviceId;
    match.setData(data);
    match.setText(displayName);
    match.setSubtext(i18n("YubiKey OATH TOTP/HOTP"));
    match.setIconName(QStringLiteral(":/icons/yubikey.svg"));
    match.setId(QStringLiteral("yubikey_") + credential.name);

    // Calculate and set relevance
    qreal relevance = calculateRelevance(credential, query);
    qCDebug(MatchBuilderLog) << "Match relevance:" << relevance;

    match.setRelevance(relevance);
    match.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::Highest);
    match.setActions(m_actions);

    qCDebug(MatchBuilderLog) << "Match built successfully with" << m_actions.size() << "actions";

    return match;
}

KRunner::QueryMatch MatchBuilder::buildPasswordErrorMatch(const DeviceInfo &device)
{
    qCDebug(MatchBuilderLog) << "Building password error match for device:"
                              << device.deviceId << device.deviceName;

    KRunner::QueryMatch match(m_runner);
    QStringList data;
    // Format: [credentialName, displayName, code, requiresTouch, isPasswordError, deviceId]
    data << QString() << QString() << QString()
         << QStringLiteral("false") << QStringLiteral("true") << device.deviceId;

    // Show device name and short device ID
    QString shortId = device.deviceId.left(8);
    if (device.deviceId.length() > 8) {
        shortId += QStringLiteral("...");
    }

    QString displayMessage = i18n("YubiKey password required: %1", device.deviceName);
    QString subtext = i18n("Device: %1 - Click to enter password", shortId);

    match.setData(data);
    match.setText(displayMessage);
    match.setSubtext(subtext);
    match.setIconName(QStringLiteral(":/icons/yubikey.svg"));
    match.setId(QStringLiteral("yubikey_password_error_") + device.deviceId);
    match.setRelevance(1.0);  // Highest priority
    match.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::Highest);

    qCDebug(MatchBuilderLog) << "Password error match built for" << device.deviceName;

    return match;
}

qreal MatchBuilder::calculateRelevance(const CredentialInfo &credential, const QString &query) const
{
    QString name = credential.name.toLower();
    QString issuer = credential.issuer.toLower();
    QString username = credential.username.toLower();
    QString lowerQuery = query.toLower();

    qCDebug(MatchBuilderLog) << "Calculating relevance - name:" << name
             << "issuer:" << issuer
             << "username:" << username
             << "query:" << lowerQuery;

    // Empty query should return default relevance
    if (lowerQuery.isEmpty()) {
        return 0.5;
    }

    if (name.startsWith(lowerQuery)) {
        return 1.0;
    } else if (issuer.startsWith(lowerQuery)) {
        return 0.9;
    } else if (username.startsWith(lowerQuery)) {
        return 0.8;
    } else if (name.contains(lowerQuery)) {
        return 0.7;
    }

    return 0.5;
}

} // namespace YubiKey
} // namespace KRunner
