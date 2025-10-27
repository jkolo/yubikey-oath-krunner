/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "password_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QProgressBar>
#include <QTimer>
#include <QPointer>
#include <QFont>
#include <QEvent>
#include <QKeyEvent>
#include <KLocalizedString>

namespace YubiKeyOath {
namespace Shared {

PasswordDialog::PasswordDialog(const QString &deviceId,
                               const QString &deviceName,
                               QWidget *parent)
    : QDialog(parent)
    , m_deviceId(deviceId)
    , m_originalDeviceName(deviceName)
    , m_deviceNameLabel(nullptr)
    , m_editNameButton(nullptr)
    , m_deviceNameField(nullptr)
    , m_deviceNameLayout(nullptr)
    , m_passwordField(nullptr)
    , m_errorLabel(nullptr)
    , m_okButton(nullptr)
    , m_progressBar(nullptr)
{
    setWindowTitle(i18n("Authorize YubiKey"));
    setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint);
    setModal(true);

    setupUi(deviceName);
}

void PasswordDialog::setupUi(const QString &deviceName)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);  // Make dialog non-resizable

    // Header
    auto *headerLabel = new QLabel(i18n("Enter YubiKey OATH password for device:"), this);
    headerLabel->setWordWrap(true);
    headerLabel->setMinimumWidth(400);  // Set minimum width for dialog
    mainLayout->addWidget(headerLabel);

    // Horizontal layout: label + edit button OR line edit
    m_deviceNameLayout = new QHBoxLayout();
    m_deviceNameLayout->setSpacing(8);

    // Label showing name (default visible)
    m_deviceNameLabel = new QLabel(deviceName, this);
    m_deviceNameLabel->setWordWrap(true);
    m_deviceNameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_deviceNameLayout->addWidget(m_deviceNameLabel);

    // Edit button with pencil icon (default visible)
    m_editNameButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-rename")), QString(), this);
    m_editNameButton->setFlat(true);
    m_editNameButton->setToolTip(i18n("Edit device name"));
    m_editNameButton->setMaximumWidth(32);
    m_editNameButton->setMaximumHeight(32);
    m_deviceNameLayout->addWidget(m_editNameButton);

    // Line edit for editing name (default hidden)
    m_deviceNameField = new QLineEdit(this);
    m_deviceNameField->setText(deviceName);
    m_deviceNameField->setPlaceholderText(i18n("Enter device name"));
    m_deviceNameField->setVisible(false);
    m_deviceNameField->installEventFilter(this);  // Install event filter to handle Enter key
    m_deviceNameLayout->addWidget(m_deviceNameField);

    mainLayout->addLayout(m_deviceNameLayout);

    // Password section
    auto *passwordLabel = new QLabel(i18n("Password:"), this);
    mainLayout->addWidget(passwordLabel);

    m_passwordField = new QLineEdit(this);
    m_passwordField->setEchoMode(QLineEdit::Password);
    m_passwordField->setPlaceholderText(i18n("YubiKey OATH password"));
    mainLayout->addWidget(m_passwordField);

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
    m_errorLabel->hide();  // Use hide() instead of setVisible(false)
    mainLayout->addWidget(m_errorLabel);

    // Button box
    auto *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        this
    );
    m_okButton = buttonBox->button(QDialogButtonBox::Ok);
    m_okButton->setText(i18n("OK"));
    m_okButton->setEnabled(false);
    m_okButton->setDefault(true);      // Make OK button the default
    m_okButton->setAutoDefault(true);  // Enable auto-default behavior

    mainLayout->addWidget(buttonBox);

    // Connect signals
    connect(m_editNameButton, &QPushButton::clicked, this, &PasswordDialog::onEditNameClicked);
    connect(m_deviceNameField, &QLineEdit::editingFinished, this, &PasswordDialog::onNameEditingFinished);

    connect(m_passwordField, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_okButton->setEnabled(!text.isEmpty());
        // Hide error when user starts typing again
        if (m_errorLabel->isVisible()) {
            m_errorLabel->setVisible(false);
        }
    });

    // IMPORTANT: Connect buttonBox signals manually to prevent auto-close
    // We handle accepted() ourselves instead of letting QDialog::accept() close the dialog
    connect(buttonBox, &QDialogButtonBox::accepted, this, &PasswordDialog::onOkClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Set focus to password field
    m_passwordField->setFocus();
}

