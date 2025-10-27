/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "add_credential_dialog.h"
#include "processing_overlay.h"
#include "../utils/screenshot_capture.h"
#include "../utils/qr_code_parser.h"
#include "../utils/otpauth_uri_parser.h"
#include "../logging_categories.h"
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
#include <KMessageWidget>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QIcon>
#include <KLocalizedString>

namespace YubiKeyOath {
namespace Daemon {
using Shared::OathCredentialData;
using Shared::OathAlgorithm;
using Shared::OathType;
using Shared::Result;

AddCredentialDialog::AddCredentialDialog(const OathCredentialData &initialData,
                                        const QStringList &availableDevices,
                                        const QString &preselectedDeviceId,
                                        QWidget *parent)
    : QDialog(parent)
    , m_screenshotCapture(nullptr)
    , m_secretRevealed(false)
{
    setWindowTitle(i18n("Add OATH Credential to YubiKey"));

    // Try to load icon from resources, fallback to theme icon
    QIcon windowIcon(QStringLiteral(":/icons/yubikey.svg"));
    if (windowIcon.isNull()) {
        windowIcon = QIcon::fromTheme(QStringLiteral("security-medium"));
    }
    setWindowIcon(windowIcon);

    setMinimumWidth(500);

    setupUi(initialData, availableDevices);

    // Preselect device if specified
    if (!preselectedDeviceId.isEmpty() && m_deviceCombo->isEnabled()) {
        int const index = m_deviceCombo->findData(preselectedDeviceId);
        if (index >= 0) {
            m_deviceCombo->setCurrentIndex(index);
        }
    }

    // Create processing overlay for visual feedback
    m_processingOverlay = new ProcessingOverlay(this);
}

void AddCredentialDialog::setupUi(const OathCredentialData &initialData,
                                  const QStringList &devices)
{
    auto *mainLayout = new QVBoxLayout(this);

    // Message widget for errors/info (hidden by default)
    m_messageWidget = new KMessageWidget(this);
    m_messageWidget->setCloseButtonVisible(true);
    m_messageWidget->setWordWrap(true);
    m_messageWidget->hide();
    mainLayout->addWidget(m_messageWidget);

    // Scan QR button at the top
    m_scanQrButton = new QPushButton(QIcon::fromTheme(QStringLiteral("scanner")), i18n("Scan QR Code from Screen"), this);
    m_scanQrButton->setToolTip(i18n("Capture screenshot and scan QR code"));
    mainLayout->addWidget(m_scanQrButton);

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
    int const algoIndex = initialData.algorithm == OathAlgorithm::SHA256 ? 1 :
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
    connect(m_scanQrButton, &QPushButton::clicked,
            this, &AddCredentialDialog::onScanQrClicked);
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
    bool const isTotp = m_typeCombo->currentData().value<OathType>() == OathType::TOTP;

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
    QString const issuer = m_issuerField->text().trimmed();
    if (issuer.isEmpty()) {
        m_errorLabel->setText(i18n("Issuer is required"));
        m_errorLabel->show();
        m_issuerField->setFocus();
        return false;
    }

    // Validate account
    QString const account = m_accountField->text().trimmed();
    if (account.isEmpty()) {
        m_errorLabel->setText(i18n("Account is required"));
        m_errorLabel->show();
        m_accountField->setFocus();
        return false;
    }

    // Validate secret
    QString const secret = m_secretField->text().trimmed();
    if (secret.isEmpty()) {
        m_errorLabel->setText(i18n("Secret is required"));
        m_errorLabel->show();
        m_secretField->setFocus();
        return false;
    }

    // Build and validate credential data
    OathCredentialData const data = getCredentialData();
    QString const validationError = data.validate();
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
        return {};
    }
    return m_deviceCombo->currentData().toString();
}

void AddCredentialDialog::onScanQrClicked()
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Scan QR button clicked";

    // Show overlay with initial status (UI thread)
    showProcessingOverlay(i18n("Scanning screen"));

    // Create ScreenshotCapture in UI thread (QDBusInterface requires event loop)
    
        delete m_screenshotCapture;
    
    m_screenshotCapture = new ScreenshotCapture(this);

    // Connect signals for async screenshot handling
    connect(m_screenshotCapture, &ScreenshotCapture::screenshotCaptured,
            this, &AddCredentialDialog::onScreenshotCaptured);
    connect(m_screenshotCapture, &ScreenshotCapture::screenshotCancelled,
            this, &AddCredentialDialog::onScreenshotCancelled);

    // Start screenshot capture (async, UI thread)
    auto result = m_screenshotCapture->captureInteractive(30000);

    // Handle immediate errors (e.g., Spectacle not available)
    if (result.isError()) {
        qCWarning(YubiKeyDaemonLog) << "AddCredentialDialog: Screenshot capture failed immediately:" << result.error();
        hideProcessingOverlay();
        showMessage(result.error(), KMessageWidget::Error);
        delete m_screenshotCapture;
        m_screenshotCapture = nullptr;
    }
}

