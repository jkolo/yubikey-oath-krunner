/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "oath_device_object.h"
#include "oath_credential_object.h"
#include "services/yubikey_service.h"
#include "oath/oath_device.h"
#include "types/device_state.h"
#include "logging_categories.h"
#include "deviceadaptor.h"  // Auto-generated D-Bus adaptor

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QCryptographicHash>
#include <QUrl>
#include <utility>

namespace YubiKeyOath {
namespace Daemon {

static constexpr const char *DEVICE_INTERFACE = "pl.jkolo.yubikey.oath.Device";

OathDeviceObject::OathDeviceObject(QString deviceId,
                                         QString objectPath,
                                         YubiKeyService *service,
                                         QDBusConnection connection,
                                         QObject *parent)
    : QObject(parent)
    , m_deviceId(std::move(deviceId))
    , m_service(service)
    , m_connection(std::move(connection))
    , m_objectPath(std::move(objectPath))
    , m_id(m_objectPath.section(QLatin1Char('/'), -1))  // Extract last segment of path
    , m_registered(false)
    , m_requiresPassword(false)
    , m_hasValidPassword(false)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Constructing for device:" << m_deviceId
                              << "at path:" << m_objectPath;

    // Create D-Bus adaptor for Device interface
    // This automatically registers pl.jkolo.yubikey.oath.Device interface
    new DeviceAdaptor(this);
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: DeviceAdaptor created";

    // Get initial device info from service
    const auto devices = m_service->listDevices();
    for (const auto &devInfo : devices) {
        if (devInfo._internalDeviceId == m_deviceId) {
            m_name = devInfo.deviceName;
            m_requiresPassword = devInfo.requiresPassword;
            m_hasValidPassword = devInfo.hasValidPassword;
            m_firmwareVersion = devInfo.firmwareVersion;
            m_deviceModel = devInfo.deviceModel;
            m_rawDeviceModel = devInfo.deviceModelCode;  // Numeric model code
            m_serialNumber = devInfo.serialNumber;
            m_formFactor = devInfo.formFactor;
            m_capabilities = devInfo.capabilities;

            // Raw form factor needs to be obtained from daemon's internal structures
            // For now, initialize with default - it's only used for internal logic
            m_rawFormFactor = 0;
            break;
        }
    }

    // Connect to service signals for credential updates
    connect(m_service, &YubiKeyService::credentialsUpdated,
            this, [this](const QString &deviceId) {
                if (deviceId == m_deviceId) {
                    updateCredentials();
                }
            });

    // Connect to device state signals if device is available
    auto *device = m_service->getDevice(m_deviceId);
    if (device) {
        connect(device, &OathDevice::stateChanged,
                this, [this](Shared::DeviceState newState) {
                    auto *dev = m_service->getDevice(m_deviceId);
                    const QString errorMsg = dev ? dev->lastError() : QString();
                    setState(static_cast<quint8>(newState), errorMsg);
                });

        // Set initial state from device
        setState(static_cast<quint8>(device->state()), device->lastError());
    }
}

OathDeviceObject::~OathDeviceObject()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Destructor for device:" << m_deviceId;
    unregisterObject();
}

