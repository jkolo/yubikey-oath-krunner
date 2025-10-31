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
class KMessageWidget;

namespace YubiKeyOath {
namespace Daemon {
using Shared::OathCredentialData;

class ProcessingOverlay;

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
     * @param availableDevices Map of device ID → device name for selection
     * @param preselectedDeviceId Optional device ID to preselect in combo
     * @param parent Parent widget
     */
    explicit AddCredentialDialog(const OathCredentialData &initialData,
                                const QMap<QString, QString> &availableDevices,
                                const QString &preselectedDeviceId = QString(),
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
    void onScanQrClicked();
    void onCaptured(const QImage &image);
    void onCancelled();

private:
    void setupUi(const OathCredentialData &initialData, const QMap<QString, QString> &devices);
    void updateFieldsForType();
    bool validateAndBuildData();
    void fillFieldsFromQrData(const OathCredentialData &data);
    void showMessage(const QString &text, int messageType);
    void showProcessingOverlay(const QString &message);
    void hideProcessingOverlay();
    void updateOverlayStatus(const QString &message);

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
    QPushButton *m_scanQrButton;
    QLabel *m_errorLabel;
    QPushButton *m_okButton;
    KMessageWidget *m_messageWidget;

    // Processing overlay
    ProcessingOverlay *m_processingOverlay;

    // Screenshot capturer (UI thread)
    class ScreenshotCapturer *m_screenshotCapturer;

    bool m_secretRevealed;
};

} // namespace Daemon
} // namespace YubiKeyOath
