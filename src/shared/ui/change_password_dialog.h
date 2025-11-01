/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDialog>
#include <QString>
#include <KMessageWidget>

class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;
class QProgressBar;

namespace YubiKeyOath {
namespace Shared {

/**
 * @brief Dialog for changing YubiKey OATH password
 *
 * Allows:
 * - Entering current password
 * - Setting new password with confirmation
 * - Removing password protection (via checkbox)
 * - Inline error messages for validation failures
 */
class ChangePasswordDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Constructs change password dialog
     * @param deviceId Device ID
     * @param deviceName Friendly device name
     * @param requiresPassword Whether device currently requires password
     * @param parent Parent widget (typically nullptr for top-level)
     */
    explicit ChangePasswordDialog(const QString &deviceId,
                                   const QString &deviceName,
                                   bool requiresPassword,
                                   QWidget *parent = nullptr);

    ~ChangePasswordDialog() override = default;

    /**
     * @brief Shows error message in dialog
     * @param errorMessage Error text to display
     *
     * Displays error message in red. Dialog stays open for retry.
     */
    void showError(const QString &errorMessage);

    /**
     * @brief Enables/disables verification mode
     * @param verifying true = show spinner, disable fields; false = normal mode
     *
     * When verifying=true: shows progress bar, disables all fields and OK button.
     * When verifying=false: hides progress bar, re-enables fields.
     * Cancel button always remains enabled.
     */
    void setVerifying(bool verifying);

Q_SIGNALS:
    /**
     * @brief Emitted when user clicks OK with valid input
     * @param deviceId Device ID
     * @param oldPassword Current password
     * @param newPassword New password (empty if removing password)
     *
     * Note: Dialog does NOT close automatically. Caller must call
     * accept() on success or showError() on failure.
     */
    void passwordChangeRequested(const QString &deviceId,
                                 const QString &oldPassword,
                                 const QString &newPassword);

private:
    QString m_deviceId;
    bool m_requiresPassword;
    QLabel *m_deviceNameLabel;
    QLineEdit *m_oldPasswordField;
    QLineEdit *m_newPasswordField;
    QLineEdit *m_confirmPasswordField;
    QCheckBox *m_removePasswordCheckbox;
    QLabel *m_errorLabel;
    KMessageWidget *m_messageWidget;
    QPushButton *m_okButton;
    QProgressBar *m_progressBar;

    void setupUi(const QString &deviceName);
    void onOkClicked();
    void onRemovePasswordToggled(bool checked);
    bool validateInput(QString &errorMessage);
    bool passwordsMatch() const;
};

} // namespace Shared
} // namespace YubiKeyOath
