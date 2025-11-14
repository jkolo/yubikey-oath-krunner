/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "yubikeyrunner.h"
#include "dbus/oath_manager_proxy.h"
#include "dbus/oath_credential_proxy.h"
#include "dbus/oath_device_proxy.h"
#include "ui/password_dialog_helper.h"
#include "logging_categories.h"
#include "shared/utils/yubikey_icon_resolver.h"
#include "shared/types/device_model.h"
#include "shared/types/device_brand.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <QDebug>
#include <QTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <QMessageBox>
#include <QDBusInterface>
#include <QDBusConnection>
#include <QIcon>

namespace YubiKeyOath {
namespace Runner {
using namespace YubiKeyOath::Shared;

YubiKeyRunner::YubiKeyRunner(QObject *parent, const KPluginMetaData &metaData)
    : KRunner::AbstractRunner(parent, metaData)
    , m_manager(OathManagerProxy::instance(this))
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
    connect(m_manager, &OathManagerProxy::deviceConnected,
            this, &YubiKeyRunner::onDeviceConnected);
    connect(m_manager, &OathManagerProxy::deviceDisconnected,
            this, &YubiKeyRunner::onDeviceDisconnected);
    connect(m_manager, &OathManagerProxy::credentialsChanged,
            this, &YubiKeyRunner::onCredentialsUpdated);
    connect(m_manager, &OathManagerProxy::daemonUnavailable,
            this, &YubiKeyRunner::onDaemonUnavailable);

