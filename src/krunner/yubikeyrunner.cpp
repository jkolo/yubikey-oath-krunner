/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikeyrunner.h"
#include "ui/password_dialog_helper.h"
#include "logging_categories.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <QDebug>

namespace KRunner {
namespace YubiKey {

YubiKeyRunner::YubiKeyRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
    , m_dbusClient(std::make_unique<YubiKeyDBusClient>(this))
{
    qCDebug(YubiKeyRunnerLog) << "Constructor called - thin D-Bus client mode";

    // Set translation domain for i18n
    KLocalizedString::setApplicationDomain("krunner_yubikey");

    setObjectName(QStringLiteral("yubikey-oath"));

    // Create configuration provider
    m_config = std::make_unique<KRunnerConfiguration>(
        [this]() { return this->config(); },
        this  // parent for QObject
    );

    // Create runner components
    m_actionManager = std::make_unique<ActionManager>();

    // Setup actions first, before creating MatchBuilder
    setupActions();

    m_matchBuilder = std::make_unique<MatchBuilder>(
        this,
        m_config.get(),
        m_actions
    );

    // Connect D-Bus client signals
    connect(m_dbusClient.get(), &YubiKeyDBusClient::deviceConnected,
            this, &YubiKeyRunner::onDeviceConnected);
    connect(m_dbusClient.get(), &YubiKeyDBusClient::deviceDisconnected,
            this, &YubiKeyRunner::onDeviceDisconnected);
    connect(m_dbusClient.get(), &YubiKeyDBusClient::credentialsUpdated,
            this, &YubiKeyRunner::onCredentialsUpdated);
    connect(m_dbusClient.get(), &YubiKeyDBusClient::daemonUnavailable,
            this, &YubiKeyRunner::onDaemonUnavailable);

    qCDebug(YubiKeyRunnerLog) << "Constructor finished";
}

YubiKeyRunner::~YubiKeyRunner() = default;

void YubiKeyRunner::init()
{
    qCDebug(YubiKeyRunnerLog) << "init() called";
    reloadConfiguration();

    // Check if daemon is available
    if (m_dbusClient->isDaemonAvailable()) {
        qCDebug(YubiKeyRunnerLog) << "YubiKey D-Bus daemon is available";
    } else {
        qCDebug(YubiKeyRunnerLog) << "YubiKey D-Bus daemon not available - will auto-start on first use";
    }
}

void YubiKeyRunner::setupActions()
{
    m_actions.clear();

    // Get primary action from configuration
    QString primary = m_config->primaryAction();
    qCDebug(YubiKeyRunnerLog) << "setupActions() - primary action:" << primary;

    // Add only the alternative action as a button
    // Primary action is triggered by Enter (without action ID)
    // Alternative action is triggered by clicking the button
    if (primary == QStringLiteral("copy")) {
        // Copy is primary (Enter without action), Type is the button
        m_actions.append(KRunner::Action(QStringLiteral("type"),
                                          QStringLiteral("input-keyboard"),
                                          i18n("Type code")));
    } else {
        // Type is primary (Enter without action), Copy is the button
        m_actions.append(KRunner::Action(QStringLiteral("copy"),
                                          QStringLiteral("edit-copy"),
                                          i18n("Copy to clipboard")));
    }

    qCDebug(YubiKeyRunnerLog) << "setupActions() - created" << m_actions.size() << "action(s)";
}

void YubiKeyRunner::match(KRunner::RunnerContext &context)
{
    qCDebug(YubiKeyRunnerLog) << "match() called with query:" << context.query();

    if (!context.isValid() || context.query().length() < 2) {
        qCDebug(YubiKeyRunnerLog) << "Query too short or invalid";
        return;
    }

    if (!m_dbusClient->isDaemonAvailable()) {
        qCDebug(YubiKeyRunnerLog) << "D-Bus daemon not available";
        return;
    }

    const QString query = context.query().toLower();

    // Check for "Add OATH" command
    if (query.contains(QStringLiteral("add")) &&
        (query.contains(QStringLiteral("oath")) || query.contains(QStringLiteral("credential")))) {
        qCDebug(YubiKeyRunnerLog) << "Detected 'Add OATH' command";

        KRunner::QueryMatch match(this);
        match.setId(QStringLiteral("add-oath-credential"));
        match.setText(i18n("Add OATH Credential to YubiKey"));
        match.setSubtext(i18n("Capture QR code and add credential"));
        match.setIconName(QStringLiteral("list-add"));
        match.setRelevance(1.0);

        context.addMatch(match);
    }

    // Get all devices to check their password status
    QList<DeviceInfo> devices = m_dbusClient->listDevices();
    qCDebug(YubiKeyRunnerLog) << "Found" << devices.size() << "known devices";

    // For each CONNECTED device that needs password, show password error match
    for (const auto &device : devices) {
        if (device.isConnected &&
            device.requiresPassword &&
            !device.hasValidPassword) {
            qCDebug(YubiKeyRunnerLog) << "Device requires password:"
                                      << device.deviceName << device.deviceId;
            KRunner::QueryMatch match = m_matchBuilder->buildPasswordErrorMatch(device);
            context.addMatch(match);
            // DON'T return - continue to show credentials from other devices!
        }
    }

    // Get credentials from ALL devices (daemon aggregates them)
    QList<CredentialInfo> credentials = m_dbusClient->getCredentials(QString());
    qCDebug(YubiKeyRunnerLog) << "Found" << credentials.size() << "total credentials";

    if (credentials.isEmpty()) {
        qCDebug(YubiKeyRunnerLog) << "No credentials available from any device";
        return;
    }

    // Build matches for matching credentials from all working devices
    int matchCount = 0;
    for (const auto &credential : credentials) {
        QString name = credential.name.toLower();
        QString issuer = credential.issuer.toLower();
        QString username = credential.username.toLower();

        if (name.contains(query) || issuer.contains(query) || username.contains(query)) {
            qCDebug(YubiKeyRunnerLog) << "Creating match for credential:" << credential.name;
            KRunner::QueryMatch match = m_matchBuilder->buildCredentialMatch(
                credential, query, m_dbusClient.get());
            context.addMatch(match);
            matchCount++;
        }
    }

    qCDebug(YubiKeyRunnerLog) << "Total credential matches:" << matchCount;
}

void YubiKeyRunner::run(const KRunner::RunnerContext &context, const KRunner::QueryMatch &match)
{
    Q_UNUSED(context)
    qCDebug(YubiKeyRunnerLog) << "run() called with match ID:" << match.id();

    // Handle "Add OATH Credential" command
    if (match.id() == QStringLiteral("add-oath-credential")) {
        qCDebug(YubiKeyRunnerLog) << "Starting Add OATH Credential workflow via daemon";

        // Delegate to daemon - it handles the entire workflow
        QVariantMap result = m_dbusClient->addCredentialFromScreen();

        if (!result.value(QStringLiteral("success"), false).toBool()) {
            qCWarning(YubiKeyRunnerLog) << "Add credential workflow failed:"
                                        << result.value(QStringLiteral("error")).toString();
        }

        return;
    }

    QStringList data = match.data().toStringList();
    if (data.size() < 5) {
        qCDebug(YubiKeyRunnerLog) << "Invalid match data";
        return;
    }

    QString credentialName = data.at(0);
    QString displayName = data.at(1);
    Q_UNUSED(displayName)
    QString code = data.at(2);
    Q_UNUSED(code)
    bool requiresTouch = data.at(3) == QStringLiteral("true");
    Q_UNUSED(requiresTouch)
    bool isPasswordError = data.at(4) == QStringLiteral("true");
    QString deviceId = data.size() > 5 ? data.at(5) : QString();

    qCDebug(YubiKeyRunnerLog) << "credentialName:" << credentialName
             << "deviceId:" << deviceId
             << "isPasswordError:" << isPasswordError;

    // Handle password error match
    if (isPasswordError) {
        qCDebug(YubiKeyRunnerLog) << "Showing password dialog for authentication error";

        // Use device ID from match data
        if (deviceId.isEmpty()) {
            qCDebug(YubiKeyRunnerLog) << "No device ID in match data";
            return;
        }

        qCDebug(YubiKeyRunnerLog) << "Requesting password for device:" << deviceId;

        // Get device name from D-Bus
        QString deviceName = m_dbusClient->getDeviceName(deviceId);

        // Show password dialog (non-modal, with retry on error)
        showPasswordDialog(deviceId, deviceName);

        return;
    }

    if (credentialName.isEmpty()) {
        qCDebug(YubiKeyRunnerLog) << "Empty credential name";
        return;
    }

    // Determine which action to execute using ActionManager
    QString primaryAction = m_config->primaryAction();
    QString actionId = m_actionManager->determineAction(match, primaryAction);

    qCDebug(YubiKeyRunnerLog) << "Action selection - primary from config:" << primaryAction
             << "determined action:" << actionId
             << "action name:" << m_actionManager->getActionName(actionId);

    // Execute action via daemon D-Bus methods
    bool success = false;
    if (actionId == QStringLiteral("type")) {
        qCDebug(YubiKeyRunnerLog) << "Delegating type action to daemon";
        success = m_dbusClient->typeCode(deviceId, credentialName);
    } else {  // copy
        qCDebug(YubiKeyRunnerLog) << "Delegating copy action to daemon";
        success = m_dbusClient->copyCodeToClipboard(deviceId, credentialName);
    }

    if (!success) {
        qCWarning(YubiKeyRunnerLog) << "Action failed:" << actionId;
    } else {
        qCDebug(YubiKeyRunnerLog) << "Action completed successfully:" << actionId;
    }
}

void YubiKeyRunner::showPasswordDialog(const QString &deviceId, const QString &deviceName)
{
    qCDebug(YubiKeyRunnerLog) << "showPasswordDialog() for device:" << deviceId;

    // Use PasswordDialogHelper for non-modal password dialog with retry
    PasswordDialogHelper::showDialog(
        deviceId,
        deviceName,
        m_dbusClient.get(),
        this,
        []() {
            // Password success callback - daemon already saved password
            qCDebug(YubiKeyRunnerLog) << "Password saved successfully";
        }
    );
}

void YubiKeyRunner::reloadConfiguration()
{
    qCDebug(YubiKeyRunnerLog) << "reloadConfiguration() called";

    // Configuration is automatically reloaded via callback
    // Just rebuild actions with potentially new primary action
    setupActions();
}

void YubiKeyRunner::onDeviceConnected(const QString &deviceId)
{
    qCDebug(YubiKeyRunnerLog) << "Device connected:" << deviceId;
}

void YubiKeyRunner::onDeviceDisconnected(const QString &deviceId)
{
    qCDebug(YubiKeyRunnerLog) << "Device disconnected:" << deviceId;
}

void YubiKeyRunner::onCredentialsUpdated(const QString &deviceId)
{
    qCDebug(YubiKeyRunnerLog) << "Credentials updated for device:" << deviceId;
}

void YubiKeyRunner::onDaemonUnavailable()
{
    qCWarning(YubiKeyRunnerLog) << "Daemon became unavailable";
}

K_PLUGIN_CLASS_WITH_JSON(YubiKeyRunner, "yubikeyrunner.json")

} // namespace YubiKey
} // namespace KRunner

#include "yubikeyrunner.moc"
