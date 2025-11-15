#pragma once

#include "ui_oath_config.h"
#include "../shared/types/yubikey_model.h"
#include <KCModule>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QWidget>
#include <memory>

namespace YubiKeyOath {
namespace Shared {
class OathManagerProxy;
}

namespace Config {
using Shared::OathManagerProxy;

class OathDeviceListModel;

class OathConfigForm : public QWidget, public Ui::OathConfigForm
{
    Q_OBJECT

public:
    explicit OathConfigForm(QWidget *parent) : QWidget(parent) {
        setupUi(this);
    }
};

class OathConfig : public KCModule
{
    Q_OBJECT

public:
    explicit OathConfig(QObject *parent, const QVariantList &args);
    ~OathConfig() override;

    /**
     * @brief Resolves model-specific icon path for QML
     * @param deviceModel Encoded model (0xSSVVPPFF)
     * @return Qt resource path to model-specific icon
     *
     * This method is exposed to QML to allow dynamic icon selection
     * based on YubiKey model in device list.
     */
    Q_INVOKABLE QString getModelIcon(quint32 deviceModel) const;

public Q_SLOTS:
    void load() override;
    void save() override;
    void defaults() override;

private Q_SLOTS:
    void markAsChanged();
    void validateOptions();

private:
    OathConfigForm *m_ui;
    KConfigGroup m_config;
    OathManagerProxy *m_manager;  // Singleton - not owned
    std::unique_ptr<OathDeviceListModel> m_deviceModel;
};
} // namespace Config
} // namespace YubiKeyOath
