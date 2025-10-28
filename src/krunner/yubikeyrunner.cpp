/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikeyrunner.h"
#include "dbus/yubikey_manager_proxy.h"
#include "dbus/yubikey_credential_proxy.h"
#include "dbus/yubikey_device_proxy.h"
#include "ui/password_dialog_helper.h"
#include "logging_categories.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <QMessageBox>

namespace YubiKeyOath {
namespace Runner {
using namespace YubiKeyOath::Shared;

YubiKeyRunner::YubiKeyRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
    , m_manager(YubiKeyManagerProxy::instance(this))
{
    qCDebug(YubiKeyRunnerLog) << "Constructor called - using proxy architecture";

    // Set translation domain for i18n
    KLocalizedString::setApplicationDomain("yubikey_oath");

    setObjectName(QStringLiteral("yubikey-oath"));

    // Create configuration provider (uses yubikey-oathrc like daemon and config module)
    m_config = std::make_unique<KRunnerConfiguration>(this);

    // Create runner components
    m_actionManager = std::make_unique<ActionManager>();

    // Setup actions first, before creating MatchBuilder
    setupActions();

    m_matchBuilder = std::make_unique<MatchBuilder>(
        this,
        m_config.get(),
        m_actions
    );

    // Connect Manager proxy signals
    connect(m_manager, &YubiKeyManagerProxy::deviceConnected,
            this, &YubiKeyRunner::onDeviceConnected);
    connect(m_manager, &YubiKeyManagerProxy::deviceDisconnected,
            this, &YubiKeyRunner::onDeviceDisconnected);
    connect(m_manager, &YubiKeyManagerProxy::credentialsChanged,
            this, &YubiKeyRunner::onCredentialsUpdated);
    connect(m_manager, &YubiKeyManagerProxy::daemonUnavailable,
            this, &YubiKeyRunner::onDaemonUnavailable);

    // Connect configuration change signal - setupActions only, no reload
    // (reload is called automatically by QFileSystemWatcher in config)
    connect(m_config.get(), &ConfigurationProvider::configurationChanged,
            this, &YubiKeyRunner::setupActions);

    qCDebug(YubiKeyRunnerLog) << "Constructor finished";
}

YubiKeyRunner::~YubiKeyRunner() = default;

void YubiKeyRunner::init()
{
    qCDebug(YubiKeyRunnerLog) << "init() called";
    reloadConfiguration();

    // Check if daemon is available
    if (m_manager->isDaemonAvailable()) {
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

    // Always add Delete action as third button (always visible)
    m_actions.append(KRunner::Action(QStringLiteral("delete"),
                                      QStringLiteral("edit-delete"),
                                      i18n("Delete credential")));

    qCDebug(YubiKeyRunnerLog) << "setupActions() - created" << m_actions.size() << "action(s)";
}

void YubiKeyRunner::match(KRunner::RunnerContext &context)
{
    qCDebug(YubiKeyRunnerLog) << "match() called with query:" << context.query();

    if (!context.isValid() || context.query().length() < 2) {
        qCDebug(YubiKeyRunnerLog) << "Query too short or invalid";
        return;
    }

    if (!m_manager->isDaemonAvailable()) {
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
    QList<YubiKeyDeviceProxy*> devices = m_manager->devices();
    qCDebug(YubiKeyRunnerLog) << "Found" << devices.size() << "known devices";

    // For each CONNECTED device that needs password, show password error match
    for (const auto *device : devices) {
        if (device->isConnected() &&
            device->requiresPassword() &&
            !device->hasValidPassword()) {
            qCDebug(YubiKeyRunnerLog) << "Device requires password:"
                                      << device->name() << device->deviceId();
            DeviceInfo deviceInfo = device->toDeviceInfo();
            KRunner::QueryMatch match = m_matchBuilder->buildPasswordErrorMatch(deviceInfo);
            context.addMatch(match);
            // DON'T return - continue to show credentials from other devices!
        }
    }

    // Get credentials from ALL devices (manager aggregates them)
    QList<YubiKeyCredentialProxy*> credentials = m_manager->getAllCredentials();
    qCDebug(YubiKeyRunnerLog) << "Found" << credentials.size() << "total credentials";

    if (credentials.isEmpty()) {
        qCDebug(YubiKeyRunnerLog) << "No credentials available from any device";
        return;
    }

    // Build matches for matching credentials from all working devices
    int matchCount = 0;
    for (auto *credential : credentials) {
        QString name = credential->name().toLower();
        QString issuer = credential->issuer().toLower();
        QString username = credential->username().toLower();

        if (name.contains(query) || issuer.contains(query) || username.contains(query)) {
            qCDebug(YubiKeyRunnerLog) << "Creating match for credential:" << credential->name();
            KRunner::QueryMatch match = m_matchBuilder->buildCredentialMatch(
                credential, query, m_manager);
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
        qCDebug(YubiKeyRunnerLog) << "Starting Add OATH Credential workflow via device proxy";

        // Get first available device (or show error if none)
        QList<YubiKeyDeviceProxy*> devices = m_manager->devices();
        YubiKeyDeviceProxy *targetDevice = nullptr;

        // Find first connected device
        for (auto *device : devices) {
            if (device->isConnected()) {
                targetDevice = device;
                break;
            }
        }

        if (!targetDevice) {
            qCWarning(YubiKeyRunnerLog) << "No connected YubiKey found for add credential";
            return;
        }

        // Delegate to device with empty parameters to trigger interactive mode (dialog)
        AddCredentialResult result = targetDevice->addCredential(
            QString(),  // name - empty triggers dialog
            QString(),  // secret - empty triggers dialog
            QString(),  // type - will default to TOTP
            QString(),  // algorithm - will default to SHA1
            0,          // digits - will default to 6
            0,          // period - will default to 30
            0,          // counter - will default to 0
            false       // requireTouch
        );

        if (result.status != QStringLiteral("Success")) {
            qCWarning(YubiKeyRunnerLog) << "Add credential workflow failed:" << result.status << result.message;
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

        // Get device from manager
        YubiKeyDeviceProxy *device = m_manager->getDevice(deviceId);
        if (!device) {
            qCWarning(YubiKeyRunnerLog) << "Device not found:" << deviceId;
            return;
        }

        QString deviceName = device->name();

        // Show password dialog (non-modal, with retry on error)
        showPasswordDialog(deviceId, deviceName);

        return;
    }

    if (credentialName.isEmpty()) {
        qCDebug(YubiKeyRunnerLog) << "Empty credential name";
        return;
    }

    // Find the credential proxy
    YubiKeyCredentialProxy *credential = nullptr;

    // If we have deviceId, get it from that device
    if (!deviceId.isEmpty()) {
        YubiKeyDeviceProxy *device = m_manager->getDevice(deviceId);
        if (device) {
            credential = device->getCredential(credentialName);
        }
    }

    // Fallback: search all credentials
    if (!credential) {
        QList<YubiKeyCredentialProxy*> allCredentials = m_manager->getAllCredentials();
        for (auto *cred : allCredentials) {
            if (cred->name() == credentialName) {
                credential = cred;
                break;
            }
        }
    }

    if (!credential) {
        qCWarning(YubiKeyRunnerLog) << "Credential not found:" << credentialName;
        return;
    }

    // Determine which action to execute using ActionManager
    QString primaryAction = m_config->primaryAction();
    QString actionId = m_actionManager->determineAction(match, primaryAction);

    qCDebug(YubiKeyRunnerLog) << "Action selection - primary from config:" << primaryAction
             << "determined action:" << actionId
             << "action name:" << m_actionManager->getActionName(actionId);

    // Execute action via credential proxy methods
    if (actionId == QStringLiteral("delete")) {
        // Show confirmation dialog before deleting
        qCDebug(YubiKeyRunnerLog) << "Showing delete confirmation dialog for:" << credentialName;

        QMessageBox::StandardButton reply = QMessageBox::question(
            nullptr,
            i18n("Delete Credential?"),
            i18n("Are you sure you want to delete '%1' from your YubiKey?\n\nThis action cannot be undone.", credentialName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No  // Default button is "No" for safety
        );

        if (reply == QMessageBox::Yes) {
            qCDebug(YubiKeyRunnerLog) << "User confirmed deletion for:" << credentialName;
            credential->deleteCredential();
            qCDebug(YubiKeyRunnerLog) << "Delete action completed successfully:" << credentialName;
        } else {
            qCDebug(YubiKeyRunnerLog) << "User cancelled deletion for:" << credentialName;
        }

        return;
    } else if (actionId == QStringLiteral("type")) {
        // Type action must execute AFTER KRunner closes completely
        // Use async execution with QEventLoop to avoid blocking run() return
        qCDebug(YubiKeyRunnerLog) << "Scheduling asynchronous type action for KRunner to close";

        // Capture data needed for async execution (avoid dangling pointers)
        QString capturedCredName = credentialName;
        QString capturedDeviceId = deviceId;

        // Schedule async execution: processEvents() allows KRunner to close, then 500ms delay for safety
        QTimer::singleShot(0, this, [this, capturedCredName, capturedDeviceId]() {
            qCDebug(YubiKeyRunnerLog) << "Starting async type action execution for:" << capturedCredName;

            // First, let KRunner close (processEvents ensures UI updates)
            QCoreApplication::processEvents();

            // Then wait 500ms for window to fully hide using QEventLoop
            QEventLoop loop;
            QTimer::singleShot(500, &loop, &QEventLoop::quit);
            loop.exec();

            qCDebug(YubiKeyRunnerLog) << "Executing type action after KRunner close:" << capturedCredName;

            // Re-find credential (proxy might have changed during delay)
            YubiKeyCredentialProxy *cred = nullptr;
            for (auto *c : m_manager->getAllCredentials()) {
                if (c->name() == capturedCredName && c->deviceId() == capturedDeviceId) {
                    cred = c;
                    break;
                }
            }

            if (!cred) {
                qCWarning(YubiKeyRunnerLog) << "Credential not found after delay:" << capturedCredName;
                return;
            }

            bool success = cred->typeCode(true);  // fallback to clipboard if typing fails
            if (!success) {
                qCWarning(YubiKeyRunnerLog) << "Type action failed:" << capturedCredName;
            } else {
                qCDebug(YubiKeyRunnerLog) << "Type action completed successfully:" << capturedCredName;
            }
        });

        // Return immediately - action will execute asynchronously
        return;
    } else {  // copy - no delay needed (clipboard doesn't require closed window)
        qCDebug(YubiKeyRunnerLog) << "Executing copy action immediately via credential proxy";
        bool success = credential->copyToClipboard();

        if (!success) {
            qCWarning(YubiKeyRunnerLog) << "Copy action failed:" << actionId;
        } else {
            qCDebug(YubiKeyRunnerLog) << "Copy action completed successfully:" << actionId;
        }
    }
}

void YubiKeyRunner::showPasswordDialog(const QString &deviceId, const QString &deviceName)
{
    qCDebug(YubiKeyRunnerLog) << "showPasswordDialog() for device:" << deviceId;

    // Use PasswordDialogHelper for non-modal password dialog with retry
    PasswordDialogHelper::showDialog(
        deviceId,
        deviceName,
        m_manager,
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

    // Note: Don't call m_config->reload() here to avoid infinite recursion
    // QFileSystemWatcher automatically calls reload() which emits configurationChanged()
    // which is connected to setupActions()

    // This method is kept for manual reload from init()
    setupActions();
}

void YubiKeyRunner::onDeviceConnected(YubiKeyDeviceProxy *device)
{
    if (device) {
        qCDebug(YubiKeyRunnerLog) << "Device connected:" << device->deviceId() << device->name();
    }
}

void YubiKeyRunner::onDeviceDisconnected(const QString &deviceId)
{
    qCDebug(YubiKeyRunnerLog) << "Device disconnected:" << deviceId;
}

void YubiKeyRunner::onCredentialsUpdated()
{
    qCDebug(YubiKeyRunnerLog) << "Credentials updated";
}

void YubiKeyRunner::onDaemonUnavailable()
{
    qCWarning(YubiKeyRunnerLog) << "Daemon became unavailable";
}

K_PLUGIN_CLASS_WITH_JSON(YubiKeyRunner, "yubikeyrunner.json")

} // namespace Runner
} // namespace YubiKeyOath

#include "yubikeyrunner.moc"
