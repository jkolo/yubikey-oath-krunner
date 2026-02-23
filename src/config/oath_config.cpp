#include "oath_config.h"
#include "logging_categories.h"
#include "dbus/oath_manager_proxy.h"
#include "oath_device_list_model.h"
#include "device_delegate.h"
#include "oath_config_icon_resolver.h"
#include "../shared/utils/yubikey_icon_resolver.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>
#include <QMessageBox>
#include <QWidget>
#include <QGridLayout>
#include <QVariantList>
#include <QDebug>

namespace YubiKeyOath {
namespace Config {
using namespace YubiKeyOath::Shared;

OathConfig::OathConfig(QObject *parent, const QVariantList &)
    : KCModule(qobject_cast<QWidget *>(parent))
    , m_ui(new OathConfigForm(widget()))
    , m_config(KSharedConfig::openConfig(QStringLiteral("yubikey-oathrc"))->group(QStringLiteral("General")))
    , m_manager(OathManagerProxy::instance(this))
    , m_deviceModel(std::make_unique<OathDeviceListModel>(OathManagerProxy::instance(this), nullptr))
{
    // Set translation domain for i18n
    KLocalizedString::setApplicationDomain("yubikey_oath");

    auto *layout = new QGridLayout(widget());
    layout->addWidget(m_ui, 0, 0);

    // Setup device list view with custom delegate
    if (m_ui->deviceListView) {
        qCDebug(OathConfigLog) << "OathConfig: Setting up device list view";

        // Create icon resolver adapter and delegate (delegate owns the resolver)
        auto *delegate = new DeviceDelegate(std::make_unique<OathConfigIconResolver>(), this);
        m_ui->deviceListView->setModel(m_deviceModel.get());
        m_ui->deviceListView->setItemDelegate(delegate);

        // Enable mouse tracking for hover effects
        m_ui->deviceListView->setMouseTracking(true);
        m_ui->deviceListView->viewport()->setMouseTracking(true);

        // Connect delegate signals to model methods
        connect(delegate, &DeviceDelegate::authorizeClicked,
                m_deviceModel.get(), &OathDeviceListModel::showPasswordDialog);
        connect(delegate, &DeviceDelegate::changePasswordClicked,
                m_deviceModel.get(), &OathDeviceListModel::showChangePasswordDialog);
        connect(delegate, &DeviceDelegate::forgetClicked,
                m_deviceModel.get(), &OathDeviceListModel::forgetDevice);

        // Connect name edit signal to start editing
        connect(delegate, &DeviceDelegate::nameEditRequested,
                this, [this](const QModelIndex &index) {
                    m_ui->deviceListView->edit(index);
                });

        qCDebug(OathConfigLog) << "OathConfig: Device list view configured successfully";
    } else {
        qCWarning(OathConfigLog) << "OathConfig: deviceListView is null!";
    }

    // Setup ComboBox user data programmatically (Qt Designer userData may not load correctly)
    // Primary Action ComboBox
    m_ui->primaryActionCombo->setItemData(0, QStringLiteral("copy"));
    m_ui->primaryActionCombo->setItemData(1, QStringLiteral("type"));

    qCDebug(OathConfigLog) << "ComboBox userData set programmatically";

    // Connect UI signals
    connect(m_ui->showNotificationsCheckbox, &QCheckBox::toggled,
            this, &OathConfig::markAsChanged);
    connect(m_ui->showUsernameCheckbox, &QCheckBox::toggled,
            this, &OathConfig::markAsChanged);
    connect(m_ui->showCodeCheckbox, &QCheckBox::toggled,
            this, &OathConfig::markAsChanged);
    connect(m_ui->showDeviceNameCheckbox, &QCheckBox::toggled,
            this, &OathConfig::markAsChanged);
    connect(m_ui->showDeviceNameOnlyWhenMultipleCheckbox, &QCheckBox::toggled,
            this, &OathConfig::markAsChanged);
    connect(m_ui->primaryActionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OathConfig::markAsChanged);
    connect(m_ui->touchTimeoutSpinbox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OathConfig::markAsChanged);
    connect(m_ui->notificationExtraTimeSpinbox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OathConfig::markAsChanged);
    connect(m_ui->enableCredentialsCacheCheckbox, &QCheckBox::toggled,
            this, &OathConfig::markAsChanged);
    connect(m_ui->deviceReconnectTimeoutSpinbox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &OathConfig::markAsChanged);
}

QString OathConfig::getModelIcon(quint32 deviceModel) const
{
    qCDebug(OathConfigLog) << "getModelIcon called with deviceModel:" << deviceModel << "(hex:" << Qt::hex << deviceModel << Qt::dec << ")";
    QString iconName = YubiKeyIconResolver::getIconName(deviceModel);
    qCDebug(OathConfigLog) << "getModelIcon returning iconName:" << iconName;
    return iconName;
}

OathConfig::~OathConfig()
{
    qCDebug(OathConfigLog) << "OathConfig: Destructor called";

    // Destroy device model BEFORE Qt destroys UI widget
    // This ensures QML doesn't access dangling pointers during widget destruction
    // unique_ptr has sole ownership (no Qt parent) so no double-deletion risk
    if (m_deviceModel) {
        qCDebug(OathConfigLog) << "OathConfig: Destroying device model";
        m_deviceModel.reset();
    }

    // Don't delete m_ui - it has widget() as Qt parent and will be deleted automatically
    // Manual deletion would cause double-free since Qt also deletes children
    qCDebug(OathConfigLog) << "OathConfig: Destructor complete (UI will be deleted by Qt parent)";
}

void OathConfig::load()
{
    // Load settings from config
    m_ui->showNotificationsCheckbox->setChecked(m_config.readEntry("ShowNotifications", true));
    m_ui->showUsernameCheckbox->setChecked(m_config.readEntry("ShowUsername", true));
    m_ui->showCodeCheckbox->setChecked(m_config.readEntry("ShowCode", false));
    m_ui->showDeviceNameCheckbox->setChecked(m_config.readEntry("ShowDeviceName", false));
    m_ui->showDeviceNameOnlyWhenMultipleCheckbox->setChecked(m_config.readEntry("ShowDeviceNameOnlyWhenMultiple", true));

    const QString primaryAction = m_config.readEntry("PrimaryAction", "copy");
    const int primaryIndex = m_ui->primaryActionCombo->findData(primaryAction);
    if (primaryIndex >= 0) {
        m_ui->primaryActionCombo->setCurrentIndex(primaryIndex);
    }

    m_ui->touchTimeoutSpinbox->setValue(m_config.readEntry("TouchTimeout", 10));
    m_ui->notificationExtraTimeSpinbox->setValue(m_config.readEntry("NotificationExtraTime", 15));
    m_ui->enableCredentialsCacheCheckbox->setChecked(m_config.readEntry("EnableCredentialsCache", false));
    m_ui->deviceReconnectTimeoutSpinbox->setValue(m_config.readEntry("DeviceReconnectTimeout", 30));

    // Set initial enabled state for dependent checkboxes/spinboxes
    m_ui->showDeviceNameOnlyWhenMultipleCheckbox->setEnabled(m_ui->showDeviceNameCheckbox->isChecked());
    m_ui->deviceReconnectTimeoutSpinbox->setEnabled(m_ui->enableCredentialsCacheCheckbox->isChecked());

    setNeedsSave(false);
}

void OathConfig::save()
{
    // Save settings to config
    m_config.writeEntry("ShowNotifications", m_ui->showNotificationsCheckbox->isChecked());
    m_config.writeEntry("ShowUsername", m_ui->showUsernameCheckbox->isChecked());
    m_config.writeEntry("ShowCode", m_ui->showCodeCheckbox->isChecked());
    m_config.writeEntry("ShowDeviceName", m_ui->showDeviceNameCheckbox->isChecked());
    m_config.writeEntry("ShowDeviceNameOnlyWhenMultiple", m_ui->showDeviceNameOnlyWhenMultipleCheckbox->isChecked());

    const QString primaryAction = m_ui->primaryActionCombo->itemData(m_ui->primaryActionCombo->currentIndex()).toString();
    qCDebug(OathConfigLog) << "Saving PrimaryAction:" << primaryAction;
    m_config.writeEntry("PrimaryAction", primaryAction);

    m_config.writeEntry("TouchTimeout", m_ui->touchTimeoutSpinbox->value());
    m_config.writeEntry("NotificationExtraTime", m_ui->notificationExtraTimeSpinbox->value());
    m_config.writeEntry("EnableCredentialsCache", m_ui->enableCredentialsCacheCheckbox->isChecked());
    m_config.writeEntry("DeviceReconnectTimeout", m_ui->deviceReconnectTimeoutSpinbox->value());

    m_config.sync();
    setNeedsSave(false);
}

void OathConfig::defaults()
{
    m_ui->showNotificationsCheckbox->setChecked(true);
    m_ui->showUsernameCheckbox->setChecked(true);
    m_ui->showCodeCheckbox->setChecked(false);
    m_ui->showDeviceNameCheckbox->setChecked(false);
    m_ui->showDeviceNameOnlyWhenMultipleCheckbox->setChecked(true);
    m_ui->primaryActionCombo->setCurrentIndex(0); // copy (first item)
    m_ui->touchTimeoutSpinbox->setValue(10);
    m_ui->notificationExtraTimeSpinbox->setValue(15);
    m_ui->enableCredentialsCacheCheckbox->setChecked(false);
    m_ui->deviceReconnectTimeoutSpinbox->setValue(30);

    // Set initial enabled state for dependent checkboxes/spinboxes
    m_ui->showDeviceNameOnlyWhenMultipleCheckbox->setEnabled(m_ui->showDeviceNameCheckbox->isChecked());
    m_ui->deviceReconnectTimeoutSpinbox->setEnabled(m_ui->enableCredentialsCacheCheckbox->isChecked());

    markAsChanged();
}

void OathConfig::markAsChanged()
{
    setNeedsSave(true);
}

void OathConfig::validateOptions()
{
    // Add any validation logic here if needed
}

} // namespace Config
} // namespace YubiKeyOath

#include <KPluginFactory>

// Must use unqualified name for K_PLUGIN_CLASS - MOC doesn't support namespaced names
using YubiKeyOath::Config::OathConfig;
K_PLUGIN_CLASS(OathConfig)

#include "oath_config.moc"
