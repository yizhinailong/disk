import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    signal registered(string username, string email)
    signal backRequested

    property bool showPassword: false

    function handleInput() {
        if (RegisterViewModel.errorMessage !== "") {
            RegisterViewModel.clearError()
        }
    }

    Connections {
        target: RegisterViewModel
        function onRegisterSucceeded(username, email) {
            root.registered(username, email)
            RegisterViewModel.email = ""
            RegisterViewModel.username = ""
            RegisterViewModel.password = ""
            RegisterViewModel.confirmPassword = ""
            RegisterViewModel.clearError()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== 左侧装饰区 ====================
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.preferredWidth: parent.width * 0.45
            
            gradient: Gradient {
                GradientStop { position: 0.0; color: palette.highlight }
                GradientStop { position: 1.0; color: Qt.darker(palette.highlight, 1.3) }
            }
            
            // 装饰圆
            Rectangle {
                width: 300; height: 300
                radius: 150
                color: Qt.rgba(1, 1, 1, 0.1)
                x: -50; y: -50
            }
            Rectangle {
                width: 400; height: 400
                radius: 200
                color: Qt.rgba(1, 1, 1, 0.05)
                anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.rightMargin: -100; anchors.bottomMargin: -100
            }

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 16
                Label {
                    text: "加入我们吧！"
                    font.pixelSize: 36
                    font.bold: true
                    color: "#FFFFFF"
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: "30秒快速注册✨"
                    font.pixelSize: 20
                    color: Qt.rgba(1, 1, 1, 0.8)
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // ==================== 右侧表单区 ====================
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.preferredWidth: parent.width * 0.55
            color: palette.window

            ScrollView {
                anchors.fill: parent
                contentWidth: availableWidth
                clip: true

                // 表单居中容器
                Item {
                    width: parent.width
                    height: Math.max(parent.height, formCard.height + 64)

                    // 居中卡片
                    Rectangle {
                        id: formCard
                        width: 420
                        height: formLayout.implicitHeight + 64
                        anchors.centerIn: parent
                        color: palette.base
                        radius: 16
                        
                        // 简单阴影效果
                        border.color: Qt.rgba(0, 0, 0, 0.08)
                        border.width: 1
                        
                        // 模拟更柔和的阴影可以在底层加一个带位移的深色矩形
                        Rectangle {
                            z: -1
                            anchors.fill: parent
                            anchors.margins: -1
                            anchors.horizontalCenterOffset: 0
                            anchors.verticalCenterOffset: 4
                            radius: 16
                            color: Qt.rgba(0, 0, 0, 0.04)
                        }

                        ColumnLayout {
                            id: formLayout
                            anchors.fill: parent
                            anchors.margins: 32
                            spacing: 16

                            // Logo
                            Label {
                                text: "Disk"
                                font.pixelSize: 28
                                font.bold: true
                                color: palette.text
                                Layout.alignment: Qt.AlignHCenter
                            }

                            // 切换标签
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 40
                                radius: 20
                                color: palette.alternateBase

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 4
                                    anchors.margins: 4

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        radius: 16
                                        color: "transparent"
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: "登录"
                                            color: palette.text
                                            font.pixelSize: 14
                                        }
                                        MouseArea {
                                            objectName: "tabLogin"
                                            anchors.fill: parent
                                            onClicked: root.backRequested()
                                        }
                                    }

                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        radius: 16
                                        color: palette.base
                                        border.color: Qt.rgba(0, 0, 0, 0.05)

                                        Text {
                                            anchors.centerIn: parent
                                            text: "注册"
                                            color: palette.text
                                            font.pixelSize: 14
                                            font.bold: true
                                        }
                                        MouseArea {
                                            objectName: "tabRegister"
                                            anchors.fill: parent
                                        }
                                    }
                                }
                            }

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

                            // 1. 邮箱
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                TextField {
                                    id: emailInput
                                    objectName: "emailInput"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    placeholderText: "邮箱"
                                    enabled: !RegisterViewModel.loading
                                    text: RegisterViewModel.email
                                    onTextChanged: {
                                        root.handleInput()
                                        RegisterViewModel.email = text
                                    }
                                    background: Rectangle {
                                        radius: 12
                                        color: palette.alternateBase
                                        border.color: RegisterViewModel.emailError !== "" ? "#F44336" : (emailInput.activeFocus ? palette.highlight : "transparent")
                                    }
                                }
                                Label {
                                    text: RegisterViewModel.emailError
                                    color: "#F44336"
                                    font.pixelSize: 12
                                    visible: RegisterViewModel.emailError !== ""
                                }
                            }

                            // 2. 用户名
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                TextField {
                                    id: usernameInput
                                    objectName: "usernameInput"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    placeholderText: "用户名"
                                    enabled: !RegisterViewModel.loading
                                    text: RegisterViewModel.username
                                    onTextChanged: {
                                        root.handleInput()
                                        RegisterViewModel.username = text
                                    }
                                    background: Rectangle {
                                        radius: 12
                                        color: palette.alternateBase
                                        border.color: RegisterViewModel.usernameError !== "" ? "#F44336" : (usernameInput.activeFocus ? palette.highlight : "transparent")
                                    }
                                }
                                Label {
                                    text: RegisterViewModel.usernameError
                                    color: "#F44336"
                                    font.pixelSize: 12
                                    visible: RegisterViewModel.usernameError !== ""
                                }
                            }

                            // 3. 密码
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                TextField {
                                    id: passwordInput
                                    objectName: "passwordInput"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    placeholderText: "密码"
                                    echoMode: root.showPassword ? TextInput.Normal : TextInput.Password
                                    enabled: !RegisterViewModel.loading
                                    text: RegisterViewModel.password
                                    onTextChanged: {
                                        root.handleInput()
                                        RegisterViewModel.password = text
                                    }
                                    rightPadding: 40
                                    background: Rectangle {
                                        radius: 12
                                        color: palette.alternateBase
                                        border.color: RegisterViewModel.passwordError !== "" ? "#F44336" : (passwordInput.activeFocus ? palette.highlight : "transparent")
                                    }

                                    // 眼睛图标
                                    Text {
                                        text: root.showPassword ? "👁" : "👁‍🗨"
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.rightMargin: 16
                                        font.pixelSize: 16
                                        color: palette.placeholderText
                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -8
                                            onClicked: root.showPassword = !root.showPassword
                                        }
                                    }
                                }
                                Label {
                                    text: RegisterViewModel.passwordError
                                    color: "#F44336"
                                    font.pixelSize: 12
                                    visible: RegisterViewModel.passwordError !== ""
                                }
                            }

                            // 4. 确认密码
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                TextField {
                                    id: confirmPasswordInput
                                    objectName: "confirmPasswordInput"
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 52
                                    placeholderText: "确认密码"
                                    echoMode: root.showPassword ? TextInput.Normal : TextInput.Password
                                    enabled: !RegisterViewModel.loading
                                    text: RegisterViewModel.confirmPassword
                                    onTextChanged: {
                                        root.handleInput()
                                        RegisterViewModel.confirmPassword = text
                                    }
                                    rightPadding: 40
                                    background: Rectangle {
                                        radius: 12
                                        color: palette.alternateBase
                                        border.color: RegisterViewModel.confirmPasswordError !== "" ? "#F44336" : (confirmPasswordInput.activeFocus ? palette.highlight : "transparent")
                                    }

                                    Text {
                                        text: "✓"
                                        color: "#4CAF50"
                                        font.pixelSize: 18
                                        font.bold: true
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.rightMargin: 16
                                        visible: confirmPasswordInput.text !== "" && confirmPasswordInput.text === passwordInput.text
                                    }
                                }
                                Label {
                                    text: RegisterViewModel.confirmPasswordError
                                    color: "#F44336"
                                    font.pixelSize: 12
                                    visible: RegisterViewModel.confirmPasswordError !== ""
                                }
                            }

                            Item { Layout.preferredHeight: 8 }

                            // 注册按钮
                            Button {
                                id: submitButton
                                objectName: "submitButton"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 52
                                enabled: RegisterViewModel.canSubmit && !RegisterViewModel.loading

                                background: Rectangle {
                                    color: submitButton.enabled ? palette.highlight : palette.alternateBase
                                    radius: 12
                                    opacity: RegisterViewModel.loading ? 0.7 : 1.0
                                }
                                
                                contentItem: Item {
                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: 8
                                        BusyIndicator {
                                            running: RegisterViewModel.loading
                                            visible: RegisterViewModel.loading
                                            Layout.preferredWidth: 20
                                            Layout.preferredHeight: 20
                                        }
                                        Text {
                                            text: RegisterViewModel.loading ? "注册中..." : "注 册"
                                            color: submitButton.enabled || RegisterViewModel.loading ? palette.highlightedText : palette.placeholderText
                                            font.pixelSize: 16
                                            font.bold: true
                                        }
                                    }
                                }

                                onClicked: RegisterViewModel.submit()
                            }
                            
                            // 协议
                            Label {
                                text: "注册即表示同意《用户协议》和《隐私政策》"
                                font.pixelSize: 12
                                color: palette.placeholderText
                                Layout.alignment: Qt.AlignHCenter
                            }
                            
                            // 底部提示
                            RowLayout {
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 4
                                Label {
                                    text: "已有账号？"
                                    font.pixelSize: 14
                                    color: palette.text
                                }
                                Label {
                                    text: "直接登录"
                                    font.pixelSize: 14
                                    color: palette.highlight
                                    font.bold: true
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.backRequested()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}