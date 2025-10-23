/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikeyrunner.h"
#include "input/text_input_factory.h"
#include "workflows/notification_helper.h"
#include "../shared/utils/deferred_execution.h"
#include "../shared/ui/password_dialog_helper.h"
#include "logging_categories.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <QDebug>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>

namespace KRunner {
namespace YubiKey {

YubiKeyRunner::YubiKeyRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
    , m_dbusClient(std::make_unique<YubiKeyDBusClient>(this))
    , m_notificationManager(std::make_unique<DBusNotificationManager>(this))
    , m_clipboardManager(std::make_unique<ClipboardManager>(this))
    , m_textInput(TextInputFactory::createProvider(this))
    , m_touchHandler(std::make_unique<TouchHandler>(this))
{
    qCDebug(YubiKeyRunnerLog) << "Constructor called";
    setObjectName(QStringLiteral("yubikey-oath"));

    // Create configuration provider
    m_config = std::make_unique<KRunnerConfiguration>(
        [this]() { return this->config(); },
        this  // parent for QObject
    );

    // Create runner components
    // Note: NotificationOrchestrator must be created before ActionExecutor
    // because ActionExecutor needs it for modifier key notifications
    m_notificationOrchestrator = std::make_unique<NotificationOrchestrator>(
        m_notificationManager.get(),
        m_config.get(),
        this
    );

    m_actionExecutor = std::make_unique<ActionExecutor>(
        m_textInput.get(),
        m_clipboardManager.get(),
        m_config.get(),
        m_notificationOrchestrator.get(),
        this
    );

    m_actionManager = std::make_unique<ActionManager>();

    m_touchWorkflowCoordinator = std::make_unique<TouchWorkflowCoordinator>(
        m_dbusClient.get(),
        m_touchHandler.get(),
        m_actionExecutor.get(),
        m_notificationOrchestrator.get(),
        m_config.get(),
        this
    );

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

    connect(m_actionExecutor.get(), &ActionExecutor::notificationRequested,
            this, &YubiKeyRunner::onNotificationRequested);

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

    QStringList data = match.data().toStringList();
    if (data.size() < 5) {
        qCDebug(YubiKeyRunnerLog) << "Invalid match data";
        return;
    }

    QString credentialName = data.at(0);
    QString displayName = data.at(1);
    QString code = data.at(2);
    bool requiresTouch = data.at(3) == QStringLiteral("true");
    bool isPasswordError = data.at(4) == QStringLiteral("true");
    QString deviceId = data.size() > 5 ? data.at(5) : QString();

    qCDebug(YubiKeyRunnerLog) << "credentialName:" << credentialName
             << "deviceId:" << deviceId
             << "requiresTouch:" << requiresTouch
             << "isPasswordError:" << isPasswordError;

    // Handle password error match
    if (isPasswordError) {
        qCDebug(YubiKeyRunnerLog) << "Showing password dialog for authentication error";

        // Use device ID from match data
        if (deviceId.isEmpty()) {
            qCDebug(YubiKeyRunnerLog) << "No device ID in match data";
            m_notificationOrchestrator->showSimpleNotification(
                i18n("Error"),
                i18n("No YubiKey connected"),
                1);
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

    // Process asynchronously
    DeferredExecution::defer(this, [this, credentialName, displayName, code, requiresTouch, actionId, deviceId]() {
        processCredentialAsync(credentialName, displayName, code, requiresTouch, actionId, deviceId);
    });

    qCDebug(YubiKeyRunnerLog) << "run() completed, processing continues asynchronously";
}

void YubiKeyRunner::processCredentialAsync(const QString &credentialName,
                                           const QString &displayName,
                                           const QString &code,
                                           bool requiresTouch,
                                           const QString &actionId,
                                           const QString &deviceId)
{
    Q_UNUSED(displayName)
    qCDebug(YubiKeyRunnerLog) << "processCredentialAsync() started for:" << credentialName << "device:" << deviceId;

    QString actualCode = code;

    if (actualCode.isEmpty()) {
        qCDebug(YubiKeyRunnerLog) << "Generating new code for credential:" << credentialName;

        if (requiresTouch) {
            qCDebug(YubiKeyRunnerLog) << "Touch required - starting touch workflow";
            m_touchWorkflowCoordinator->startTouchWorkflow(credentialName, actionId, deviceId);
            return;
        }

        // Generate code for non-touch credentials
        m_touchHandler->startTouchOperation(credentialName, 0);
        GenerateCodeResult codeResult = m_dbusClient->generateCode(deviceId, credentialName);

        if (codeResult.code.isEmpty()) {
            qCDebug(YubiKeyRunnerLog) << "Failed to generate code via D-Bus";
            m_notificationOrchestrator->showSimpleNotification(
                i18n("Error"),
                i18n("Failed to generate code"),
                1);
            return;
        }

        actualCode = codeResult.code;
    }

    qCDebug(YubiKeyRunnerLog) << "Executing action:" << actionId;

    // Execute action
    if (actionId == QStringLiteral("type")) {
        qCDebug(YubiKeyRunnerLog) << "[TIMING] Deferring type action by 150ms to allow KRunner to close and focus to return";

        // Defer typing to allow KRunner window to close and focus to return to previous window
        // Without this delay, key events are sent while KRunner has focus, causing them to be lost
        QTimer::singleShot(150, this, [this, actualCode, credentialName]() {
            qCDebug(YubiKeyRunnerLog) << "[TIMING] Executing deferred type action now";

            auto result = m_actionExecutor->executeTypeAction(actualCode, credentialName);

            qCDebug(YubiKeyRunnerLog) << "executeTypeAction() returned result:" << static_cast<int>(result);

            // Handle different results
            if (result == ActionExecutor::ActionResult::Success) {
                qCDebug(YubiKeyRunnerLog) << "Type action succeeded";
            } else if (result == ActionExecutor::ActionResult::WaitingForPermission) {
                qCDebug(YubiKeyRunnerLog) << "Type action waiting for permission - user needs to approve dialog";
                // Don't show any notification - waiting for user to approve permission dialog
                // On next attempt (after approval), typing will work
            } else if (result == ActionExecutor::ActionResult::Failed) {
                qCDebug(YubiKeyRunnerLog) << "Type action failed - code was copied to clipboard as fallback";
                // If permission was rejected or type failed, code was copied to clipboard as fallback
                // Show code notification (ActionExecutor already showed "Permission Denied" notification if rejected)
                int totalSeconds = NotificationHelper::calculateNotificationDuration(m_config.get());

                m_notificationOrchestrator->showCodeNotification(actualCode, credentialName, totalSeconds);
            }
        });
    } else {
        auto result = m_actionExecutor->executeCopyAction(actualCode, credentialName);

        qCDebug(YubiKeyRunnerLog) << "executeCopyAction() returned result:" << static_cast<int>(result);

        // Show code notification for copy action
        if (result == ActionExecutor::ActionResult::Success) {
            qCDebug(YubiKeyRunnerLog) << "Copy action succeeded";
            int totalSeconds = NotificationHelper::calculateNotificationDuration(m_config.get());

            m_notificationOrchestrator->showCodeNotification(actualCode, credentialName, totalSeconds);
        } else {
            qCDebug(YubiKeyRunnerLog) << "Copy action failed";
        }
    }

    qCDebug(YubiKeyRunnerLog) << "processCredentialAsync() completed";
}

void YubiKeyRunner::reloadConfiguration()
{
    qCDebug(YubiKeyRunnerLog) << "reloadConfiguration() called - notifying components";

    // Rebuild actions based on new primary action configuration
    setupActions();

    // Update MatchBuilder with new actions
    m_matchBuilder = std::make_unique<MatchBuilder>(
        this,
        m_config.get(),
        m_actions
    );

    // Password management is now handled by the daemon
    // Just notify all components that configuration has changed
    Q_EMIT m_config->configurationChanged();
}

void YubiKeyRunner::onDeviceConnected(const QString &deviceId)
{
    qCDebug(YubiKeyRunnerLog) << "Device connected:" << deviceId;
    // Credentials will be automatically available from daemon
}

void YubiKeyRunner::onDeviceDisconnected(const QString &deviceId)
{
    qCDebug(YubiKeyRunnerLog) << "Device disconnected:" << deviceId;
    // Credential cache will be updated by daemon
}

void YubiKeyRunner::onCredentialsUpdated(const QString &deviceId)
{
    qCDebug(YubiKeyRunnerLog) << "Credentials updated for device:" << deviceId;
}

void YubiKeyRunner::onDaemonUnavailable()
{
    qCWarning(YubiKeyRunnerLog) << "D-Bus daemon became unavailable";

    m_notificationOrchestrator->showSimpleNotification(
        i18n("YubiKey OATH"),
        i18n("YubiKey daemon disconnected"),
        1);
}

void YubiKeyRunner::showPasswordDialog(const QString &deviceId,
                                        const QString &deviceName)
{
    PasswordDialogHelper::showDialog(
        deviceId,
        deviceName,
        m_dbusClient.get(),
        this,
        [this]() {
            // Success - show notification
            m_notificationOrchestrator->showSimpleNotification(
                i18n("YubiKey OATH"),
                i18n("Password saved successfully"),
                0);
        }
    );
}

void YubiKeyRunner::onNotificationRequested(const QString &title, const QString &message, int type)
{
    m_notificationOrchestrator->showSimpleNotification(title, message, type);
}

} // namespace YubiKey
} // namespace KRunner

// Must use unqualified name for K_PLUGIN_CLASS - MOC doesn't support namespaced names
using KRunner::YubiKey::YubiKeyRunner;
K_PLUGIN_CLASS_WITH_JSON(YubiKeyRunner, "yubikeyrunner.json")

#include "yubikeyrunner.moc"
#include "moc_yubikeyrunner.cpp"
