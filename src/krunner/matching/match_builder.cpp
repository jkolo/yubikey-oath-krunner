/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "match_builder.h"
#include "../config/configuration_provider.h"
#include "formatting/credential_formatter.h"
#include "dbus/oath_manager_proxy.h"
#include "dbus/oath_credential_proxy.h"
#include "dbus/oath_device_proxy.h"
#include "../logging_categories.h"
#include "../shared/utils/yubikey_icon_resolver.h"

#include <KLocalizedString>
#include <QDebug>
#include <QMap>

namespace YubiKeyOath {
namespace Runner {
using namespace YubiKeyOath::Shared;

MatchBuilder::MatchBuilder(KRunner::AbstractRunner *runner,
                          const ConfigurationProvider *config,
                          const KRunner::Actions &actions)
    : m_runner(runner)
    , m_config(config)
    , m_actions(actions)
{
}

KRunner::QueryMatch MatchBuilder::buildCredentialMatch(OathCredentialProxy *credentialProxy,
                                                       const QString &query,
                                                       OathManagerProxy *manager)
{
    if (!credentialProxy) {
        qCWarning(MatchBuilderLog) << "Cannot build match: credential proxy is null";
        return KRunner::QueryMatch(m_runner);
    }

    qCDebug(MatchBuilderLog) << "Building match for credential:" << credentialProxy->fullName();

    KRunner::QueryMatch match(m_runner);

    // Get display preferences from config
    const bool showUsername = m_config->showUsername();
    const bool showCode = m_config->showCode();
    const bool showDeviceName = m_config->showDeviceName();
    const bool showDeviceOnlyWhenMultiple = m_config->showDeviceNameOnlyWhenMultiple();

    qCDebug(MatchBuilderLog) << "Display preferences - username:" << showUsername
             << "code:" << showCode
             << "deviceName:" << showDeviceName
             << "onlyWhenMultiple:" << showDeviceOnlyWhenMultiple;

    // Get device information from manager
    const QList<OathDeviceProxy*> devices = manager->devices();
    QMap<QString, QString> deviceIdToName;
    int connectedDeviceCount = 0;

    for (const auto *device : devices) {
        // Use device ID from D-Bus (serial number or "dev_<hex>") as map key
        // This matches the device ID extracted from credential's object path
        // device->deviceId() returns D-Bus property "ID" which is either:
        // - Serial number as string (e.g., "20252879")
        // - "dev_<hexhash>" for devices without serial number
        deviceIdToName[device->deviceId()] = device->name();
        if (device->isConnected()) {
            connectedDeviceCount++;
        }
    }

    qCDebug(MatchBuilderLog) << "Found" << devices.size() << "devices,"
             << connectedDeviceCount << "connected";
    qCDebug(MatchBuilderLog) << "Device ID to name map:" << deviceIdToName;

    // Prepare match data
    QStringList data;
    const QString credentialName = credentialProxy->fullName();
    QString displayName;
    QString code;
    const QString requiresTouch = credentialProxy->requiresTouch() ? QStringLiteral("true") : QStringLiteral("false");
    const QString isPasswordError = QStringLiteral("false");

    // Generate code if requested and credential doesn't require touch
    if (showCode && !credentialProxy->requiresTouch()) {
        qCDebug(MatchBuilderLog) << "Generating code for non-touch credential:" << credentialProxy->fullName()
                 << "on device:" << credentialProxy->deviceId();
        const GenerateCodeResult codeResult = credentialProxy->generateCode();
        if (!codeResult.code.isEmpty()) {
            code = codeResult.code;
            qCDebug(MatchBuilderLog) << "Generated code:" << code;
        } else {
            qCDebug(MatchBuilderLog) << "Failed to generate code";
        }
    }

    // Get device name for this credential
    // Use parentDeviceId() which extracts the public device ID from object path
    // This matches the device ID in our map (serial number or "dev_<hex>")
    const QString parentDeviceId = credentialProxy->parentDeviceId();
    const QString deviceName = deviceIdToName.value(parentDeviceId, QString());

    qCDebug(MatchBuilderLog) << "Device lookup for credential" << credentialProxy->fullName()
                               << "- parent device ID:" << parentDeviceId
                               << "- found name:" << (deviceName.isEmpty() ? QStringLiteral("(empty)") : deviceName);

    // Prepare OathCredential for formatting
    OathCredential tempCred;
    tempCred.originalName = credentialProxy->fullName();
    tempCred.issuer = credentialProxy->issuer();
    tempCred.account = credentialProxy->username();
    tempCred.requiresTouch = credentialProxy->requiresTouch();

    // Format display name
    if (showCode) {
        // Use formatWithCode() when showCode is enabled
        // This handles both touch-required (shows 👆) and regular credentials (shows code)
        const FormatOptions options = FormatOptionsBuilder()
            .withUsername(showUsername)
            .withCode(showCode)
            .withDevice(deviceName, showDeviceName)
            .withDeviceCount(connectedDeviceCount)
            .onlyWhenMultipleDevices(showDeviceOnlyWhenMultiple)
            .build();
        displayName = CredentialFormatter::formatWithCode(
            tempCred,
            code,
            credentialProxy->requiresTouch(),
            options);
    } else {
        // Standard formatting without code
        const FormatOptions options = FormatOptionsBuilder()
            .withUsername(showUsername)
            .withCode(false) // showCode=false to prevent showing code from credential.code field
            .withDevice(deviceName, showDeviceName)
            .withDeviceCount(connectedDeviceCount)
            .onlyWhenMultipleDevices(showDeviceOnlyWhenMultiple)
            .build();
        displayName = CredentialFormatter::formatDisplayName(tempCred, options);
    }

    qCDebug(MatchBuilderLog) << "Formatted displayName:" << displayName;

    // Use generic OATH icon theme name
    const QString iconName = YubiKeyIconResolver::getGenericIconName();

    // Set match data (index: 0=name, 1=display, 2=code, 3=touch, 4=pwdError, 5=deviceId)
    data << credentialName << displayName << code << requiresTouch << isPasswordError << credentialProxy->deviceId();
    match.setData(data);
    match.setText(displayName);
    match.setSubtext(i18n("YubiKey OATH TOTP/HOTP"));
    match.setIconName(iconName);
    match.setId(QStringLiteral("yubikey_") + credentialProxy->fullName());

    // Convert to CredentialInfo for relevance calculation
    const CredentialInfo credentialInfo = credentialProxy->toCredentialInfo();

    // Calculate and set relevance
    const qreal relevance = calculateRelevance(credentialInfo, query);
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
                              << device._internalDeviceId << device.deviceName;

    KRunner::QueryMatch match(m_runner);
    QStringList data;
    // Format: [credentialName, displayName, code, requiresTouch, isPasswordError, deviceId]
    data << QString() << QString() << QString()
         << QStringLiteral("false") << QStringLiteral("true") << device._internalDeviceId;

    // Show device name and short device ID
    QString shortId = device._internalDeviceId.left(8);
    if (device._internalDeviceId.length() > 8) {
        shortId += QStringLiteral("...");
    }

    const QString displayMessage = i18n("YubiKey password required: %1", device.deviceName);
    const QString subtext = i18n("Device: %1 - Click to enter password", shortId);

    // Use generic OATH icon theme name
    const QString iconName = YubiKeyIconResolver::getGenericIconName();

    match.setData(data);
    match.setText(displayMessage);
    match.setSubtext(subtext);
    match.setIconName(iconName);
    match.setId(QStringLiteral("yubikey_password_error_") + device._internalDeviceId);
    match.setRelevance(1.0);  // Highest priority
    match.setCategoryRelevance(KRunner::QueryMatch::CategoryRelevance::Highest);

    qCDebug(MatchBuilderLog) << "Password error match built for" << device.deviceName
                              << "model:" << device.deviceModel << "icon:" << iconName;

    return match;
}

qreal MatchBuilder::calculateRelevance(const CredentialInfo &credential, const QString &query) const
{
    const QString name = credential.name.toLower();
    const QString issuer = credential.issuer.toLower();
    const QString account = credential.account.toLower();
    const QString lowerQuery = query.toLower();

    qCDebug(MatchBuilderLog) << "Calculating relevance - name:" << name
             << "issuer:" << issuer
             << "account:" << account
             << "query:" << lowerQuery;

    // Empty query should return default relevance
    if (lowerQuery.isEmpty()) {
        return 0.5;
    }

    if (name.startsWith(lowerQuery)) {
        return 1.0;
    } else if (issuer.startsWith(lowerQuery)) {
        return 0.9;
    } else if (account.startsWith(lowerQuery)) {
        return 0.8;
    } else if (name.contains(lowerQuery)) {
        return 0.7;
    }

    return 0.5;
}

} // namespace Runner
} // namespace YubiKeyOath
