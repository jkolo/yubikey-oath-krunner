/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDialog>
#include <QString>
#include "types/oath_credential_data.h"
#include "shared/types/yubikey_value_types.h"

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
     * @param availableDevices List of DeviceInfo for device selection
     * @param preselectedDeviceId Optional device ID to preselect in combo
     * @param parent Parent widget
     */
    explicit AddCredentialDialog(const OathCredentialData &initialData,
                                const QList<Shared::DeviceInfo> &availableDevices,
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

    /**
     * @brief Shows save result (success or error)
     * @param success True if save succeeded, false otherwise
     * @param errorMessage Error message to display (empty if success)
     */
    void showSaveResult(bool success, const QString &errorMessage);

    /**
     * @brief Updates the processing overlay status message
     * @param message New status message to display
     */
    void updateOverlayStatus(const QString &message);

Q_SIGNALS:
    /**
     * @brief Emitted when user accepts dialog with valid data
     * @param data Final credential data
     * @param deviceId Selected device ID
     */
    void credentialAccepted(const OathCredentialData &data, const QString &deviceId);

    /**
     * @brief Emitted when credential is ready to be saved to YubiKey
     * @param data Validated credential data
     * @param deviceId Target device ID
     */
    void credentialReadyToSave(const Shared::OathCredentialData &data, const QString &deviceId);

private Q_SLOTS:
    void onTypeChanged(int index);
    void onDeviceChanged(int index);
    void onOkClicked();
    void onRevealSecretClicked();
    void onScanQrClicked();
    void onCaptured(const QImage &image);
    void onCancelled();

private:
    void setupUi(const OathCredentialData &initialData, const QList<Shared::DeviceInfo> &devices);
    void updateFieldsForType();
    bool validateAndBuildData();
    void fillFieldsFromQrData(const OathCredentialData &data);
    void showMessage(const QString &text, int messageType);
    void showProcessingOverlay(const QString &message);
    void hideProcessingOverlay();
    int algorithmToComboIndex(Shared::OathAlgorithm algorithm) const;

    // UI elements
    QLineEdit *m_issuerField = nullptr;
    QLineEdit *m_accountField = nullptr;
    QLineEdit *m_secretField = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QComboBox *m_algorithmCombo = nullptr;
    QSpinBox *m_digitsSpinBox = nullptr;
    QSpinBox *m_periodSpinBox = nullptr;
    QSpinBox *m_counterSpinBox = nullptr;
    QCheckBox *m_touchCheckBox = nullptr;
    QComboBox *m_deviceCombo = nullptr;
    QPushButton *m_revealSecretButton = nullptr;
    QPushButton *m_scanQrButton = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_okButton = nullptr;
    KMessageWidget *m_messageWidget = nullptr;

    // Processing overlay
    ProcessingOverlay *m_processingOverlay = nullptr;

    // Screenshot capturer (UI thread)
    class ScreenshotCapturer *m_screenshotCapturer = nullptr;

    bool m_secretRevealed = false;

    // Device list for firmware validation
    QList<Shared::DeviceInfo> m_availableDevices;
};

} // namespace Daemon
} // namespace YubiKeyOath
