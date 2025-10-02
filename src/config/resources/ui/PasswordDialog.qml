/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.OverlaySheet {
    id: passwordDialog

    property string deviceId: ""
    property string deviceName: ""
    property var deviceModel: null

    title: i18n("Authorize YubiKey")

    header: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: ":/icons/yubikey.svg"
            Layout.preferredWidth: Kirigami.Units.iconSizes.medium
            Layout.preferredHeight: Kirigami.Units.iconSizes.medium
        }

        Kirigami.Heading {
            level: 2
            text: passwordDialog.title
            Layout.fillWidth: true
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            QQC2.Label {
                Kirigami.FormData.label: i18n("Device:")
                text: passwordDialog.deviceName
                font.bold: true
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Device ID:")
                text: passwordDialog.deviceId
                font.family: "monospace"
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: i18n("This YubiKey requires a password to access OATH credentials.")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            QQC2.Label {
                text: i18n("Please enter the password:")
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            QQC2.TextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: i18n("YubiKey OATH password")
                echoMode: TextInput.Password
                focus: true

                onAccepted: {
                    okButton.clicked()
                }

                Keys.onEscapePressed: {
                    cancelButton.clicked()
                }
            }

            Kirigami.InlineMessage {
                id: errorMessage
                Layout.fillWidth: true
                type: Kirigami.MessageType.Error
                visible: false
                showCloseButton: true

                onVisibleChanged: {
                    if (visible) {
                        // Auto-hide after 5 seconds
                        errorTimer.restart()
                    }
                }
            }

            Timer {
                id: errorTimer
                interval: 5000
                onTriggered: {
                    errorMessage.visible = false
                }
            }
        }

        RowLayout {
            spacing: Kirigami.Units.smallSpacing
            Layout.alignment: Qt.AlignRight

            QQC2.Button {
                id: cancelButton
                text: i18n("Cancel")
                icon.name: "dialog-cancel"

                onClicked: {
                    passwordDialog.close()
                    passwordField.text = ""
                    errorMessage.visible = false
                }
            }

            QQC2.Button {
                id: okButton
                text: i18n("OK")
                icon.name: "dialog-ok-apply"
                enabled: passwordField.text.length > 0

                onClicked: {
                    if (!passwordDialog.deviceModel) {
                        console.error("PasswordDialog: deviceModel is null")
                        return
                    }

                    // Test password
                    var success = passwordDialog.deviceModel.testAndSavePassword(
                        passwordDialog.deviceId,
                        passwordField.text
                    )

                    if (success) {
                        // Password valid - close dialog
                        console.log("PasswordDialog: Password valid for device:", passwordDialog.deviceId)
                        passwordDialog.close()
                        passwordField.text = ""
                        errorMessage.visible = false
                    } else {
                        // Password invalid - show error and allow retry
                        console.log("PasswordDialog: Password invalid for device:", passwordDialog.deviceId)
                        errorMessage.text = i18n("Invalid password. Please try again.")
                        errorMessage.visible = true
                        passwordField.selectAll()
                        passwordField.forceActiveFocus()
                    }
                }
            }
        }
    }

    onOpened: {
        passwordField.text = ""
        passwordField.forceActiveFocus()
        errorMessage.visible = false
    }

    onClosed: {
        passwordField.text = ""
        errorMessage.visible = false
    }

    // Connect to model's passwordTestFailed signal
    Connections {
        target: passwordDialog.deviceModel

        function onPasswordTestFailed(failedDeviceId, error) {
            if (failedDeviceId === passwordDialog.deviceId) {
                console.log("PasswordDialog: Password test failed:", error)
                errorMessage.text = error
                errorMessage.visible = true
            }
        }
    }
}