void AddCredentialDialog::fillFieldsFromQrData(const OathCredentialData &data)
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Filling fields from QR data";

    // Fill text fields
    m_issuerField->setText(data.issuer);
    m_accountField->setText(data.account);
    m_secretField->setText(data.secret);

    // Set type
    m_typeCombo->setCurrentIndex(data.type == OathType::TOTP ? 0 : 1);

    // Set algorithm
    int const algoIndex = data.algorithm == OathAlgorithm::SHA256 ? 1 :
                   data.algorithm == OathAlgorithm::SHA512 ? 2 : 0;
    m_algorithmCombo->setCurrentIndex(algoIndex);

    // Set numeric fields
    m_digitsSpinBox->setValue(data.digits);
    m_periodSpinBox->setValue(data.period);
    m_counterSpinBox->setValue(static_cast<int>(data.counter));
    m_touchCheckBox->setChecked(data.requireTouch);

    // Update field visibility based on type
    updateFieldsForType();
}

void AddCredentialDialog::showProcessingOverlay(const QString &message)
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Showing processing overlay:" << message;
    m_processingOverlay->show(message);
}

void AddCredentialDialog::onScreenshotCaptured(const QString &filePath)
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Screenshot captured:" << filePath;

    // Update overlay status to QR parsing
    updateOverlayStatus(i18n("Processing QR code"));

    // Run QR parsing + URI parsing in background thread (CPU-heavy)
    QFuture<Result<OathCredentialData>> const future = QtConcurrent::run([filePath]() -> Result<OathCredentialData> {
        qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Background QR parsing started";

        // Parse QR code from image (static method - thread-safe)
        auto qrResult = QrCodeParser::parse(filePath);

        if (qrResult.isError()) {
            qCWarning(YubiKeyDaemonLog) << "AddCredentialDialog: QR parsing failed:" << qrResult.error();
            return Result<OathCredentialData>::error(i18n("No QR code found in the screenshot. Please try again."));
        }

        QString const otpauthUri = qrResult.value();
        qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: QR code parsed, URI length:" << otpauthUri.length();

        // Parse otpauth URI (static method - thread-safe)
        auto parseResult = OtpauthUriParser::parse(otpauthUri);

        if (parseResult.isError()) {
            qCWarning(YubiKeyDaemonLog) << "AddCredentialDialog: URI parsing failed:" << parseResult.error();
            return Result<OathCredentialData>::error(parseResult.error());
        }

        qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Credential data parsed successfully";
        return parseResult;
    });

    // Use QFutureWatcher to get notified when QR parsing is done (in UI thread)
    auto *watcher = new QFutureWatcher<Result<OathCredentialData>>(this);
    connect(watcher, &QFutureWatcher<Result<OathCredentialData>>::finished,
            this, [this, watcher]() {
        qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Background QR parsing finished";

        auto result = watcher->result();

        // Hide overlay (UI thread)
        hideProcessingOverlay();

        // Clean up screenshot capture
        if (m_screenshotCapture) {
            delete m_screenshotCapture;
            m_screenshotCapture = nullptr;
        }

        if (result.isError()) {
            qCWarning(YubiKeyDaemonLog) << "AddCredentialDialog: QR processing failed:" << result.error();
            showMessage(result.error(), KMessageWidget::Error);
        } else {
            // Fill form fields with parsed data (UI thread)
            fillFieldsFromQrData(result.value());
            showMessage(i18n("QR code scanned successfully. Please review the information below."),
                       KMessageWidget::Positive);
        }

        // Clean up watcher
        watcher->deleteLater();
    });

    // Start watching the future
    watcher->setFuture(future);
}

void AddCredentialDialog::onScreenshotCancelled()
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Screenshot cancelled by user";

    // Hide overlay
    hideProcessingOverlay();

    // Clean up screenshot capture
    if (m_screenshotCapture) {
        delete m_screenshotCapture;
        m_screenshotCapture = nullptr;
    }
}

void AddCredentialDialog::hideProcessingOverlay()
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Hiding processing overlay";
    m_processingOverlay->hide();
}

void AddCredentialDialog::updateOverlayStatus(const QString &message)
{
    qCDebug(YubiKeyDaemonLog) << "AddCredentialDialog: Updating overlay status:" << message;
    m_processingOverlay->updateStatus(message);
}

void AddCredentialDialog::showMessage(const QString &text, int messageType)
{
    m_messageWidget->setText(text);
    m_messageWidget->setMessageType(static_cast<KMessageWidget::MessageType>(messageType));
    m_messageWidget->animatedShow();
}

} // namespace Daemon
} // namespace YubiKeyOath
