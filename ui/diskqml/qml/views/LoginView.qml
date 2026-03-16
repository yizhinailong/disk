/**
 * @file LoginView.qml
 * @brief 用户登录视图
 * @author LiuFeng (liufeng.code@outlook.com)
 * @copyright Copyright (c) 2026
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0
import "../tokens"
import "../components/primitives"

Item {
    id: root

    property string prefillAccount: ""

    signal registerRequested

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // 左侧装饰区域
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: root.width * 0.45
            color: StyleTokens.colorBackground

            // 背景渐变
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop {
                        position: 0.0
                        color: Qt.tint(StyleTokens.colorBackground,
                                       Qt.rgba(StyleTokens.colorPrimary.r,
                                               StyleTokens.colorPrimary.g,
                                               StyleTokens.colorPrimary.b, 0.15))
                    }
                    GradientStop {
                        position: 1.0
                        color: Qt.tint(StyleTokens.colorBackground,
                                       Qt.rgba(StyleTokens.colorPrimary.r,
                                               StyleTokens.colorPrimary.g,
                                               StyleTokens.colorPrimary.b, 0.05))
                    }
                }
            }

            // 椭圆 1
            Rectangle {
                width: 300
                height: 300
                radius: 150
                color: Qt.rgba(1, 1, 1, 0.3)
                x: -50
                y: -50
            }
            // 椭圆 2
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
            // 椭圆 3
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
                spacing: StyleTokens.spacingMd

                Label {
                    text: qsTr("欢迎回来！")
                    font.pixelSize: 32
                    font.bold: true
                    color: StyleTokens.colorTextPrimary
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: qsTr("今天也要元气满满哦✨")
                    font.pixelSize: StyleTokens.fontSizeH1
                    color: StyleTokens.colorTextSecondary
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // 右侧表单区域
        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true

            Rectangle {
                anchors.centerIn: parent
                width: 420
                radius: StyleTokens.radiusXl
                color: StyleTokens.colorSurface
                implicitHeight: formLayout.implicitHeight + 64

                // 假阴影
                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.margins: -1
                    anchors.topMargin: StyleTokens.shadowOffsetYLg
                    anchors.bottomMargin: -StyleTokens.shadowOffsetYLg
                    radius: StyleTokens.radiusXl
                    color: StyleTokens.shadowColorLg
                }

                border.color: StyleTokens.colorBorder
                border.width: 1

                ColumnLayout {
                    id: formLayout
                    anchors.fill: parent
                    anchors.margins: StyleTokens.spacingXl
                    spacing: StyleTokens.spacingLg

                    // 标志和标题
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingSm

                        Label {
                            text: qsTr("Disk")
                            font.pixelSize: 64
                            font.bold: true
                            color: StyleTokens.colorPrimary
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Label {
                            text: qsTr("欢迎回来")
                            font.pixelSize: StyleTokens.fontSizeH1
                            font.bold: true
                            color: StyleTokens.colorTextPrimary
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Label {
                            text: qsTr("登录你的账号")
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                            Layout.alignment: Qt.AlignHCenter
                        }
                    }

                    // 胶囊标签页: 登录 | 注册
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        radius: 22
                        color: StyleTokens.colorBackground

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: StyleTokens.spacingXs
                            spacing: StyleTokens.spacingXs

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 18
                                color: StyleTokens.colorPrimary

                                Text {
                                    anchors.centerIn: parent
                                    text: qsTr("登录")
                                    color: StyleTokens.colorSurface
                                    font.pixelSize: StyleTokens.fontSizeBody
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
                                    color: StyleTokens.colorTextPrimary
                                    font.pixelSize: StyleTokens.fontSizeBody
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

                    // 输入字段
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingMd

                        AppTextInput {
                            id: accountField
                            objectName: "accountField"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            placeholderText: qsTr("账号（用户名/邮箱）")
                            text: LoginViewModel.account
                            enabled: !LoginViewModel.loading

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

                        AppTextInput {
                            id: passwordField
                            objectName: "passwordField"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            placeholderText: qsTr("密码")
                            echoMode: showPasswordBtn.checked ? TextInput.Normal : TextInput.Password
                            text: LoginViewModel.password
                            enabled: !LoginViewModel.loading
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

                            AppButton {
                                id: showPasswordBtn
                                variant: "icon"
                                checkable: true
                                anchors.right: parent.right
                                anchors.rightMargin: StyleTokens.spacingSm
                                anchors.verticalCenter: parent.verticalCenter
                                width: 36
                                height: 36
                                enabled: !LoginViewModel.loading
                                
                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: "👁"
                                    font.pixelSize: StyleTokens.fontSizeH2
                                    color: showPasswordBtn.checked ? StyleTokens.colorPrimary : StyleTokens.colorTextTertiary
                                }
                            }
                        }
                    }

                    // 错误文本
                    Label {
                        id: errorLabel
                        visible: LoginViewModel.errorMessage !== ""
                        text: LoginViewModel.errorMessage
                        color: StyleTokens.colorError
                        font.pixelSize: StyleTokens.fontSizeSmall
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        Layout.preferredHeight: visible ? implicitHeight : 0
                        Layout.topMargin: visible ? -StyleTokens.spacingSm : -StyleTokens.spacingMd
                        Layout.bottomMargin: visible ? 0 : -StyleTokens.spacingMd
                    }

                    // 登录按钮
                    AppButton {
                        id: loginButton
                        objectName: "loginButton"
                        variant: "primary"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        Layout.topMargin: StyleTokens.spacingSm
                        enabled: LoginViewModel.canSubmit && !LoginViewModel.loading

                        contentItem: Item {
                            anchors.fill: parent

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: StyleTokens.spacingSm

                                BusyIndicator {
                                    running: LoginViewModel.loading
                                    visible: LoginViewModel.loading
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                }

                                Text {
                                    text: LoginViewModel.loading ? qsTr("登录中...") : qsTr("登 录")
                                    color: loginButton.enabled ? StyleTokens.colorSurface : StyleTokens.colorTextTertiary
                                    font.pixelSize: StyleTokens.fontSizeH2
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
