/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;
class QProgressBar;
class QHBoxLayout;

namespace KRunner {
namespace YubiKey {

/**
 * @brief Simple password dialog for YubiKey authentication
 *
 * Custom dialog that allows multiple password attempts without closing.
 * Shows inline error messages when password is incorrect.
 */
class PasswordDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Constructs password dialog
     * @param deviceId Device ID requiring password
     * @param deviceName Friendly device name
     * @param parent Parent widget (typically nullptr for top-level)
     */
    explicit PasswordDialog(const QString &deviceId,
                           const QString &deviceName,
                           QWidget *parent = nullptr);

    ~PasswordDialog() override = default;

    /**
     * @brief Event filter to handle Enter key in device name field
     * @param watched Object being watched
     * @param event Event to filter
     * @return true if event should be filtered (not propagated), false otherwise
     *
     * Intercepts Enter key in device name field to prevent it from triggering
     * the default OK button. Instead, it finishes name editing only.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * @brief Shows error message in dialog
     * @param errorMessage Error text to display
     *
     * Displays error message in red, clears password field,
     * and sets focus back to password input. Dialog stays open.
     */
    void showError(const QString &errorMessage);

    /**
     * @brief Enables/disables verification mode
     * @param verifying true = show spinner, disable fields; false = normal mode
     *
     * When verifying=true: shows progress bar, disables password field and OK button.
     * When verifying=false: hides progress bar, re-enables fields.
     * Cancel button always remains enabled.
     */
    void setVerifying(bool verifying);

Q_SIGNALS:
    /**
     * @brief Emitted when user enters password and clicks OK
     * @param deviceId Device ID
     * @param password Password entered by user
     *
     * Note: Dialog does NOT close automatically. Caller must call
     * accept() on success or showError() on failure.
     */
    void passwordEntered(const QString &deviceId, const QString &password);

    /**
     * @brief Emitted when device name is changed
     * @param deviceId Device ID
     * @param newName New device name
     *
     * Emitted immediately when user finishes editing name (focus out or Enter).
     */
    void deviceNameChanged(const QString &deviceId, const QString &newName);

private:
    QString m_deviceId;
    QString m_originalDeviceName;
    QLabel *m_deviceNameLabel;
    QPushButton *m_editNameButton;
    QLineEdit *m_deviceNameField;
    QHBoxLayout *m_deviceNameLayout;
    QLineEdit *m_passwordField;
    QLabel *m_errorLabel;
    QPushButton *m_okButton;
    QProgressBar *m_progressBar;

    void setupUi(const QString &deviceName);
    void onOkClicked();
    void onEditNameClicked();
    void onNameEditingFinished();
};

} // namespace YubiKey
} // namespace KRunner
