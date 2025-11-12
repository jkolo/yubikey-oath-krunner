#pragma once

#include "ui_yubikey_config.h"
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

class YubiKeyDeviceModel;

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
    YubiKeyConfigForm *m_ui;
    KConfigGroup m_config;
    OathManagerProxy *m_manager;  // Singleton - not owned
    std::unique_ptr<YubiKeyDeviceModel> m_deviceModel;
};
} // namespace Config
} // namespace YubiKeyOath