bool OathDeviceObject::registerObject()
{
    if (m_registered) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Already registered:" << m_deviceId;
        return true;
    }

    // Register on D-Bus using adaptor (exports interfaces defined in XML)
    // Using ExportAdaptors ensures we use the interface name from the adaptor's Q_CLASSINFO,
    // not the C++ class name
    if (!m_connection.registerObject(m_objectPath, this, QDBusConnection::ExportAdaptors)) {
        qCCritical(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Failed to register at"
                                     << m_objectPath << ":" << m_connection.lastError().message();
        return false;
    }

    m_registered = true;
    qCInfo(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Registered successfully:" << m_deviceId
                             << "at" << m_objectPath;

    // Load initial credentials
    updateCredentials();

    return true;
}

void OathDeviceObject::unregisterObject()
{
    if (!m_registered) {
        return;
    }

    // Remove all credential objects first
    const QStringList credIds = m_credentials.keys();
    for (const QString &credId : credIds) {
        removeCredential(credId);
    }

    m_connection.unregisterObject(m_objectPath);
    m_registered = false;
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Unregistered:" << m_deviceId;
}

QString OathDeviceObject::objectPath() const
{
    return m_objectPath;
}

QString OathDeviceObject::name() const
{
    return m_name;
}

quint8 OathDeviceObject::state() const
{
    return m_state;
}

QString OathDeviceObject::stateMessage() const
{
    return m_stateMessage;
}

QString OathDeviceObject::deviceId() const
{
    return m_deviceId;
}

bool OathDeviceObject::requiresPassword() const
{
    return m_requiresPassword;
}

bool OathDeviceObject::hasValidPassword() const
{
    return m_hasValidPassword;
}

QString OathDeviceObject::firmwareVersionString() const
{
    return m_firmwareVersion.toString();
}

quint32 OathDeviceObject::serialNumber() const
{
    return m_serialNumber;
}

QString OathDeviceObject::id() const
{
    return m_id;
}

QString OathDeviceObject::deviceModelString() const
{
    return m_deviceModel;
}

quint32 OathDeviceObject::deviceModelCode() const
{
    return m_rawDeviceModel;
}

QString OathDeviceObject::formFactorString() const
{
    return m_formFactor;
}

QStringList OathDeviceObject::capabilitiesList() const
{
    return m_capabilities;
}

// Internal getters for raw values
Shared::YubiKeyModel OathDeviceObject::deviceModel() const
{
    return m_rawDeviceModel;
}

quint8 OathDeviceObject::formFactor() const
{
    return m_rawFormFactor;
}

qint64 OathDeviceObject::lastSeen() const
{
    const QDateTime lastSeenDateTime = m_service->getDeviceLastSeen(m_deviceId);
    if (lastSeenDateTime.isValid()) {
        return lastSeenDateTime.toMSecsSinceEpoch();
    }
    return 0; // Return 0 if device not in database or invalid timestamp
}

void OathDeviceObject::setName(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Cannot set empty name for device:"
                                    << m_deviceId;
        return;
    }

    if (m_name == name) {
        return;
    }

    // Update in service (database)
    if (m_service->setDeviceName(m_deviceId, name)) {
        m_name = name;
        Q_EMIT nameChanged(m_name);
        // Emit D-Bus PropertiesChanged signal
        emitPropertyChanged(QStringLiteral("Name"), m_name);
        qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Name changed for device:" << m_deviceId
                                  << "to:" << m_name;
    } else {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Failed to set name for device:"
                                    << m_deviceId;
    }
}

void OathDeviceObject::setState(quint8 state, const QString &message)
{
    bool stateChanged = false;
    bool messageChanged = false;

    if (m_state != state) {
        m_state = state;
        stateChanged = true;
    }

    if (m_stateMessage != message) {
        m_stateMessage = message;
        messageChanged = true;
    }

    // Emit signals if values changed
    if (stateChanged) {
        Q_EMIT this->stateChanged(state);  // Qt property signal
        emitPropertyChanged(QStringLiteral("State"), state);
        qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: State changed for device:" << m_deviceId
                                  << "to:" << static_cast<int>(state);
    }

    if (messageChanged) {
        Q_EMIT stateMessageChanged(message);
        emitPropertyChanged(QStringLiteral("StateMessage"), message);
        if (!message.isEmpty()) {
            qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: State message for device:" << m_deviceId
                                      << "is:" << message;
        }
    }
}

