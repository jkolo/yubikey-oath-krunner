/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "add_credential_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <KLocalizedString>

namespace KRunner {
namespace YubiKey {

AddCredentialDialog::AddCredentialDialog(const OathCredentialData &initialData,
                                        const QStringList &availableDevices,
                                        QWidget *parent)
    : QDialog(parent)
    , m_secretRevealed(false)
{
    setWindowTitle(i18n("Add OATH Credential to YubiKey"));
    setMinimumWidth(500);
    setupUi(initialData, availableDevices);
}

void AddCredentialDialog::setupUi(const OathCredentialData &initialData,
                                  const QStringList &devices)
{
    auto *mainLayout = new QVBoxLayout(this);

    // Form layout for credential fields
    auto *formLayout = new QFormLayout();
    formLayout->setSpacing(12);

    // Issuer field
    m_issuerField = new QLineEdit(initialData.issuer, this);
    m_issuerField->setPlaceholderText(i18n("e.g., Google, GitHub"));
    formLayout->addRow(i18n("Issuer:"), m_issuerField);

    // Account field
    m_accountField = new QLineEdit(initialData.account, this);
    m_accountField->setPlaceholderText(i18n("e.g., user@example.com"));
    formLayout->addRow(i18n("Account:"), m_accountField);

    // Secret field (password mode initially)
    auto *secretLayout = new QHBoxLayout();
    m_secretField = new QLineEdit(initialData.secret, this);
    m_secretField->setEchoMode(QLineEdit::Password);
    m_secretField->setPlaceholderText(i18n("Base32 encoded secret"));
    m_revealSecretButton = new QPushButton(QIcon::fromTheme(QStringLiteral("visibility")), QString(), this);
    m_revealSecretButton->setToolTip(i18n("Show/hide secret"));
    m_revealSecretButton->setMaximumWidth(40);
    secretLayout->addWidget(m_secretField);
    secretLayout->addWidget(m_revealSecretButton);
    formLayout->addRow(i18n("Secret:"), secretLayout);

    // Type combo
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem(i18n("TOTP (Time-based)"), QVariant::fromValue(OathType::TOTP));
    m_typeCombo->addItem(i18n("HOTP (Counter-based)"), QVariant::fromValue(OathType::HOTP));
    m_typeCombo->setCurrentIndex(initialData.type == OathType::TOTP ? 0 : 1);
    formLayout->addRow(i18n("Type:"), m_typeCombo);

    // Algorithm combo
    m_algorithmCombo = new QComboBox(this);
    m_algorithmCombo->addItem(QStringLiteral("SHA1"), QVariant::fromValue(OathAlgorithm::SHA1));
    m_algorithmCombo->addItem(QStringLiteral("SHA256"), QVariant::fromValue(OathAlgorithm::SHA256));
    m_algorithmCombo->addItem(QStringLiteral("SHA512"), QVariant::fromValue(OathAlgorithm::SHA512));
    int algoIndex = initialData.algorithm == OathAlgorithm::SHA256 ? 1 :
                   initialData.algorithm == OathAlgorithm::SHA512 ? 2 : 0;
    m_algorithmCombo->setCurrentIndex(algoIndex);
    formLayout->addRow(i18n("Algorithm:"), m_algorithmCombo);

    // Digits spinbox
    m_digitsSpinBox = new QSpinBox(this);
    m_digitsSpinBox->setRange(6, 8);
    m_digitsSpinBox->setValue(initialData.digits);
    formLayout->addRow(i18n("Digits:"), m_digitsSpinBox);

    // Period spinbox (TOTP only)
    m_periodSpinBox = new QSpinBox(this);
    m_periodSpinBox->setRange(1, 300);
    m_periodSpinBox->setValue(initialData.period);
    m_periodSpinBox->setSuffix(i18n(" seconds"));
    formLayout->addRow(i18n("Period:"), m_periodSpinBox);

    // Counter spinbox (HOTP only)
    m_counterSpinBox = new QSpinBox(this);
    m_counterSpinBox->setRange(0, 999999);
    m_counterSpinBox->setValue(static_cast<int>(initialData.counter));
    formLayout->addRow(i18n("Initial Counter:"), m_counterSpinBox);

    // Touch checkbox
    m_touchCheckBox = new QCheckBox(i18n("Require physical touch to generate code"), this);
    m_touchCheckBox->setChecked(initialData.requireTouch);
    formLayout->addRow(QString(), m_touchCheckBox);

    // Device selection (if multiple)
    m_deviceCombo = new QComboBox(this);
    if (devices.isEmpty()) {
        m_deviceCombo->addItem(i18n("No devices available"));
        m_deviceCombo->setEnabled(false);
    } else {
        for (const QString &deviceId : devices) {
            m_deviceCombo->addItem(deviceId, deviceId);
        }
    }
    formLayout->addRow(i18n("Target Device:"), m_deviceCombo);

    mainLayout->addLayout(formLayout);

    // Error label
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("QLabel { color: red; font-weight: bold; }"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->hide();
    mainLayout->addWidget(m_errorLabel);

    // Button box
    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        this
    );
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    m_okButton->setText(i18n("Add to YubiKey"));
    m_okButton->setDefault(true);

    mainLayout->addWidget(buttonBox);

    // Connections
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AddCredentialDialog::onTypeChanged);
    connect(m_revealSecretButton, &QPushButton::clicked,
            this, &AddCredentialDialog::onRevealSecretClicked);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &AddCredentialDialog::onOkClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Initial field visibility
    updateFieldsForType();
}

void AddCredentialDialog::onTypeChanged(int index)
{
    Q_UNUSED(index)
    updateFieldsForType();
}

void AddCredentialDialog::updateFieldsForType()
{
    bool isTotp = m_typeCombo->currentData().value<OathType>() == OathType::TOTP;

    // Show/hide fields based on type
    m_periodSpinBox->setVisible(isTotp);
    m_counterSpinBox->setVisible(!isTotp);

    // Update form labels visibility
    auto *formLayout = qobject_cast<QFormLayout*>(m_periodSpinBox->parent());
    if (formLayout) {
        formLayout->labelForField(m_periodSpinBox)->setVisible(isTotp);
        formLayout->labelForField(m_counterSpinBox)->setVisible(!isTotp);
    }
}

void AddCredentialDialog::onRevealSecretClicked()
{
    m_secretRevealed = !m_secretRevealed;
    m_secretField->setEchoMode(m_secretRevealed ? QLineEdit::Normal : QLineEdit::Password);
    m_revealSecretButton->setIcon(QIcon::fromTheme(
        m_secretRevealed ? QStringLiteral("hint") : QStringLiteral("visibility")));
}

void AddCredentialDialog::onOkClicked()
{
    if (validateAndBuildData()) {
        accept();
    }
}

bool AddCredentialDialog::validateAndBuildData()
{
    // Clear previous error
    m_errorLabel->hide();

    // Validate issuer
    QString issuer = m_issuerField->text().trimmed();
    if (issuer.isEmpty()) {
        m_errorLabel->setText(i18n("Issuer is required"));
        m_errorLabel->show();
        m_issuerField->setFocus();
        return false;
    }

    // Validate account
    QString account = m_accountField->text().trimmed();
    if (account.isEmpty()) {
        m_errorLabel->setText(i18n("Account is required"));
        m_errorLabel->show();
        m_accountField->setFocus();
        return false;
    }

    // Validate secret
    QString secret = m_secretField->text().trimmed();
    if (secret.isEmpty()) {
        m_errorLabel->setText(i18n("Secret is required"));
        m_errorLabel->show();
        m_secretField->setFocus();
        return false;
    }

    // Build and validate credential data
    OathCredentialData data = getCredentialData();
    QString validationError = data.validate();
    if (!validationError.isEmpty()) {
        m_errorLabel->setText(validationError);
        m_errorLabel->show();
        return false;
    }

    return true;
}

OathCredentialData AddCredentialDialog::getCredentialData() const
{
    OathCredentialData data;

    data.issuer = m_issuerField->text().trimmed();
    data.account = m_accountField->text().trimmed();
    data.name = data.issuer + QStringLiteral(":") + data.account;
    data.secret = m_secretField->text().trimmed();
    data.type = m_typeCombo->currentData().value<OathType>();
    data.algorithm = m_algorithmCombo->currentData().value<OathAlgorithm>();
    data.digits = m_digitsSpinBox->value();
    data.period = m_periodSpinBox->value();
    data.counter = static_cast<quint32>(m_counterSpinBox->value());
    data.requireTouch = m_touchCheckBox->isChecked();

    return data;
}

QString AddCredentialDialog::getSelectedDeviceId() const
{
    if (m_deviceCombo->count() == 0 || !m_deviceCombo->isEnabled()) {
        return QString();
    }
    return m_deviceCombo->currentData().toString();
}

} // namespace YubiKey
} // namespace KRunner
