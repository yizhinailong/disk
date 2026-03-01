import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root


    property string serverUrl
    property string prefillAccount: ""

    signal registerRequested()

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

            TextField {
                id: accountField
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                placeholderText: qsTr("账号或邮箱")
                text: root.prefillAccount
                font.pixelSize: 14
                color: "#212121"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                TextField {
                    id: passwordField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    placeholderText: qsTr("密码")
                    echoMode: showPasswordBtn.checked ? TextInput.Normal : TextInput.Password
                    font.pixelSize: 14
                    color: "#212121"
                }

                Button {
                    id: showPasswordBtn
                    text: checked ? qsTr("隐藏") : qsTr("显示")
                    checkable: true
                    flat: true
                    Layout.preferredHeight: 36
                }
            }

            Button {
                id: loginButton
                text: qsTr("登录")
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                Layout.topMargin: 16
                
                Material.background: "#2196F3"
                Material.foreground: "#FFFFFF"
                
                onClicked: {
                    console.log("Login requested for:", accountField.text)
                }
            }

            Button {
                id: createAccountButton
                objectName: "createAccountButton"
                text: qsTr("创建账号")
                Layout.alignment: Qt.AlignHCenter
                flat: true
                Material.foreground: "#2196F3"
                onClicked: root.registerRequested()
            }
        }
    }

    onPrefillAccountChanged: {
        if (prefillAccount !== "") {
            accountField.text = prefillAccount
        }
    }
}