bool OathDeviceObject::SavePassword(const QString &password)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: SavePassword for device:" << m_deviceId;

    const bool success = m_service->savePassword(m_deviceId, password);

    if (success) {
        // Update cached properties
        if (!m_hasValidPassword) {
            m_hasValidPassword = true;
            Q_EMIT hasValidPasswordChanged(m_hasValidPassword);
            // Emit D-Bus PropertiesChanged signal
            emitPropertyChanged(QStringLiteral("HasValidPassword"), m_hasValidPassword);
        }
        qCInfo(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Password saved for device:" << m_deviceId;
    } else {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Failed to save password for device:"
                                    << m_deviceId;
    }

    return success;
}

bool OathDeviceObject::ChangePassword(const QString &oldPassword, const QString &newPassword)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: ChangePassword for device:" << m_deviceId;

    const bool success = m_service->changePassword(m_deviceId, oldPassword, newPassword);

    if (success) {
        // Update properties based on whether password was set or removed
        const bool requiresPassword = !newPassword.isEmpty();
        const bool hasValidPassword = !newPassword.isEmpty();

        if (m_requiresPassword != requiresPassword) {
            m_requiresPassword = requiresPassword;
            Q_EMIT requiresPasswordChanged(m_requiresPassword);
            emitPropertyChanged(QStringLiteral("RequiresPassword"), m_requiresPassword);
        }

        if (m_hasValidPassword != hasValidPassword) {
            m_hasValidPassword = hasValidPassword;
            Q_EMIT hasValidPasswordChanged(m_hasValidPassword);
            emitPropertyChanged(QStringLiteral("HasValidPassword"), m_hasValidPassword);
        }

        if (newPassword.isEmpty()) {
            qCInfo(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Password removed for device:" << m_deviceId;
        } else {
            qCInfo(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Password changed for device:" << m_deviceId;
        }
    } else {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Failed to change password for device:"
                                    << m_deviceId;
    }

    return success;
}

void OathDeviceObject::Forget()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Forget device:" << m_deviceId;

    m_service->forgetDevice(m_deviceId);

    // Note: The device object will be removed by Manager when deviceDisconnected signal is emitted
}

Shared::AddCredentialResult OathDeviceObject::AddCredential(const QString &name,
                                                               const QString &secret,
                                                               const QString &type,
                                                               const QString &algorithm,
                                                               int digits,
                                                               int period,
                                                               int counter,
                                                               bool requireTouch)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: AddCredential for device:" << m_deviceId
                              << "name:" << name;

    auto result = m_service->addCredential(m_deviceId, name, secret, type, algorithm,
                                           digits, period, counter, requireTouch);

    // If success, result.message contains credential name - build path
    if (result.status == QLatin1String("Success")) {
        const QString credId = encodeCredentialId(result.message);
        const QString path = credentialPath(credId);
        return {QLatin1String("Success"), path};
    }

    return result;
}

OathCredentialObject* OathDeviceObject::addCredential(const Shared::OathCredential &credential)
{
    const QString credId = encodeCredentialId(credential.originalName);

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Adding credential:" << credential.originalName
                              << "id:" << credId << "for device:" << m_deviceId;

    // Check if already exists
    if (m_credentials.contains(credId)) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Credential already exists:" << credId;
        return m_credentials.value(credId);
    }

    // Create credential object
    const QString path = credentialPath(credId);
    auto *credObj = new OathCredentialObject(credential, m_deviceId, m_service,
                                              m_connection, this);

    // Set object path before registration
    credObj->setObjectPath(path);

    if (!credObj->registerObject()) {
        qCCritical(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Failed to register credential object"
                                     << credId;
        delete credObj;
        return nullptr;
    }

    m_credentials.insert(credId, credObj);

    // Emit D-Bus signals for ObjectManager
    Q_EMIT CredentialAdded(QDBusObjectPath(path));
    Q_EMIT credentialAdded(); // Internal signal for Manager

    qCInfo(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Credential added:" << credential.originalName
                             << "at" << path;

    return credObj;
}

void OathDeviceObject::removeCredential(const QString &credentialId)
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Removing credential:" << credentialId
                              << "from device:" << m_deviceId;

    if (!m_credentials.contains(credentialId)) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Credential not found:" << credentialId;
        return;
    }

    OathCredentialObject *const credObj = m_credentials.value(credentialId);
    const QString path = credObj->objectPath();

    // Unregister and delete
    credObj->unregisterObject();
    delete credObj;

    m_credentials.remove(credentialId);

    // Emit D-Bus signals for ObjectManager
    Q_EMIT CredentialRemoved(QDBusObjectPath(path));
    Q_EMIT credentialRemoved(); // Internal signal for Manager

    qCInfo(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Credential removed:" << credentialId;
}

