/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "change_password_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <KLocalizedString>

namespace YubiKeyOath {
namespace Shared {

ChangePasswordDialog::ChangePasswordDialog(const QString &deviceId,
                                           const QString &deviceName,
                                           bool requiresPassword,
                                           QWidget *parent)
    : QDialog(parent)
    , m_deviceId(deviceId)
    , m_requiresPassword(requiresPassword)
    , m_deviceNameLabel(nullptr)
    , m_oldPasswordField(nullptr)
    , m_newPasswordField(nullptr)
    , m_confirmPasswordField(nullptr)
    , m_removePasswordCheckbox(nullptr)
    , m_errorLabel(nullptr)
    , m_messageWidget(nullptr)
    , m_okButton(nullptr)
    , m_progressBar(nullptr)
{
    setWindowTitle(i18n("Change YubiKey Password"));
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint);
    setModal(true);

    setupUi(deviceName);
}

void ChangePasswordDialog::setupUi(const QString &deviceName)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);  // Make dialog non-resizable

    // Header
    auto *headerLabel = new QLabel(i18n("Change password for YubiKey device:"), this);
    headerLabel->setWordWrap(true);
    headerLabel->setMinimumWidth(400);  // Set minimum width for dialog
    mainLayout->addWidget(headerLabel);

    // Device name (read-only, bold)
    m_deviceNameLabel = new QLabel(deviceName, this);
    m_deviceNameLabel->setWordWrap(true);
    m_deviceNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QFont boldFont = m_deviceNameLabel->font();
    boldFont.setBold(true);
    m_deviceNameLabel->setFont(boldFont);
    mainLayout->addWidget(m_deviceNameLabel);

    // Error message widget (KMessageWidget) - hidden by default
    m_messageWidget = new KMessageWidget(this);
    m_messageWidget->setMessageType(KMessageWidget::Error);
    m_messageWidget->setCloseButtonVisible(true);
    m_messageWidget->setWordWrap(true);
    m_messageWidget->hide();
    mainLayout->addWidget(m_messageWidget);

    // Form layout for password fields
    auto *formLayout = new QFormLayout();
    formLayout->setSpacing(10);

    // Current password
    m_oldPasswordField = new QLineEdit(this);
    m_oldPasswordField->setEchoMode(QLineEdit::Password);
    if (m_requiresPassword) {
        m_oldPasswordField->setPlaceholderText(i18n("Current YubiKey password"));
        formLayout->addRow(i18n("Current password:"), m_oldPasswordField);
    } else {
        m_oldPasswordField->setPlaceholderText(i18n("No password currently set"));
        m_oldPasswordField->setEnabled(false);  // Disable field for devices without password
        formLayout->addRow(i18n("Current password:"), m_oldPasswordField);
    }

    // New password
    m_newPasswordField = new QLineEdit(this);
    m_newPasswordField->setEchoMode(QLineEdit::Password);
    m_newPasswordField->setPlaceholderText(i18n("New password"));
    formLayout->addRow(i18n("New password:"), m_newPasswordField);

    // Confirm new password
    m_confirmPasswordField = new QLineEdit(this);
    m_confirmPasswordField->setEchoMode(QLineEdit::Password);
    m_confirmPasswordField->setPlaceholderText(i18n("Confirm new password"));
    formLayout->addRow(i18n("Confirm password:"), m_confirmPasswordField);

    mainLayout->addLayout(formLayout);

    // Remove password checkbox
    m_removePasswordCheckbox = new QCheckBox(i18n("Remove password protection"), this);
    m_removePasswordCheckbox->setToolTip(
        i18n("Check this to remove password protection from the YubiKey.\n"
             "When checked, the new password fields will be disabled.")
    );
    mainLayout->addWidget(m_removePasswordCheckbox);

    // Progress bar (hidden by default, shown during verification)
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);  // Indeterminate mode
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    // Error message label (hidden by default)
    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(QStringLiteral("QLabel { color: red; font-weight: bold; }"));
    m_errorLabel->hide();
    mainLayout->addWidget(m_errorLabel);

    // Button box
    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        this
    );
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    m_okButton->setText(i18n("Change Password"));
    m_okButton->setEnabled(false);
    m_okButton->setDefault(true);
    m_okButton->setAutoDefault(true);

    mainLayout->addWidget(buttonBox);

    // Connect signals
    connect(m_removePasswordCheckbox, &QCheckBox::toggled,
            this, &ChangePasswordDialog::onRemovePasswordToggled);

    // Enable OK button based on input state and password requirement
    auto updateOkButton = [this]() {
        bool hasOldPassword = !m_oldPasswordField->text().isEmpty();
        bool removeMode = m_removePasswordCheckbox->isChecked();
        bool hasNewPasswords = !m_newPasswordField->text().isEmpty() &&
                               !m_confirmPasswordField->text().isEmpty();
        bool passwordsAreMatching = passwordsMatch();

        // Old password required if device has password (always, regardless of operation)
        bool needsOldPassword = m_requiresPassword;

        // Enable OK button when:
        // - Remove mode: checkbox checked AND (no password required OR has old password)
        // - Set/Change mode: new passwords filled AND passwords match AND (no password required OR has old password)
        bool canProceed = removeMode
            ? (!needsOldPassword || hasOldPassword)
            : (hasNewPasswords && passwordsAreMatching && (!needsOldPassword || hasOldPassword));

        m_okButton->setEnabled(canProceed);

        // Hide error when user starts typing
        if (m_errorLabel->isVisible()) {
            m_errorLabel->setVisible(false);
        }
        if (m_messageWidget->isVisible()) {
            m_messageWidget->animatedHide();
        }
    };

    connect(m_oldPasswordField, &QLineEdit::textChanged, this, updateOkButton);
    connect(m_newPasswordField, &QLineEdit::textChanged, this, updateOkButton);
    connect(m_confirmPasswordField, &QLineEdit::textChanged, this, updateOkButton);
    connect(m_removePasswordCheckbox, &QCheckBox::toggled, this, updateOkButton);

    // Connect button box signals
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ChangePasswordDialog::onOkClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Set initial focus based on whether device requires password
    if (m_requiresPassword) {
        m_oldPasswordField->setFocus();
    } else {
        m_newPasswordField->setFocus();
    }
}