    // Connect configuration change signal - setupActions only, no reload
    // (reload is called automatically by QFileSystemWatcher in config)
    connect(m_config.get(), &KRunnerConfiguration::configurationChanged,
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
    const QString primary = m_config->primaryAction();
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

    // Allow minimum 2 characters to enable searching from "ad" and "add"
    if (!context.isValid() || context.query().length() < 2) {
        qCDebug(YubiKeyRunnerLog) << "Query too short or invalid (minimum 2 characters)";
        return;
    }

    if (!m_manager->isDaemonAvailable()) {
        qCDebug(YubiKeyRunnerLog) << "D-Bus daemon not available";
        return;
    }

    const QString query = context.query().toLower();

    // Check for "Add OATH" command - multilingual keyword matching
    bool matchesKeyword = false;
    for (const QString &keyword : m_addOathKeywords) {
        if (query.contains(keyword)) {
            matchesKeyword = true;
            qCDebug(YubiKeyRunnerLog) << "Detected 'Add OATH' command (matched keyword:" << keyword << ")";
            break;
        }
    }

    if (matchesKeyword) {
        // Get all devices and create a match for each one
        const QList<OathDeviceProxy*> allDevices = m_manager->devices();
        qCDebug(YubiKeyRunnerLog) << "Creating Add OATH matches for" << allDevices.size() << "devices";

        for (const auto *device : allDevices) {
            // Reconstruct DeviceModel for icon resolution
            DeviceModel deviceModel;
            deviceModel.brand = detectBrandFromModelString(device->deviceModel());
            deviceModel.modelCode = device->deviceModelCode();
            deviceModel.modelString = device->deviceModel();
            deviceModel.capabilities = device->capabilities();

            // Get device-specific icon theme name
            const QString iconName = YubiKeyIconResolver::getIconName(deviceModel);

            KRunner::QueryMatch match(this);
            match.setId(QStringLiteral("add-oath-to-") + device->deviceId());
            match.setText(i18n("Add OATH to %1", device->name()));
            match.setIcon(QIcon::fromTheme(iconName));  // Use device-specific icon from theme

            if (device->isConnected()) {
                match.setSubtext(i18n("Device is connected - ready to add"));
                match.setRelevance(1.0);
            } else {
                match.setSubtext(i18n("Device offline - will wait for connection"));
                match.setRelevance(0.8);
            }

            // Store device ID in match data for run handler
            match.setData(QVariantList{device->deviceId()});

            context.addMatch(match);
            qCDebug(YubiKeyRunnerLog) << "Created Add OATH match for device:" << device->name()
                                       << "ID:" << device->deviceId()
                                       << "connected:" << device->isConnected();
        }
    }

    // Get all devices to check their password status and state
    const QList<OathDeviceProxy*> devices = m_manager->devices();
    qCDebug(YubiKeyRunnerLog) << "Found" << devices.size() << "known devices";

    int readyDevices = 0;
    int initializingDevices = 0;

    // For each CONNECTED device that needs password, show password error match
    // Skip devices that are still initializing
    for (const auto *device : devices) {
        const DeviceState state = device->state();

        // Count devices by state
        if (isDeviceStateTransitional(state)) {
            initializingDevices++;
            qCDebug(YubiKeyRunnerLog) << "Device" << device->name()
                                      << "is initializing (state:" << deviceStateToString(state) << ")";
            continue; // Skip non-ready devices
        }

        if (state == DeviceState::Ready) {
            readyDevices++;
        }

        if (device->isConnected() &&
            device->requiresPassword() &&
            !device->hasValidPassword()) {
            qCDebug(YubiKeyRunnerLog) << "Device requires password:"
                                      << device->name() << "serial:" << device->serialNumber();
            const DeviceInfo deviceInfo = device->toDeviceInfo();
            const KRunner::QueryMatch match = m_matchBuilder->buildPasswordErrorMatch(deviceInfo);
            context.addMatch(match);
            // DON'T return - continue to show credentials from other devices!
        }
    }

    // If all devices are still initializing, wait for them to become ready
    if (readyDevices == 0 && initializingDevices > 0) {
        qCDebug(YubiKeyRunnerLog) << initializingDevices << "device(s) still initializing - no credentials available yet";
        return;
    }

    // Get credentials from ALL devices (manager aggregates them)
    const QList<OathCredentialProxy*> credentials = m_manager->getAllCredentials();
    qCDebug(YubiKeyRunnerLog) << "Found" << credentials.size() << "total credentials";

    if (credentials.isEmpty()) {
        qCDebug(YubiKeyRunnerLog) << "No credentials available from any device";
        return;
    }

    // Build matches for matching credentials from all working devices
    int matchCount = 0;
    for (auto *credential : credentials) {
        const QString name = credential->fullName().toLower();
        const QString issuer = credential->issuer().toLower();
        const QString account = credential->username().toLower();

        if (name.contains(query) || issuer.contains(query) || account.contains(query)) {
            qCDebug(YubiKeyRunnerLog) << "Creating match for credential:" << credential->fullName();
            const KRunner::QueryMatch match = m_matchBuilder->buildCredentialMatch(
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

    // Handle "Add OATH to {Device}" command
    if (match.id().startsWith(QStringLiteral("add-oath-to-"))) {
        qCDebug(YubiKeyRunnerLog) << "Starting Add OATH Credential workflow for device";

        // Extract device ID from match data
        const QString deviceId = match.data().toList().at(0).toString();
        qCDebug(YubiKeyRunnerLog) << "Target device ID:" << deviceId;

        // Find the device proxy
        const QList<OathDeviceProxy*> devices = m_manager->devices();
        const OathDeviceProxy *targetDevice = nullptr;

        for (auto *device : devices) {
            if (device->deviceId() == deviceId) {
                targetDevice = device;
                break;
            }
        }

        if (!targetDevice) {
            qCWarning(YubiKeyRunnerLog) << "Device not found:" << deviceId;
            return;
        }

        // Delegate to device with empty parameters to trigger interactive mode (dialog)
        // Dialog will handle waiting for device connection if needed
        // Use async call to prevent blocking KRunner UI
        qCDebug(YubiKeyRunnerLog) << "Calling AddCredential asynchronously on device:" << targetDevice->name();

        QDBusInterface interface(
            QStringLiteral("pl.jkolo.yubikey.oath.daemon"),
            targetDevice->objectPath(),
            QStringLiteral("pl.jkolo.yubikey.oath.Device"),
            QDBusConnection::sessionBus()
        );

        // Fire-and-forget async call - don't wait for response
        interface.asyncCall(
            QStringLiteral("AddCredential"),
            QString(),  // name - empty triggers dialog
            QString(),  // secret - empty triggers dialog
            QString(),  // type - will default to TOTP
            QString(),  // algorithm - will default to SHA1
            0,          // digits - will default to 6
            0,          // period - will default to 30
            0,          // counter - will default to 0
            false       // requireTouch
        );

        qCDebug(YubiKeyRunnerLog) << "Async call initiated, KRunner can close immediately";
        return;
    }

    const QStringList data = match.data().toStringList();
    if (data.size() < 5) {
        qCDebug(YubiKeyRunnerLog) << "Invalid match data";
        return;
    }

    const QString &credentialName = data.at(0);
    const QString &displayName = data.at(1);
    Q_UNUSED(displayName)
    const QString &code = data.at(2);
    Q_UNUSED(code)
    const bool requiresTouch = data.at(3) == QStringLiteral("true");
    Q_UNUSED(requiresTouch)
    const bool isPasswordError = data.at(4) == QStringLiteral("true");
    const QString deviceId = data.size() > 5 ? data.at(5) : QString();

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
        const OathDeviceProxy *const device = m_manager->getDevice(deviceId);
        if (!device) {
            qCWarning(YubiKeyRunnerLog) << "Device not found:" << deviceId;
            return;
        }

        const QString deviceName = device->name();

        // Show password dialog (non-modal, with retry on error)
        showPasswordDialog(deviceId, deviceName);

        return;
    }

    if (credentialName.isEmpty()) {
        qCDebug(YubiKeyRunnerLog) << "Empty credential name";
        return;
    }

    // Find the credential proxy
    OathCredentialProxy *credential = nullptr;

    // If we have deviceId, get it from that device
    if (!deviceId.isEmpty()) {
        const OathDeviceProxy *const device = m_manager->getDevice(deviceId);
        if (device) {
            credential = device->getCredential(credentialName);
        }
    }

    // Fallback: search all credentials
    if (!credential) {
        const QList<OathCredentialProxy*> allCredentials = m_manager->getAllCredentials();
        for (auto *cred : allCredentials) {
            if (cred->fullName() == credentialName) {
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
    const QString primaryAction = m_config->primaryAction();
    const QString actionId = m_actionManager->determineAction(match, primaryAction);

    qCDebug(YubiKeyRunnerLog) << "Action selection - primary from config:" << primaryAction
             << "determined action:" << actionId
             << "action name:" << m_actionManager->getActionName(actionId);

    // Execute action via credential proxy methods
    if (actionId == QStringLiteral("delete")) {
        // Show confirmation dialog before deleting
        qCDebug(YubiKeyRunnerLog) << "Showing delete confirmation dialog for:" << credentialName;

        const QMessageBox::StandardButton reply = QMessageBox::question(
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
        // Use QTimer to delay execution until window closes
        qCDebug(YubiKeyRunnerLog) << "Scheduling type action (async) for KRunner to close";

        // Schedule execution: wait for KRunner to close (500ms delay)
        // Capture by value (QString is copy-on-write, safe for async execution)
        QTimer::singleShot(500, this, [this, credentialName, deviceId]() {
            qCDebug(YubiKeyRunnerLog) << "Executing type action (async) after KRunner close:" << credentialName;

            // Re-find credential (proxy might have changed during delay)
            OathCredentialProxy *cred = nullptr;
            for (auto *c : m_manager->getAllCredentials()) {
                if (c->fullName() == credentialName && c->deviceId() == deviceId) {
                    cred = c;
                    break;
                }
            }

            if (!cred) {
                qCWarning(YubiKeyRunnerLog) << "Credential not found after delay:" << credentialName;
                return;
            }

            // Fire-and-forget async call with fallback to clipboard
            cred->typeCode(true);
            // Result will be delivered via CodeTyped signal
            // TouchWorkflowCoordinator will show notifications if needed
            qCDebug(YubiKeyRunnerLog) << "Type action requested (async)";
        });

        // Return immediately - action will execute asynchronously
        return;
    } else {  // copy - fire-and-forget async call
        qCDebug(YubiKeyRunnerLog) << "Executing copy action (async) via credential proxy";
        credential->copyToClipboard();
        // Result will be delivered via ClipboardCopied signal
        // TouchWorkflowCoordinator will show notifications if needed
        qCDebug(YubiKeyRunnerLog) << "Copy action requested (async)";
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

    // Initialize translated keywords for "Add OATH" matching
    m_addOathKeywords.clear();
    const QString translatedAdd = i18nc("search keyword", "add").toLower();
    m_addOathKeywords << translatedAdd;
    // Add English "add" if translation is different (multi-language support)
    if (translatedAdd != QStringLiteral("add")) {
        m_addOathKeywords << QStringLiteral("add");
    }
    m_addOathKeywords << QStringLiteral("oath");
    m_addOathKeywords << QStringLiteral("totp");
    m_addOathKeywords << QStringLiteral("hotp");

    qCDebug(YubiKeyRunnerLog) << "Add OATH keywords:" << m_addOathKeywords;

    // This method is kept for manual reload from init()
    setupActions();
}

void YubiKeyRunner::onDeviceConnected(OathDeviceProxy *device)
{
    if (device) {
        qCDebug(YubiKeyRunnerLog) << "Device connected:" << device->name()
                                  << "serial:" << device->serialNumber()
                                  << "state:" << deviceStateToString(device->state());

        // Connect to state change signals for logging/debugging
        connect(device, &OathDeviceProxy::stateChanged, this, [device](DeviceState newState) {
            qCDebug(YubiKeyRunnerLog) << "Device" << device->name()
                                      << "state changed to:" << deviceStateToString(newState);
        });
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
