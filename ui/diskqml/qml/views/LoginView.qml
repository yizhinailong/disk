/**
 * @file LoginView.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 登录页面
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

    property string prefillAccount: ""

    signal registerRequested

    // ==================== 页面布局 ====================

    Rectangle {
        anchors.fill: parent
        color: "#FAFAFA"

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.8, 400)
            spacing: 16

            Label {
                text: qsTr("登录 Disk")
                font.pixelSize: 24
                font.bold: true
                color: "#212121"
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 32
            }

            // 错误信息
            Label {
                id: errorLabel
                visible: LoginViewModel.errorMessage !== ""
                text: LoginViewModel.errorMessage
                color: "#F44336"
                font.pixelSize: 14
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            TextField {
                id: accountField
                objectName: "accountField"
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                placeholderText: qsTr("账号或邮箱")
                text: LoginViewModel.account
                onTextChanged: LoginViewModel.account = text
                font.pixelSize: 14
                color: "#212121"
                enabled: !LoginViewModel.loading
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                TextField {
                    id: passwordField
                    objectName: "passwordField"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    placeholderText: qsTr("密码")
                    echoMode: showPasswordBtn.checked ? TextInput.Normal : TextInput.Password
                    text: LoginViewModel.password
                    onTextChanged: LoginViewModel.password = text
                    font.pixelSize: 14
                    color: "#212121"
                    enabled: !LoginViewModel.loading
                }

                Button {
                    id: showPasswordBtn
                    text: checked ? qsTr("隐藏") : qsTr("显示")
                    checkable: true
                    flat: true
                    Layout.preferredHeight: 36
                    enabled: !LoginViewModel.loading
                }
            }

            Button {
                id: loginButton
                objectName: "loginButton"
                text: LoginViewModel.loading ? qsTr("登录中...") : qsTr("登录")
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                Layout.topMargin: 16
                enabled: LoginViewModel.canSubmit && !LoginViewModel.loading
                
                background: Rectangle {
                    color: loginButton.enabled ? "#2196F3" : "#BDBDBD"
                    radius: 4
                }
                contentItem: Text {
                    text: loginButton.text
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 14
                }
                
                onClicked: {
                    LoginViewModel.submit()
                }
            }

            Button {
                id: createAccountButton
                objectName: "createAccountButton"
                text: qsTr("创建账号")
                Layout.alignment: Qt.AlignHCenter
                flat: true
                enabled: !LoginViewModel.loading
                onClicked: root.registerRequested()
            }
        }
    }

    // ==================== 状态联动 ====================
    onPrefillAccountChanged: {
        if (prefillAccount !== "") {
            LoginViewModel.account = prefillAccount
        }
    }
}
