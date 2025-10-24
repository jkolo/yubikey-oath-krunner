#pragma once

#include "ui_yubikey_config.h"
#include <KCModule>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QWidget>
#include <memory>

namespace KRunner {
namespace YubiKey {

class YubiKeyDBusClient;
class YubiKeyDeviceModel;
class PortalPermissionManager;

class YubiKeyConfigForm : public QWidget, public Ui::YubiKeyConfigForm
{
    Q_OBJECT

public:
    explicit YubiKeyConfigForm(QWidget *parent) : QWidget(parent) {
        setupUi(this);
    }
};

class YubiKeyConfig : public KCModule
{
    Q_OBJECT

public:
    explicit YubiKeyConfig(QObject *parent, const QVariantList &args);
    ~YubiKeyConfig() override;

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

private Q_SLOTS:
    void markAsChanged();
    void validateOptions();
    void onScreenshotPermissionChanged(bool enabled);
    void onRemoteDesktopPermissionChanged(bool enabled);

private:
    void loadPortalPermissions();

    YubiKeyConfigForm *m_ui;
    KConfigGroup m_config;
    std::unique_ptr<YubiKeyDBusClient> m_dbusClient;
    std::unique_ptr<YubiKeyDeviceModel> m_deviceModel;
    std::unique_ptr<PortalPermissionManager> m_permissionManager;
};
} // namespace YubiKey
} // namespace KRunner
