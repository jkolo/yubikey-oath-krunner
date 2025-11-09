#include "yubikey_config.h"
#include "logging_categories.h"
#include "dbus/yubikey_manager_proxy.h"
#include "yubikey_device_model.h"
#include "device_delegate.h"
#include "yubikey_config_icon_resolver.h"
#include "../shared/utils/yubikey_icon_resolver.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>
#include <QMessageBox>
#include <QWidget>
#include <QGridLayout>
#include <QVariantList>
#include <QDebug>

// Forward declare Qt resource initialization functions
extern void qInitResources_shared();

namespace YubiKeyOath {
namespace Config {
using namespace YubiKeyOath::Shared;

YubiKeyConfig::YubiKeyConfig(QObject *parent, const QVariantList &)
    : KCModule(qobject_cast<QWidget *>(parent))
    , m_ui(new YubiKeyConfigForm(widget()))
    , m_manager(YubiKeyManagerProxy::instance(this))
{
    // Set translation domain for i18n
    KLocalizedString::setApplicationDomain("yubikey_oath");

    // Initialize Qt resources (icons)
    qInitResources_shared();

    auto *layout = new QGridLayout(widget());
    layout->addWidget(m_ui, 0, 0);

    // Load config
    auto config = KSharedConfig::openConfig(QStringLiteral("yubikey-oathrc"));
    m_config = config->group(QStringLiteral("General"));

    // Create device model using manager proxy - NO Qt parent, managed solely by unique_ptr
    // This prevents double-deletion (unique_ptr + Qt parent-child)
    m_deviceModel = std::make_unique<YubiKeyDeviceModel>(
        m_manager,
        nullptr  // No Qt parent - sole ownership by unique_ptr
    );

    // Setup device list view with custom delegate
    if (m_ui->deviceListView) {
        qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: Setting up device list view";

        // Create icon resolver adapter and delegate
        auto *iconResolver = new YubiKeyConfigIconResolver(this);
        auto *delegate = new DeviceDelegate(iconResolver, this);
        m_ui->deviceListView->setModel(m_deviceModel.get());
        m_ui->deviceListView->setItemDelegate(delegate);

        // Enable mouse tracking for hover effects
        m_ui->deviceListView->setMouseTracking(true);
        m_ui->deviceListView->viewport()->setMouseTracking(true);

        // Connect delegate signals to model methods
        connect(delegate, &DeviceDelegate::authorizeClicked,
                m_deviceModel.get(), &YubiKeyDeviceModel::showPasswordDialog);
        connect(delegate, &DeviceDelegate::changePasswordClicked,
                m_deviceModel.get(), &YubiKeyDeviceModel::showChangePasswordDialog);
        connect(delegate, &DeviceDelegate::forgetClicked,
                m_deviceModel.get(), &YubiKeyDeviceModel::forgetDevice);

        // Connect name edit signal to start editing
        connect(delegate, &DeviceDelegate::nameEditRequested,
                this, [this](const QModelIndex &index) {
                    m_ui->deviceListView->edit(index);
                });

        qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: Device list view configured successfully";
    } else {
        qCWarning(YubiKeyConfigLog) << "YubiKeyConfig: deviceListView is null!";
    }

    // Setup ComboBox user data programmatically (Qt Designer userData may not load correctly)
    // Primary Action ComboBox
    m_ui->primaryActionCombo->setItemData(0, QStringLiteral("copy"));
    m_ui->primaryActionCombo->setItemData(1, QStringLiteral("type"));

    qCDebug(YubiKeyConfigLog) << "ComboBox userData set programmatically";

    // Connect UI signals
    connect(m_ui->showNotificationsCheckbox, &QCheckBox::toggled,
            this, &YubiKeyConfig::markAsChanged);
    connect(m_ui->showUsernameCheckbox, &QCheckBox::toggled,
            this, &YubiKeyConfig::markAsChanged);
    connect(m_ui->showCodeCheckbox, &QCheckBox::toggled,
            this, &YubiKeyConfig::markAsChanged);
    connect(m_ui->showDeviceNameCheckbox, &QCheckBox::toggled,
            this, &YubiKeyConfig::markAsChanged);
    connect(m_ui->showDeviceNameOnlyWhenMultipleCheckbox, &QCheckBox::toggled,
            this, &YubiKeyConfig::markAsChanged);
    connect(m_ui->primaryActionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &YubiKeyConfig::markAsChanged);
    connect(m_ui->touchTimeoutSpinbox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &YubiKeyConfig::markAsChanged);
    connect(m_ui->notificationExtraTimeSpinbox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &YubiKeyConfig::markAsChanged);
    connect(m_ui->enableCredentialsCacheCheckbox, &QCheckBox::toggled,
            this, &YubiKeyConfig::markAsChanged);
    connect(m_ui->deviceReconnectTimeoutSpinbox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &YubiKeyConfig::markAsChanged);
}

QString YubiKeyConfig::getModelIcon(quint32 deviceModel) const
{
    qCDebug(YubiKeyConfigLog) << "getModelIcon called with deviceModel:" << deviceModel << "(hex:" << Qt::hex << deviceModel << Qt::dec << ")";
    QString iconPath = YubiKeyIconResolver::getIconPath(deviceModel);
    qCDebug(YubiKeyConfigLog) << "getModelIcon returning iconPath:" << iconPath;
    return iconPath;
}

YubiKeyConfig::~YubiKeyConfig()
{
    qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: Destructor called";

    // Destroy device model BEFORE Qt destroys UI widget
    // This ensures QML doesn't access dangling pointers during widget destruction
    // unique_ptr has sole ownership (no Qt parent) so no double-deletion risk
    if (m_deviceModel) {
        qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: Destroying device model";
        m_deviceModel.reset();
    }

    // Don't delete m_ui - it has widget() as Qt parent and will be deleted automatically
    // Manual deletion would cause double-free since Qt also deletes children
    qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: Destructor complete (UI will be deleted by Qt parent)";
}

void YubiKeyConfig::load()
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

void YubiKeyConfig::save()
{
    // Save settings to config
    m_config.writeEntry("ShowNotifications", m_ui->showNotificationsCheckbox->isChecked());
    m_config.writeEntry("ShowUsername", m_ui->showUsernameCheckbox->isChecked());
    m_config.writeEntry("ShowCode", m_ui->showCodeCheckbox->isChecked());
    m_config.writeEntry("ShowDeviceName", m_ui->showDeviceNameCheckbox->isChecked());
    m_config.writeEntry("ShowDeviceNameOnlyWhenMultiple", m_ui->showDeviceNameOnlyWhenMultipleCheckbox->isChecked());

    const QString primaryAction = m_ui->primaryActionCombo->itemData(m_ui->primaryActionCombo->currentIndex()).toString();
    qCDebug(YubiKeyConfigLog) << "Saving PrimaryAction:" << primaryAction;
    m_config.writeEntry("PrimaryAction", primaryAction);

    m_config.writeEntry("TouchTimeout", m_ui->touchTimeoutSpinbox->value());
    m_config.writeEntry("NotificationExtraTime", m_ui->notificationExtraTimeSpinbox->value());
    m_config.writeEntry("EnableCredentialsCache", m_ui->enableCredentialsCacheCheckbox->isChecked());
    m_config.writeEntry("DeviceReconnectTimeout", m_ui->deviceReconnectTimeoutSpinbox->value());

    m_config.sync();
    setNeedsSave(false);
}

void YubiKeyConfig::defaults()
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

void YubiKeyConfig::markAsChanged()
{
    setNeedsSave(true);
}

void YubiKeyConfig::validateOptions()
{
    // Add any validation logic here if needed
}

} // namespace Config
} // namespace YubiKeyOath

#include <KPluginFactory>

// Must use unqualified name for K_PLUGIN_CLASS - MOC doesn't support namespaced names
using YubiKeyOath::Config::YubiKeyConfig;
K_PLUGIN_CLASS(YubiKeyConfig)

#include "yubikey_config.moc"