bool PasswordDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_deviceNameField) {
        // Intercept Enter key to prevent default button activation
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                // Manually trigger editing finished (will save name via signal)
                onNameEditingFinished();
                return true; // Block propagation to default button
            }
        }
        // Handle focus out to save name when clicking outside field
        else if (event->type() == QEvent::FocusOut) {
            // Call onNameEditingFinished which has visibility guard
            onNameEditingFinished();
            return false; // Don't block focus out event
        }
    }
    return QDialog::eventFilter(watched, event);
}

void PasswordDialog::onOkClicked()
{
    QString password = m_passwordField->text();
    if (!password.isEmpty()) {
        // Show spinner immediately (synchronously)
        setVerifying(true);

        // Use QPointer to safely check if dialog still exists when lambda executes
        // This prevents use-after-free if user closes dialog before lambda runs
        QPointer<PasswordDialog> self(this);

        // Defer signal emission to next event loop iteration
        // This allows UI to update (show spinner) before caller's blocking operations
        QTimer::singleShot(0, [self, password]() {
            // Check if dialog still exists (not closed/deleted)
            if (self) {
                // Emit signal - do NOT close dialog
                // Caller will call accept() on success or showError() on failure
                Q_EMIT self->passwordEntered(self->m_deviceId, password);
            }
        });
    }
}

void PasswordDialog::showError(const QString &errorMessage)
{
    // Disable verification mode first
    setVerifying(false);

    // Show error message
    m_errorLabel->setText(errorMessage);
    m_errorLabel->show();  // Use show() instead of setVisible(true)
    m_errorLabel->raise();  // Bring to front
    m_errorLabel->updateGeometry();  // Force layout recalculation

    // Select all text in password field (allows user to retype or edit)
    // Don't clear - user might want to fix a typo
    m_passwordField->selectAll();
    m_passwordField->setFocus();
}

void PasswordDialog::setVerifying(bool verifying)
{
    // Disable/enable password field and OK button
    m_passwordField->setEnabled(!verifying);
    m_okButton->setEnabled(!verifying);
    // Cancel button remains enabled

    // Show/hide progress bar
    m_progressBar->setVisible(verifying);

    // Hide error label during verification
    if (verifying) {
        m_errorLabel->hide();  // Use hide() instead of setVisible(false)
    }

    // Update placeholder text
    if (verifying) {
        m_passwordField->setPlaceholderText(i18n("Verifying password..."));
    } else {
        m_passwordField->setPlaceholderText(i18n("YubiKey OATH password"));
    }
}

void PasswordDialog::onEditNameClicked()
{
    // Hide label and button
    m_deviceNameLabel->setVisible(false);
    m_editNameButton->setVisible(false);

    // Show and focus line edit
    m_deviceNameField->setVisible(true);
    m_deviceNameField->setFocus();
    m_deviceNameField->selectAll();
}

void PasswordDialog::onNameEditingFinished()
{
    // Guard: only process if field is visible (in edit mode)
    // This prevents double-calling from both editingFinished signal and FocusOut event
    if (!m_deviceNameField->isVisible()) {
        return;
    }

    QString newName = m_deviceNameField->text().trimmed();

    // If empty, restore original name
    if (newName.isEmpty()) {
        newName = m_originalDeviceName;
        m_deviceNameField->setText(newName);
    }

    // Check if name actually changed
    bool nameChanged = (newName != m_originalDeviceName);

    // Update label with new name
    m_deviceNameLabel->setText(newName);

    // Emit signal if name changed
    if (nameChanged) {
        m_originalDeviceName = newName;
        Q_EMIT deviceNameChanged(m_deviceId, newName);
    }

    // Switch back to display mode
    m_deviceNameField->setVisible(false);
    m_deviceNameLabel->setVisible(true);
    m_editNameButton->setVisible(true);
}

} // namespace Shared
} // namespace YubiKeyOath
