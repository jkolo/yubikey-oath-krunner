/*
 * SPDX-FileCopyrightText: 2024 YubiKey KRunner Plugin Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    anchors.fill: parent
    anchors.margins: 0
    spacing: 0

    // Shared properties for consistent styling
    readonly property int smallFontSize: Kirigami.Theme.smallFont.pointSize
    readonly property int normalFontSize: Kirigami.Theme.defaultFont.pointSize
    readonly property int largeFontSize: Kirigami.Theme.defaultFont.pointSize * 1.1

    // Background
    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
        z: -1
    }

    // Device list with scrollbar
    QQC2.ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 150

        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff
        QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AsNeeded

        ListView {
            id: deviceListView
            clip: true
            spacing: Kirigami.Units.smallSpacing
            model: deviceModel

            delegate: Kirigami.AbstractCard {
                width: deviceListView.width

                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    // YubiKey icon
                    Kirigami.Icon {
                        source: ":/icons/yubikey.svg"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.large
                        Layout.preferredHeight: Kirigami.Units.iconSizes.large
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Device info
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        // Device name (editable inline)
                        EditableDeviceName {
                            Layout.fillWidth: true
                            deviceId: model.deviceId
                            deviceName: model.deviceName
                            fontSize: root.largeFontSize
                        }

                        // Device ID
                        QQC2.Label {
                            text: i18n("Device ID: %1", model.deviceId.substring(0, 16) + "...")
                            font.family: "monospace"
                            font.pointSize: root.smallFontSize
                            opacity: 0.7
                        }

                        // Status row
                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            // Connection status indicator
                            StatusIndicator {
                                dotColor: model.isConnected ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor
                                text: model.isConnected ? i18n("Connected") : i18n("Not connected")
                            }

                            // Password status indicator
                            PasswordStatusIndicator {
                                requiresPassword: model.requiresPassword
                                hasValidPassword: model.hasValidPassword
                            }
                        }

                        // Last seen timestamp
                        QQC2.Label {
                            text: i18n("Last seen: %1", Qt.formatDateTime(model.lastSeen, "yyyy-MM-dd HH:mm"))
                            font.pointSize: root.smallFontSize
                            opacity: 0.6
                            visible: !model.isConnected && model.lastSeen !== undefined
                        }
                    }

                    // Action buttons
                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Layout.alignment: Qt.AlignVCenter

                        // Authorize button
                        QQC2.Button {
                            text: i18n("Authorize")
                            icon.name: "password-show-on"
                            visible: model.showAuthorizeButton
                            onClicked: {
                                deviceModel.showPasswordDialog(model.deviceId, model.deviceName)
                            }
                        }

                        // Forget button
                        QQC2.Button {
                            text: i18n("Forget")
                            icon.name: "edit-delete"
                            display: QQC2.AbstractButton.IconOnly
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.text: i18n("Forget this device and remove saved password")
                            onClicked: deviceModel.forgetDevice(model.deviceId)
                        }
                    }
                }
            }

            // Empty state
            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - (Kirigami.Units.largeSpacing * 4)
                visible: deviceListView.count === 0
                icon.name: ":/icons/yubikey.svg"
                text: i18n("No YubiKey devices found")
                explanation: i18n("Connect a YubiKey to see it listed here.\nDevices that have been used before will be remembered.")
            }
        }
    }

    // Refresh button
    QQC2.Button {
        text: i18n("Refresh Device List")
        icon.name: "view-refresh"
        Layout.alignment: Qt.AlignRight
        Layout.topMargin: Kirigami.Units.smallSpacing
        onClicked: deviceModel.refreshDevices()
    }

    // Status indicator component
    component StatusIndicator: RowLayout {
        property color dotColor
        property string text

        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: dotColor
            Layout.alignment: Qt.AlignVCenter
        }

        QQC2.Label {
            text: parent.text
            font.pointSize: root.smallFontSize
            opacity: 0.8
        }
    }

    // Password status indicator component
    component PasswordStatusIndicator: RowLayout {
        property bool requiresPassword
        property bool hasValidPassword

        readonly property string iconName: requiresPassword ? (hasValidPassword ? "lock" : "lock-insecure") : "lock-unlocked"
        readonly property color iconColor: requiresPassword ? (hasValidPassword ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor) : Kirigami.Theme.neutralTextColor
        readonly property string statusText: {
            if (!requiresPassword) return i18n("No password required")
            if (hasValidPassword) return i18n("Password saved")
            return i18n("Password required")
        }

        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: parent.iconName
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            color: parent.iconColor
            Layout.alignment: Qt.AlignVCenter
        }

        QQC2.Label {
            text: parent.statusText
            font.pointSize: root.smallFontSize
            opacity: 0.8
        }
    }

    // Editable device name component with inline editing
    component EditableDeviceName: Item {
        id: editableNameRoot

        property string deviceId
        property string deviceName
        property int fontSize: Kirigami.Theme.defaultFont.pointSize

        implicitHeight: editMode ? nameTextField.implicitHeight : nameLabel.implicitHeight

        property bool editMode: false
        property string originalName: deviceName

        // Label mode (default)
        QQC2.Label {
            id: nameLabel
            anchors.fill: parent
            text: deviceName
            font.bold: true
            font.pointSize: fontSize
            visible: !editMode

            // Visual hint that it's editable
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true

                onClicked: {
                    editableNameRoot.originalName = editableNameRoot.deviceName
                    editableNameRoot.editMode = true
                    nameTextField.forceActiveFocus()
                    nameTextField.selectAll()
                }

                onEntered: {
                    nameLabel.color = Kirigami.Theme.highlightColor
                }

                onExited: {
                    nameLabel.color = Kirigami.Theme.textColor
                }
            }
        }

        // TextField mode (editing)
        QQC2.TextField {
            id: nameTextField
            anchors.fill: parent
            text: deviceName
            font.bold: true
            font.pointSize: fontSize
            visible: editMode
            selectByMouse: true
            maximumLength: 64

            onAccepted: {
                saveName()
            }

            onActiveFocusChanged: {
                if (!activeFocus && editMode) {
                    saveName()
                }
            }

            Keys.onEscapePressed: {
                // Cancel editing
                editableNameRoot.editMode = false
                nameTextField.text = editableNameRoot.originalName
            }

            function saveName() {
                let trimmedName = text.trim()

                if (trimmedName.length === 0) {
                    // Empty name - revert
                    nameTextField.text = editableNameRoot.originalName
                    editableNameRoot.editMode = false
                    return
                }

                if (trimmedName === editableNameRoot.deviceName) {
                    // No change
                    editableNameRoot.editMode = false
                    return
                }

                // Update via model
                let success = deviceModel.setDeviceName(editableNameRoot.deviceId, trimmedName)

                if (success) {
                    editableNameRoot.deviceName = trimmedName
                    editableNameRoot.editMode = false
                } else {
                    // Failed - show error and revert
                    console.error("Failed to update device name")
                    nameTextField.text = editableNameRoot.originalName
                    editableNameRoot.editMode = false
                }
            }
        }
    }

    Component.onCompleted: {
        console.log("YubiKeyConfig.qml loaded")
        deviceModel.refreshDevices()
    }
}
