import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    signal registered(string username, string email)
    signal backRequested

    property bool showPassword: false

    Rectangle {
        anchors.fill: parent
        color: "#FAFAFA"

        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: Math.min(400, parent.width * 0.9)
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 16

                Item {
                    Layout.preferredHeight: 32
                } // Spacer

                // Back button
                Button {
                    text: "← 返回"
                    flat: true
                    Layout.alignment: Qt.AlignLeft
                    onClicked: root.backRequested()
                }

                Label {
                    text: "创建新账号"
                    font.pixelSize: 24
                    font.bold: true
                    color: "#212121"
                    Layout.alignment: Qt.AlignHCenter
                }

                Item {
                    Layout.preferredHeight: 16
                } // Spacer

                // Global Error Message
                Label {
                    id: globalErrorLabel
                    objectName: "globalErrorLabel"
                    visible: AppContext.registerViewModel.errorMessage !== ""
                    text: AppContext.registerViewModel.errorMessage
                    color: "#F44336"
                    font.pixelSize: 14
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                // Username
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    TextField {
                        id: usernameInput
                        objectName: "usernameInput"
                        Layout.fillWidth: true
                        placeholderText: "4-32个字符，仅包含字母、数字、下划线"
                        maximumLength: 32
                        enabled: !AppContext.registerViewModel.loading
                        text: AppContext.registerViewModel.username
                        onTextChanged: AppContext.registerViewModel.username = text
                    }
                    Label {
                        objectName: "usernameErrorLabel"
                        text: AppContext.registerViewModel.usernameError
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: AppContext.registerViewModel.usernameError !== ""
                    }
                }

                // Email
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    TextField {
                        id: emailInput
                        objectName: "emailInput"
                        Layout.fillWidth: true
                        placeholderText: "请输入有效邮箱地址"
                        maximumLength: 254
                        enabled: !AppContext.registerViewModel.loading
                        text: AppContext.registerViewModel.email
                        onTextChanged: AppContext.registerViewModel.email = text
                    }
                    Label {
                        objectName: "emailErrorLabel"
                        text: AppContext.registerViewModel.emailError
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: AppContext.registerViewModel.emailError !== ""
                    }
                }

                // Password
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        TextField {
                            id: passwordInput
                            objectName: "passwordInput"
                            Layout.fillWidth: true
                            placeholderText: "设置密码"
                            echoMode: root.showPassword ? TextInput.Normal : TextInput.Password
                            maximumLength: 64
                            enabled: !AppContext.registerViewModel.loading
                            text: AppContext.registerViewModel.password
                            onTextChanged: AppContext.registerViewModel.password = text
                        }
                        Button {
                            id: showPasswordButton
                            objectName: "showPasswordButton"
                            text: root.showPassword ? "隐藏" : "显示"
                            onClicked: root.showPassword = !root.showPassword
                            enabled: !AppContext.registerViewModel.loading
                        }
                    }
                    Label {
                        objectName: "passwordErrorLabel"
                        text: AppContext.registerViewModel.passwordError
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: AppContext.registerViewModel.passwordError !== ""
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // Confirm Password
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    TextField {
                        id: confirmPasswordInput
                        objectName: "confirmPasswordInput"
                        Layout.fillWidth: true
                        placeholderText: "请再次输入密码"
                        echoMode: root.showPassword ? TextInput.Normal : TextInput.Password
                        maximumLength: 64
                        enabled: !AppContext.registerViewModel.loading
                        text: AppContext.registerViewModel.confirmPassword
                        onTextChanged: AppContext.registerViewModel.confirmPassword = text
                    }
                    Label {
                        objectName: "confirmPasswordErrorLabel"
                        text: AppContext.registerViewModel.confirmPasswordError
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: AppContext.registerViewModel.confirmPasswordError !== ""
                    }
                }

                Item {
                    Layout.preferredHeight: 16
                } // Spacer

                // Register Button
                Button {
                    id: submitButton
                    objectName: "submitButton"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    text: AppContext.registerViewModel.loading ? "注册中..." : "注册"
                    enabled: AppContext.registerViewModel.canSubmit
                             && !AppContext.registerViewModel.loading

                    background: Rectangle {
                        color: submitButton.enabled ? "#2196F3" : "#E0E0E0"
                        radius: 8
                    }
                    contentItem: Text {
                        text: submitButton.text
                        color: submitButton.enabled ? "#FFFFFF" : "#757575"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 14
                        font.bold: true
                    }

                    onClicked: {
                        AppContext.registerViewModel.submit()
                    }
                }

                Item {
                    Layout.preferredHeight: 32
                } // Bottom spacer
            }
        }
    }

    Connections {
        target: AppContext.registerViewModel
        function onRegisterSucceeded(username, email) {
            root.registered(username, email)
        }
    }
}
