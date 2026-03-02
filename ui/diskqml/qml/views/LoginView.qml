import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    property string prefillAccount: ""

    signal registerRequested

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

            // Error Message
            Label {
                id: errorLabel
                visible: AppContext.loginViewModel.errorMessage !== ""
                text: AppContext.loginViewModel.errorMessage
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
                text: AppContext.loginViewModel.account
                onTextChanged: AppContext.loginViewModel.account = text
                font.pixelSize: 14
                color: "#212121"
                enabled: !AppContext.loginViewModel.loading
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
                    text: AppContext.loginViewModel.password
                    onTextChanged: AppContext.loginViewModel.password = text
                    font.pixelSize: 14
                    color: "#212121"
                    enabled: !AppContext.loginViewModel.loading
                }

                Button {
                    id: showPasswordBtn
                    text: checked ? qsTr("隐藏") : qsTr("显示")
                    checkable: true
                    flat: true
                    Layout.preferredHeight: 36
                    enabled: !AppContext.loginViewModel.loading
                }
            }

            Button {
                id: loginButton
                objectName: "loginButton"
                text: AppContext.loginViewModel.loading ? qsTr("登录中...") : qsTr("登录")
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                Layout.topMargin: 16
                enabled: AppContext.loginViewModel.canSubmit && !AppContext.loginViewModel.loading
                
                Material.background: "#2196F3"
                Material.foreground: "#FFFFFF"
                
                onClicked: {
                    AppContext.loginViewModel.submit()
                }
            }

            Button {
                id: createAccountButton
                objectName: "createAccountButton"
                text: qsTr("创建账号")
                Layout.alignment: Qt.AlignHCenter
                flat: true
                Material.foreground: "#2196F3"
                enabled: !AppContext.loginViewModel.loading
                onClicked: root.registerRequested()
            }
        }
    }

    // Handle prefill from parent (e.g., after registration)
    onPrefillAccountChanged: {
        if (prefillAccount !== "") {
            AppContext.loginViewModel.account = prefillAccount
        }
    }
}
