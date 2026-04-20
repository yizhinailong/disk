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
            text: "Share Access"
            font.pixelSize: 24
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: needsPassword ?
                  "This share requires a password" :
                  "Enter share code or paste share link"
            color: "#666"
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }

        TextField {
            id: shareIdField
            Layout.fillWidth: true
            placeholderText: "Share code"
            visible: !needsPassword && shareId === ""
            text: root.shareId
            onAccepted: verifyButton.clicked()
        }

        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "Password (4-8 characters)"
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
            text: "Access Share"
            highlighted: true

            onClicked: {
                errorLabel.text = ""
                var sid = root.shareId !== "" ? root.shareId : shareIdField.text.trim()
                if (sid === "") {
                    errorLabel.text = "Please enter a share code"
                    return
                }
                var pwd = passwordField.text.trim()
                authService.AccessShare(sid, pwd)
            }
        }
    }

    Connections {
        target: authService

        function onShareAccessSuccess(shareToken, expiresIn, permission, files) {
            shellController.navigateToVisitor(root.shareId || shareIdField.text.trim())
        }

        function onShareAccessFailure(errorCode, message) {
            errorLabel.text = message
            if (errorCode === 60003) {
                root.needsPassword = true
            }
        }
    }

    Component.onCompleted: {
        if (root.shareId !== "" && !root.needsPassword) {
            authService.AccessShare(root.shareId)
        }
    }
}
