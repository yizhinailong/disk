import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    property string shareId: ""
    property bool needsPassword: false

    ColumnLayout {
        anchors.centerIn: parent
        width: 360
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "分享访问"
            font.pixelSize: 24
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: needsPassword ?
                  "此分享需要密码" :
                  "输入分享码或粘贴分享链接"
            color: "#666"
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        TextField {
            id: shareIdField
            Layout.fillWidth: true
            placeholderText: "分享码"
            visible: !needsPassword && shareId === ""
            text: root.shareId
            onAccepted: verifyButton.clicked()
        }

        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "密码（4-8个字符）"
            echoMode: TextInput.Password
            visible: needsPassword || shareId !== ""
            maximumLength: 8
            onAccepted: verifyButton.clicked()
        }

        Label {
            id: errorLabel
            Layout.fillWidth: true
            color: "#f44336"
            visible: text !== ""
            wrapMode: Text.WordWrap
        }

        Button {
            id: verifyButton
            Layout.fillWidth: true
            text: "访问分享"
            highlighted: true

            onClicked: {
                errorLabel.text = ""
                var sid = root.shareId !== "" ? root.shareId : shareIdField.text.trim()
                if (sid === "") {
                    errorLabel.text = "请输入分享码"
                    return
                }
                var pwd = passwordField.text.trim()
                if (sessionStore.visitor.shareId !== sid) {
                    sessionStore.ActivateVisitor(sid)
                }
                sessionStore.visitor.StartVerify(pwd)
            }
        }
    }

    Connections {
        target: authService

        function onShareAccessFailure(errorCode, message) {
            errorLabel.text = message
            if (errorCode === 60003) {
                root.needsPassword = true
            }
        }
    }

    onShareIdChanged: {
        if (root.shareId !== "" && !root.needsPassword) {
            sessionStore.visitor.StartVerify()
        }
    }
}