void ChangePasswordDialog::onRemovePasswordToggled(bool checked)
{
    // Disable new password fields when "Remove password" is checked
    m_newPasswordField->setEnabled(!checked);
    m_confirmPasswordField->setEnabled(!checked);

    if (checked) {
        m_newPasswordField->clear();
        m_confirmPasswordField->clear();
        m_newPasswordField->setPlaceholderText(i18n("(password will be removed)"));
        m_confirmPasswordField->setPlaceholderText(i18n("(password will be removed)"));
        m_okButton->setText(i18n("Remove Password"));
    } else {
        m_newPasswordField->setPlaceholderText(i18n("New password"));
        m_confirmPasswordField->setPlaceholderText(i18n("Confirm new password"));
        m_okButton->setText(i18n("Change Password"));
    }
}

bool ChangePasswordDialog::passwordsMatch() const
{
    return m_newPasswordField->text() == m_confirmPasswordField->text();
}

bool ChangePasswordDialog::validateInput(QString &errorMessage)
{
    // If removing password, no further validation needed beyond old password check below
    bool removeMode = m_removePasswordCheckbox->isChecked();

    // Check if old password is provided (only required if device has password AND not removing)
    if (m_oldPasswordField->text().isEmpty() && m_requiresPassword && !removeMode) {
        errorMessage = i18n("Current password is required");
        return false;
    }

    // If removing password, no further validation needed
    if (removeMode) {
        return true;
    }

    // Check if new password is provided
    if (m_newPasswordField->text().isEmpty()) {
        errorMessage = i18n("New password cannot be empty");
        return false;
    }

    // Check if passwords match
    if (m_newPasswordField->text() != m_confirmPasswordField->text()) {
        errorMessage = i18n("Passwords do not match");
        return false;
    }

    // Check if new password is different from old password
    if (m_newPasswordField->text() == m_oldPasswordField->text()) {
        errorMessage = i18n("New password must be different from current password");
        return false;
    }

    return true;
}

void ChangePasswordDialog::onOkClicked()
{
    // Validate input
    QString errorMessage;
    if (!validateInput(errorMessage)) {
        showError(errorMessage);
        return;
    }

    // Show verification state
    setVerifying(true);

    // Emit signal with old and new passwords
    QString oldPassword = m_oldPasswordField->text();
    QString newPassword = m_removePasswordCheckbox->isChecked() ?
                          QString() : m_newPasswordField->text();

    Q_EMIT passwordChangeRequested(m_deviceId, oldPassword, newPassword);
}

void ChangePasswordDialog::showError(const QString &errorMessage)
{
    // Show error using KMessageWidget
    m_messageWidget->setText(errorMessage);
    m_messageWidget->animatedShow();

    // Also show in legacy error label for backward compatibility
    m_errorLabel->setText(errorMessage);
    m_errorLabel->setVisible(true);

    setVerifying(false);

    // Clear password fields for retry
    m_oldPasswordField->clear();
    if (!m_removePasswordCheckbox->isChecked()) {
        m_newPasswordField->clear();
        m_confirmPasswordField->clear();
    }

    // Set focus to appropriate field based on requiresPassword
    if (m_requiresPassword) {
        m_oldPasswordField->setFocus();
    } else {
        m_newPasswordField->setFocus();
    }
}

void ChangePasswordDialog::setVerifying(bool verifying)
{
    // Show/hide progress bar
    m_progressBar->setVisible(verifying);

    // Disable/enable fields and buttons
    // Old password field: only enable when not verifying AND device requires password
    m_oldPasswordField->setEnabled(!verifying && m_requiresPassword);
    m_newPasswordField->setEnabled(!verifying && !m_removePasswordCheckbox->isChecked());
    m_confirmPasswordField->setEnabled(!verifying && !m_removePasswordCheckbox->isChecked());
    m_removePasswordCheckbox->setEnabled(!verifying);
    m_okButton->setEnabled(!verifying);

    // Cancel button always enabled (get from button box parent)
    if (auto *buttonBox = qobject_cast<QDialogButtonBox*>(m_okButton->parent())) {
        if (auto *cancelButton = buttonBox->button(QDialogButtonBox::Cancel)) {
            cancelButton->setEnabled(true);
        }
    }
}

} // namespace Shared
} // namespace YubiKeyOath
