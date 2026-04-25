import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    property bool isBusy: false

    function validateUsername(username) {
        if (username.length < 4 || username.length > 32) {
            return "Username must be 4-32 characters"
        }
        if (!/^[A-Za-z0-9_]+$/.test(username)) {
            return "Username may contain only letters, digits, and underscores"
        }
        return ""
    }

    function validateEmail(email) {
        if (email === "") {
            return "Please enter your email"
        }
        if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
            return "Please enter a valid email address"
        }
        return ""
    }

    function validatePassword(password) {
        if (password.length < 8 || password.length > 64) {
            return "Password must be 8-64 characters"
        }
        if (!/^[A-Za-z0-9]+$/.test(password)) {
            return "Password may contain only letters and digits"
        }
        if (!/[a-z]/.test(password) || !/[A-Z]/.test(password) || !/[0-9]/.test(password)) {
            return "Password must include uppercase, lowercase, and digits"
        }
        return ""
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: 360
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: "Create Account"
            font.pixelSize: 24
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            text: "Register first, then sign in with your new account"
            color: "#666"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        TextField {
            id: usernameField
            Layout.fillWidth: true
            placeholderText: "Username"
            maximumLength: 32
            enabled: !root.isBusy
            onAccepted: emailField.forceActiveFocus()
        }

        TextField {
            id: emailField
            Layout.fillWidth: true
            placeholderText: "Email"
            inputMethodHints: Qt.ImhEmailCharactersOnly
            enabled: !root.isBusy
            onAccepted: passwordField.forceActiveFocus()
        }

        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "Password"
            echoMode: TextInput.Password
            maximumLength: 64
            enabled: !root.isBusy
            onAccepted: confirmPasswordField.forceActiveFocus()
        }

        TextField {
            id: confirmPasswordField
            Layout.fillWidth: true
            placeholderText: "Confirm password"
            echoMode: TextInput.Password
            maximumLength: 64
            enabled: !root.isBusy
            onAccepted: registerButton.clicked()
        }

        Label {
            id: messageLabel
            Layout.fillWidth: true
            color: "#f44336"
            visible: text !== ""
            wrapMode: Text.WordWrap
        }

        Button {
            id: registerButton
            Layout.fillWidth: true
            text: root.isBusy ? "Creating account..." : "Create account"
            enabled: !root.isBusy
            highlighted: true

            onClicked: {
                messageLabel.color = "#f44336"
                messageLabel.text = ""

                var username = usernameField.text.trim()
                var email = emailField.text.trim()
                var password = passwordField.text

                var validationMessage = root.validateUsername(username)
                if (validationMessage !== "") {
                    messageLabel.text = validationMessage
                    usernameField.forceActiveFocus()
                    return
                }

                validationMessage = root.validateEmail(email)
                if (validationMessage !== "") {
                    messageLabel.text = validationMessage
                    emailField.forceActiveFocus()
                    return
                }

                validationMessage = root.validatePassword(password)
                if (validationMessage !== "") {
                    messageLabel.text = validationMessage
                    passwordField.forceActiveFocus()
                    return
                }

                if (password !== confirmPasswordField.text) {
                    messageLabel.text = "The two passwords do not match"
                    confirmPasswordField.forceActiveFocus()
                    return
                }

                root.isBusy = true
                authService.Register(username, email, password)
            }
        }

        Button {
            Layout.fillWidth: true
            text: "Back to login"
            flat: true
            enabled: !root.isBusy
            onClicked: shellController.navigateToLogin()
        }
    }

    Connections {
        target: authService

        function onRegisterSuccess(user) {
            root.isBusy = false
            messageLabel.color = "#2e7d32"
            messageLabel.text = "Account created for " + (user.username || usernameField.text.trim()) + ". Please sign in."
            returnToLoginTimer.start()
        }

        function onRegisterFailure(errorCode, message) {
            root.isBusy = false
            messageLabel.color = "#f44336"
            messageLabel.text = message || "Registration failed. Please check your input."
        }
    }

    Timer {
        id: returnToLoginTimer
        interval: 1200
        repeat: false
        onTriggered: shellController.navigateToLogin()
    }
}
