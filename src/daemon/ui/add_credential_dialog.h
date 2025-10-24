/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDialog>
#include <QString>
#include "types/oath_credential_data.h"

// Forward declarations
class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;

namespace KRunner {
namespace YubiKey {

/**
 * @brief Dialog for adding/editing OATH credential before saving to YubiKey
 *
 * Allows user to review and modify credential parameters extracted from QR code:
 * - Issuer (service name)
 * - Account (username)
 * - Secret (Base32, read-only with option to reveal/edit)
 * - Type (TOTP/HOTP)
 * - Algorithm (SHA1/SHA256/SHA512)
 * - Digits (6/7/8)
 * - Period (TOTP only, seconds)
 * - Counter (HOTP only, initial value)
 * - Require Touch (checkbox)
 * - Device selection (if multiple YubiKeys)
 */
class AddCredentialDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @brief Constructs credential dialog
     * @param initialData Initial credential data (from QR code parser)
     * @param availableDevices List of device IDs for selection
     * @param parent Parent widget
     */
    explicit AddCredentialDialog(const OathCredentialData &initialData,
                                const QStringList &availableDevices,
                                QWidget *parent = nullptr);

    ~AddCredentialDialog() override = default;

    /**
     * @brief Gets final credential data after user edits
     * @return Credential data with user modifications
     */
    OathCredentialData getCredentialData() const;

    /**
     * @brief Gets selected device ID
     * @return Device ID selected by user (empty if none selected)
     */
    QString getSelectedDeviceId() const;

Q_SIGNALS:
    /**
     * @brief Emitted when user accepts dialog with valid data
     * @param data Final credential data
     * @param deviceId Selected device ID
     */
    void credentialAccepted(const OathCredentialData &data, const QString &deviceId);

private Q_SLOTS:
    void onTypeChanged(int index);
    void onOkClicked();
    void onRevealSecretClicked();

private:
    void setupUi(const OathCredentialData &initialData, const QStringList &devices);
    void updateFieldsForType();
    bool validateAndBuildData();

    // UI elements
    QLineEdit *m_issuerField;
    QLineEdit *m_accountField;
    QLineEdit *m_secretField;
    QComboBox *m_typeCombo;
    QComboBox *m_algorithmCombo;
    QSpinBox *m_digitsSpinBox;
    QSpinBox *m_periodSpinBox;
    QSpinBox *m_counterSpinBox;
    QCheckBox *m_touchCheckBox;
    QComboBox *m_deviceCombo;
    QPushButton *m_revealSecretButton;
    QLabel *m_errorLabel;
    QPushButton *m_okButton;

    bool m_secretRevealed;
};

} // namespace YubiKey
} // namespace KRunner
