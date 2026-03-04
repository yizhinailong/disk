import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    property string prefillAccount: ""

    signal registerRequested

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Left decoration region
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: root.width * 0.45
            color: palette.window
            
            // Background gradient
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.tint(palette.window, Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.15)) }
                    GradientStop { position: 1.0; color: Qt.tint(palette.window, Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.05)) }
                }
            }

            // Ellipse 1
            Rectangle {
                width: 300
                height: 300
                radius: 150
                color: Qt.rgba(1, 1, 1, 0.3)
                x: -50
                y: -50
            }
            // Ellipse 2
            Rectangle {
                width: 200
                height: 200
                radius: 100
                color: Qt.rgba(1, 1, 1, 0.2)
                anchors.right: parent.right
                anchors.rightMargin: -50
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 100
            }
            // Ellipse 3
            Rectangle {
                width: 150
                height: 150
                radius: 75
                color: Qt.rgba(1, 1, 1, 0.25)
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 150
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 16

                Label {
                    text: qsTr("欢迎回来！")
                    font.pixelSize: 32
                    font.bold: true
                    color: palette.windowText
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: qsTr("今天也要元气满满哦✨")
                    font.pixelSize: 18
                    color: palette.windowText
                    opacity: 0.8
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // Right form region
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true

            Rectangle {
                anchors.centerIn: parent
                width: 420
                radius: 16
                color: palette.window
                implicitHeight: formLayout.implicitHeight + 64

                // Fake shadow
                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.margins: -1
                    anchors.topMargin: 4
                    anchors.bottomMargin: -4
                    radius: 16
                    color: Qt.rgba(0, 0, 0, 0.05)
                }

                border.color: Qt.rgba(0, 0, 0, 0.08)
                border.width: 1

                ColumnLayout {
                    id: formLayout
                    anchors.fill: parent
                    anchors.margins: 32
                    spacing: 24

                    // Logo and Title
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: qsTr("Disk")
                            font.pixelSize: 64
                            font.bold: true
                            color: palette.highlight
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Label {
                            text: qsTr("欢迎回来")
                            font.pixelSize: 24
                            font.bold: true
                            color: palette.windowText
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Label {
                            text: qsTr("登录你的账号")
                            font.pixelSize: 14
                            color: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.6)
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    // Pill tabs: 登录 | 注册
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        radius: 22
                        color: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.05)

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 4

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 18
                                color: palette.highlight

                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("登录")
                                    color: palette.highlightedText
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                                MouseArea {
                                    objectName: "tabLogin"
                                    anchors.fill: parent
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 18
                                color: "transparent"

                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("注册")
                                    color: palette.windowText
                                    font.pixelSize: 14
                                }
                                MouseArea {
                                    objectName: "tabRegister"
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.registerRequested()
                                }
                            }
                        }
                    }

                    // Input fields
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        TextField {
                            id: accountField
                            objectName: "accountField"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            placeholderText: qsTr("账号（用户名/邮箱）")
                            text: LoginViewModel.account
                            font.pixelSize: 14
                            enabled: !LoginViewModel.loading

                            background: Rectangle {
                                radius: 12
                                color: accountField.activeFocus ? palette.base : Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.03)
                                border.color: accountField.activeFocus ? palette.highlight : Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.1)
                                border.width: accountField.activeFocus ? 2 : 1
                            }
                            leftPadding: 16
                            rightPadding: 16

                            onTextChanged: {
                                LoginViewModel.account = text
                                LoginViewModel.clearError()
                            }
                            onAccepted: {
                                if (LoginViewModel.canSubmit) {
                                    LoginViewModel.submit()
                                } else {
                                    passwordField.forceActiveFocus()
                                }
                            }
                        }

                        TextField {
                            id: passwordField
                            objectName: "passwordField"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            placeholderText: qsTr("密码")
                            echoMode: showPasswordBtn.checked ? TextInput.Normal : TextInput.Password
                            text: LoginViewModel.password
                            font.pixelSize: 14
                            enabled: !LoginViewModel.loading

                            background: Rectangle {
                                radius: 12
                                color: passwordField.activeFocus ? palette.base : Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.03)
                                border.color: passwordField.activeFocus ? palette.highlight : Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.1)
                                border.width: passwordField.activeFocus ? 2 : 1
                            }
                            leftPadding: 16
                            rightPadding: 48

                            onTextChanged: {
                                LoginViewModel.password = text
                                LoginViewModel.clearError()
                            }
                            onAccepted: {
                                if (LoginViewModel.canSubmit) {
                                    LoginViewModel.submit()
                                }
                            }

                            Button {
                                id: showPasswordBtn
                                checkable: true
                                flat: true
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                width: 36
                                height: 36
                                enabled: !LoginViewModel.loading
                                
                                background: Item {}

                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: "👁"
                                    font.pixelSize: 16
                                    color: showPasswordBtn.checked ? palette.highlight : Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.4)
                                }
                            }
                        }
                    }

                    // Error text
                    Label {
                        id: errorLabel
                        visible: LoginViewModel.errorMessage !== ""
                        text: LoginViewModel.errorMessage
                        color: "#F44336" // Red
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        Layout.preferredHeight: visible ? implicitHeight : 0
                        Layout.topMargin: visible ? -8 : -16
                        Layout.bottomMargin: visible ? 0 : -16
                    }

                    // Login Button
                    Button {
                        id: loginButton
                        objectName: "loginButton"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        Layout.topMargin: 8
                        enabled: LoginViewModel.canSubmit && !LoginViewModel.loading
                        
                        background: Rectangle {
                            radius: 12
                            color: loginButton.enabled ? palette.highlight : Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.5)
                        }

                        contentItem: Item {
                            anchors.fill: parent
                            
                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 8
                                
                                BusyIndicator {
                                    running: LoginViewModel.loading
                                    visible: LoginViewModel.loading
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                }
                                
                                Text {
                                    text: LoginViewModel.loading ? qsTr("登录中...") : qsTr("登 录")
                                    color: palette.highlightedText
                                    font.pixelSize: 16
                                    font.bold: true
                                }
                            }
                        }
                        
                        onClicked: {
                            LoginViewModel.submit()
                        }
                    }
                }
            }
        }
    }

    onPrefillAccountChanged: {
        if (prefillAccount !== "") {
            LoginViewModel.account = prefillAccount
            passwordField.forceActiveFocus()
        }
    }
}
