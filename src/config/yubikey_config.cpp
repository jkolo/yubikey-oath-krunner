#include "yubikey_config.h"
#include "logging_categories.h"
#include "../shared/dbus/yubikey_dbus_client.h"
#include "yubikey_device_model.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>
#include <KLocalizedContext>
#include <QMessageBox>
#include <QWidget>
#include <QGridLayout>
#include <QVariantList>
#include <QDebug>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWidget>
#include <QUrl>

// Forward declare Qt resource initialization
extern void qInitResources_shared();
extern void qInitResources_config();

namespace KRunner {
namespace YubiKey {

YubiKeyConfig::YubiKeyConfig(QObject *parent, const QVariantList &)
    : KCModule(qobject_cast<QWidget *>(parent))
    , m_dbusClient(std::make_unique<YubiKeyDBusClient>(this))
{
    // Initialize Qt resources (QML files, icons)
    qInitResources_shared();
    qInitResources_config();

    m_ui = new YubiKeyConfigForm(widget());
    auto *layout = new QGridLayout(widget());
    layout->addWidget(m_ui, 0, 0);

    // Load config
    auto config = KSharedConfig::openConfig(QStringLiteral("krunnerrc"));
    m_config = config->group(QStringLiteral("Runners")).group(QStringLiteral("krunner_yubikey"));

    // Create device model using D-Bus client - NO Qt parent, managed solely by unique_ptr
    // This prevents double-deletion (unique_ptr + Qt parent-child)
    m_deviceModel = std::make_unique<YubiKeyDeviceModel>(
        m_dbusClient.get(),
        nullptr  // No Qt parent - sole ownership by unique_ptr
    );

    // Setup QML widget
    if (m_ui->qmlWidget) {
        qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: Setting up QML widget";

        // Use default background color (not transparent)
        // The QML will set its own background color to match theme

        // Set up i18n support for QML
        // KLocalizedContext ownership is transferred to QML engine's root context
        auto *localizedContext = new KLocalizedContext(m_ui->qmlWidget->engine());
        m_ui->qmlWidget->engine()->rootContext()->setContextObject(localizedContext);
        qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: KLocalizedContext set";

        // Expose device model to QML
        m_ui->qmlWidget->rootContext()->setContextProperty(
            QStringLiteral("deviceModel"),
            m_deviceModel.get()
        );
        qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: deviceModel exposed to QML";

        // Load QML file
        QUrl qmlUrl(QStringLiteral("qrc:/qml/config/YubiKeyConfig.qml"));
        qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: Loading QML from:" << qmlUrl;
        m_ui->qmlWidget->setSource(qmlUrl);

        // Check QML status
        qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: QML status:" << m_ui->qmlWidget->status();
        if (m_ui->qmlWidget->status() == QQuickWidget::Error) {
            qCWarning(YubiKeyConfigLog) << "YubiKeyConfig: QML errors:" << m_ui->qmlWidget->errors();
        } else {
            qCDebug(YubiKeyConfigLog) << "YubiKeyConfig: QML loaded successfully";
        }
    } else {
        qCWarning(YubiKeyConfigLog) << "YubiKeyConfig: qmlWidget is null!";
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

    QString primaryAction = m_config.readEntry("PrimaryAction", "copy");
    int primaryIndex = m_ui->primaryActionCombo->findData(primaryAction);
    if (primaryIndex >= 0) {
        m_ui->primaryActionCombo->setCurrentIndex(primaryIndex);
    }

    m_ui->touchTimeoutSpinbox->setValue(m_config.readEntry("TouchTimeout", 10));
    m_ui->notificationExtraTimeSpinbox->setValue(m_config.readEntry("NotificationExtraTime", 15));

    // Set initial enabled state for dependent checkbox
    m_ui->showDeviceNameOnlyWhenMultipleCheckbox->setEnabled(m_ui->showDeviceNameCheckbox->isChecked());

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

    QString primaryAction = m_ui->primaryActionCombo->itemData(m_ui->primaryActionCombo->currentIndex()).toString();
    qCDebug(YubiKeyConfigLog) << "Saving PrimaryAction:" << primaryAction;
    m_config.writeEntry("PrimaryAction", primaryAction);

    m_config.writeEntry("TouchTimeout", m_ui->touchTimeoutSpinbox->value());
    m_config.writeEntry("NotificationExtraTime", m_ui->notificationExtraTimeSpinbox->value());

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

    // Set initial enabled state for dependent checkbox
    m_ui->showDeviceNameOnlyWhenMultipleCheckbox->setEnabled(m_ui->showDeviceNameCheckbox->isChecked());

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

} // namespace YubiKey
} // namespace KRunner

#include <KPluginFactory>

// Must use unqualified name for K_PLUGIN_CLASS - MOC doesn't support namespaced names
using KRunner::YubiKey::YubiKeyConfig;
K_PLUGIN_CLASS(YubiKeyConfig)

#include "yubikey_config.moc"
