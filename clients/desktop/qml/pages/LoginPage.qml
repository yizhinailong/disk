import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    property bool isBusy: false
    
    ColumnLayout {
        anchors.centerIn: parent
        width: 300
        spacing: 16
        
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Login"
            font.pixelSize: 24
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            text: "Sign in to access your cloud drive"
            color: "#666"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        
        TextField {
            id: usernameField
            Layout.fillWidth: true
            placeholderText: "Username"
            enabled: !root.isBusy
            onAccepted: passwordField.forceActiveFocus()
        }
        
        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "Password"
            echoMode: TextInput.Password
            enabled: !root.isBusy
            onAccepted: loginButton.clicked()
        }

        Label {
            id: errorLabel
            Layout.fillWidth: true
            color: "#f44336"
            visible: text !== ""
            wrapMode: Text.WordWrap
        }
        
        Button {
            id: loginButton
            Layout.fillWidth: true
            text: root.isBusy ? "Signing in..." : "Login"
            enabled: !root.isBusy
            highlighted: true
            onClicked: {
                errorLabel.text = ""

                var username = usernameField.text.trim()
                if (username === "") {
                    errorLabel.text = "Please enter your username"
                    usernameField.forceActiveFocus()
                    return
                }

                if (passwordField.text === "") {
                    errorLabel.text = "Please enter your password"
                    passwordField.forceActiveFocus()
                    return
                }

                root.isBusy = true
                sessionStore.owner.StartLogin()
                authService.Login(username, passwordField.text)
            }
        }
        
        Button {
            Layout.fillWidth: true
            text: "Register"
            flat: true
            enabled: !root.isBusy
            onClicked: shellController.navigateToRegister()
        }
    }

    Connections {
        target: authService

        function onLoginSuccess(accessToken, refreshToken, expiresIn, user) {
            root.isBusy = false
        }

        function onLoginFailure(errorCode, message) {
            root.isBusy = false
            errorLabel.text = message || "Login failed. Please try again."
        }
    }
}