OathCredentialObject* OathDeviceObject::getCredential(const QString &credentialId) const
{
    return m_credentials.value(credentialId, nullptr);
}

QStringList OathDeviceObject::credentialPaths() const
{
    QStringList paths;
    for (auto it = m_credentials.constBegin(); it != m_credentials.constEnd(); ++it) {
        paths.append(it.value()->objectPath());
    }
    return paths;
}

void OathDeviceObject::updateCredentials()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Updating credentials for device:"
                              << m_deviceId;

    // Get current credentials from service
    const QList<Shared::OathCredential> currentCreds = m_service->getCredentials(m_deviceId);

    // Build set of current credential IDs
    QSet<QString> currentCredIds;
    for (const auto &cred : currentCreds) {
        currentCredIds.insert(encodeCredentialId(cred.originalName));
    }

    // Build set of existing credential IDs
    const QSet<QString> existingCredIds = QSet<QString>(m_credentials.keyBegin(),
                                                         m_credentials.keyEnd());

    // Remove credentials that no longer exist
    const QSet<QString> toRemove = existingCredIds - currentCredIds;
    for (const QString &credId : toRemove) {
        removeCredential(credId);
    }

    // Add new credentials
    const QSet<QString> toAdd = currentCredIds - existingCredIds;
    for (const auto &cred : currentCreds) {
        const QString credId = encodeCredentialId(cred.originalName);
        if (toAdd.contains(credId)) {
            addCredential(cred);
        }
    }

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Credentials updated for device:"
                              << m_deviceId << "- total:" << m_credentials.size();
}

