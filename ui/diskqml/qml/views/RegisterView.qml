/**
 * @file RegisterView.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 注册页面
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    signal registered(string username, string email)
    signal backRequested

    property bool showPassword: false

    // ==================== 页面布局 ====================
    Rectangle {
        anchors.fill: parent
        color: "#FAFAFA"

        // ==================== 注册表单 ====================
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
                } // 间距

                // 返回按钮
                Button {
                    text: "← 返回"
                    flat: true
                    Layout.alignment: Qt.AlignLeft
                    onClicked: root.backRequested()
                }

                Label {
                    text: "创建新账号"
                    font.pixelSize: 20
                    font.bold: true
                    color: "#212121"
                    Layout.alignment: Qt.AlignHCenter
                }

                Item {
                    Layout.preferredHeight: 16
                } // 间距

                // 全局错误信息
                Label {
                    id: globalErrorLabel
                    objectName: "globalErrorLabel"
                    visible: RegisterViewModel.errorMessage !== ""
                    text: RegisterViewModel.errorMessage
                    color: "#F44336"
                    font.pixelSize: 14
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                // 用户名
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    TextField {
                        id: usernameInput
                        objectName: "usernameInput"
                        Layout.fillWidth: true
                        placeholderText: "4-32个字符，仅包含字母、数字、下划线"
                        maximumLength: 32
                        enabled: !RegisterViewModel.loading
                        text: RegisterViewModel.username
                        onTextChanged: RegisterViewModel.username = text
                    }
                    Label {
                        objectName: "usernameErrorLabel"
                        text: RegisterViewModel.usernameError
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: RegisterViewModel.usernameError !== ""
                    }
                }

                // 邮箱
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    TextField {
                        id: emailInput
                        objectName: "emailInput"
                        Layout.fillWidth: true
                        placeholderText: "请输入有效邮箱地址"
                        maximumLength: 254
                        enabled: !RegisterViewModel.loading
                        text: RegisterViewModel.email
                        onTextChanged: RegisterViewModel.email = text
                    }
                    Label {
                        objectName: "emailErrorLabel"
                        text: RegisterViewModel.emailError
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: RegisterViewModel.emailError !== ""
                    }
                }

                // 密码
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
                            enabled: !RegisterViewModel.loading
                            text: RegisterViewModel.password
                            onTextChanged: RegisterViewModel.password = text
                        }
                        Button {
                            id: showPasswordButton
                            objectName: "showPasswordButton"
                            text: root.showPassword ? "隐藏" : "显示"
                            onClicked: root.showPassword = !root.showPassword
                            enabled: !RegisterViewModel.loading
                        }
                    }
                    Label {
                        objectName: "passwordErrorLabel"
                        text: RegisterViewModel.passwordError
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: RegisterViewModel.passwordError !== ""
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }

                // 确认密码
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
                        enabled: !RegisterViewModel.loading
                        text: RegisterViewModel.confirmPassword
                        onTextChanged: RegisterViewModel.confirmPassword = text
                    }
                    Label {
                        objectName: "confirmPasswordErrorLabel"
                        text: RegisterViewModel.confirmPasswordError
                        color: "#F44336"
                        font.pixelSize: 12
                        visible: RegisterViewModel.confirmPasswordError !== ""
                    }
                }

                Item {
                    Layout.preferredHeight: 16
                } // 间距

                // 注册按钮
                Button {
                    id: submitButton
                    objectName: "submitButton"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    text: RegisterViewModel.loading ? "注册中..." : "注册"
                    enabled: RegisterViewModel.canSubmit
                             && !RegisterViewModel.loading

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
                        RegisterViewModel.submit()
                    }
                }

                Item {
                    Layout.preferredHeight: 32
                } // 底部间距
            }
        }
    }

    // ==================== 状态监听 ====================
    Connections {
        target: RegisterViewModel
        function onRegisterSucceeded(username, email) {
            root.registered(username, email)
        }
    }
}