void OathDeviceObject::connectToDevice()
{
    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Connecting to device:" << m_deviceId;

    auto *device = m_service->getDevice(m_deviceId);
    if (!device) {
        qCWarning(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Device not available:" << m_deviceId;
        // Set disconnected state
        setState(static_cast<quint8>(Shared::DeviceState::Disconnected), QString());
        return;
    }

    // Disconnect any previous connections to avoid duplicates
    // (Qt's connect with lambda creates new connection each time)
    disconnect(device, nullptr, this, nullptr);

    // Connect to device state signals
    connect(device, &OathDevice::stateChanged,
            this, [this](Shared::DeviceState newState) {
                auto *dev = m_service->getDevice(m_deviceId);
                const QString errorMsg = dev ? dev->lastError() : QString();
                setState(static_cast<quint8>(newState), errorMsg);
            });

    // Update current state from device
    setState(static_cast<quint8>(device->state()), device->lastError());

    qCDebug(YubiKeyDaemonLog) << "YubiKeyDeviceObject: Connected to device:" << m_deviceId
                              << "state:" << static_cast<int>(device->state());
}

QVariantMap OathDeviceObject::getManagedObjectData() const
{
    QVariantMap result;

    // pl.jkolo.yubikey.oath.Device interface properties
    QVariantMap deviceProps;
    deviceProps.insert(QLatin1String("Name"), m_name);
    deviceProps.insert(QLatin1String("State"), m_state);
    deviceProps.insert(QLatin1String("StateMessage"), m_stateMessage);
    deviceProps.insert(QLatin1String("RequiresPassword"), m_requiresPassword);
    deviceProps.insert(QLatin1String("HasValidPassword"), m_hasValidPassword);
    deviceProps.insert(QLatin1String("FirmwareVersion"), m_firmwareVersion.toString());
    deviceProps.insert(QLatin1String("SerialNumber"), m_serialNumber);
    deviceProps.insert(QLatin1String("ID"), m_id);
    deviceProps.insert(QLatin1String("DeviceModel"), deviceModelString());
    deviceProps.insert(QLatin1String("DeviceModelCode"), m_rawDeviceModel);
    deviceProps.insert(QLatin1String("FormFactor"), formFactorString());
    deviceProps.insert(QLatin1String("Capabilities"), capabilitiesList());
    deviceProps.insert(QLatin1String("LastSeen"), lastSeen());
    // Note: CredentialCount and Credentials properties removed - use GetManagedObjects() instead

    result.insert(QLatin1String(DEVICE_INTERFACE), deviceProps);

    return result;
}

QVariantMap OathDeviceObject::getManagedCredentialObjects() const
{
    QVariantMap result;

    for (auto it = m_credentials.constBegin(); it != m_credentials.constEnd(); ++it) {
        const QString path = it.value()->objectPath();
        const QVariantMap credData = it.value()->getManagedObjectData();
        result.insert(path, credData);
    }

    return result;
}

QString OathDeviceObject::encodeCredentialId(const QString &credentialName)
{
    // Encode credential name for use in D-Bus object path
    // D-Bus paths allow only: [A-Za-z0-9_/]
    // Use transliteration for Unicode characters and special character mappings

    // Transliteration map for common Unicode characters
    static const QHash<QChar, QString> translitMap = {
        // Polish characters (lowercase)
        {QChar(0x0105), QStringLiteral("a")},  // ą
        {QChar(0x0107), QStringLiteral("c")},  // ć
        {QChar(0x0119), QStringLiteral("e")},  // ę
        {QChar(0x0142), QStringLiteral("l")},  // ł
        {QChar(0x0144), QStringLiteral("n")},  // ń
        {QChar(0x00F3), QStringLiteral("o")},  // ó
        {QChar(0x015B), QStringLiteral("s")},  // ś
        {QChar(0x017A), QStringLiteral("z")},  // ź
        {QChar(0x017C), QStringLiteral("z")},  // ż
        // Polish characters (uppercase)
        {QChar(0x0104), QStringLiteral("a")},  // Ą
        {QChar(0x0106), QStringLiteral("c")},  // Ć
        {QChar(0x0118), QStringLiteral("e")},  // Ę
        {QChar(0x0141), QStringLiteral("l")},  // Ł
        {QChar(0x0143), QStringLiteral("n")},  // Ń
        {QChar(0x00D3), QStringLiteral("o")},  // Ó
        {QChar(0x015A), QStringLiteral("s")},  // Ś
        {QChar(0x0179), QStringLiteral("z")},  // Ź
        {QChar(0x017B), QStringLiteral("z")},  // Ż
        // Common special characters with readable mappings
        {QLatin1Char('@'), QStringLiteral("_at_")},
        {QLatin1Char('.'), QStringLiteral("_dot_")},
        {QLatin1Char(':'), QStringLiteral("_colon_")},
        {QLatin1Char(' '), QStringLiteral("_")},
        {QLatin1Char('('), QStringLiteral("_")},
        {QLatin1Char(')'), QStringLiteral("_")},
        {QLatin1Char('-'), QStringLiteral("_")},
        {QLatin1Char('+'), QStringLiteral("_plus_")},
        {QLatin1Char('='), QStringLiteral("_eq_")},
        {QLatin1Char('/'), QStringLiteral("_slash_")},
        {QLatin1Char('\\'), QStringLiteral("_backslash_")},
        {QLatin1Char('&'), QStringLiteral("_and_")},
        {QLatin1Char('%'), QStringLiteral("_percent_")},
        {QLatin1Char('#'), QStringLiteral("_hash_")},
        {QLatin1Char('!'), QStringLiteral("_excl_")},
        {QLatin1Char('?'), QStringLiteral("_q_")},
        {QLatin1Char('*'), QStringLiteral("_star_")},
        {QLatin1Char(','), QStringLiteral("_")},
        {QLatin1Char(';'), QStringLiteral("_")},
        {QLatin1Char('\''), QStringLiteral("_")},
        {QLatin1Char('"'), QStringLiteral("_")},
        {QLatin1Char('['), QStringLiteral("_")},
        {QLatin1Char(']'), QStringLiteral("_")},
        {QLatin1Char('{'), QStringLiteral("_")},
        {QLatin1Char('}'), QStringLiteral("_")},
        {QLatin1Char('<'), QStringLiteral("_lt_")},
        {QLatin1Char('>'), QStringLiteral("_gt_")},
        {QLatin1Char('|'), QStringLiteral("_pipe_")},
        {QLatin1Char('~'), QStringLiteral("_tilde_")},
        {QLatin1Char('`'), QStringLiteral("_")},
    };

    QString encoded;
    encoded.reserve(credentialName.length() * 3); // Reserve space for worst case

    for (const QChar &ch : credentialName) {
        // Check if it's ASCII letter or number or underscore
        if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) ||
            (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) ||
            (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
            ch == QLatin1Char('_')) {
            // Keep ASCII alphanumeric and underscore as-is (lowercase)
            encoded.append(ch.toLower());
        } else if (translitMap.contains(ch)) {
            // Use transliteration mapping
            encoded.append(translitMap.value(ch));
        } else if (ch.unicode() < 128) {
            // Other ASCII characters not in map - replace with underscore
            encoded.append(QLatin1Char('_'));
        } else {
            // Unicode character not in map - encode as _uXXXX
            encoded.append(QString::fromLatin1("_u%1").arg(
                static_cast<ushort>(ch.unicode()), 4, 16, QLatin1Char('0')));
        }
    }

    // If starts with digit, prepend 'c'
    if (!encoded.isEmpty() && encoded[0].isDigit()) {
        encoded.prepend(QLatin1Char('c'));
    }

    // Truncate if too long (max 255 chars for D-Bus element)
    if (encoded.length() > 200) {
        // Use hash for very long names
        const QByteArray hash = QCryptographicHash::hash(credentialName.toUtf8(),
                                                          QCryptographicHash::Sha256);
        encoded = QString::fromLatin1("cred_") + QString::fromLatin1(hash.toHex().left(16));
    }

    return encoded;
}

QString OathDeviceObject::credentialPath(const QString &credentialId) const
{
    return QString::fromLatin1("%1/credentials/%2").arg(m_objectPath, credentialId);
}

void OathDeviceObject::emitPropertyChanged(const QString &propertyName, const QVariant &value)
{
    if (!m_registered) {
        return;
    }

    // Create PropertiesChanged signal
    // Signature: PropertiesChanged(interface_name, changed_properties, invalidated_properties)
    QDBusMessage signal = QDBusMessage::createSignal(
        m_objectPath,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged")
    );

    // Prepare arguments
    QVariantMap changedProperties;
    changedProperties.insert(propertyName, value);

    const QStringList invalidatedProperties; // Empty - we provide values directly

    signal << QStringLiteral("pl.jkolo.yubikey.oath.Device")
           << changedProperties
           << invalidatedProperties;

    // Send signal
    if (!m_connection.send(signal)) {
        qCWarning(YubiKeyDaemonLog) << "Failed to emit PropertiesChanged for"
                                     << propertyName << "on" << m_objectPath;
    } else {
        qCDebug(YubiKeyDaemonLog) << "Emitted PropertiesChanged:" << propertyName
                                   << "=" << value << "on" << m_objectPath;
    }
}

} // namespace Daemon
} // namespace YubiKeyOath
