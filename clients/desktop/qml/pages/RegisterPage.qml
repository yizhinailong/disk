import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/auth"

Item {
    id: root
    objectName: "authRegisterPage"

    property bool isBusy: false

    function resetState() {
        usernameField.text = ""
        emailField.text = ""
        passwordField.text = ""
        confirmPasswordField.text = ""
        messageLabel.text = ""
        messageLabel.color = theme.errorTextColor
        root.isBusy = false
        returnToLoginTimer.stop()
        usernameField.forceActiveFocus()
    }

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

    AuthTheme {
        id: theme
    }

    implicitWidth: registerCard.implicitWidth
    implicitHeight: registerCard.implicitHeight
    width: implicitWidth
    height: implicitHeight

    Component.onCompleted: resetState()

    AuthCard {
        id: registerCard

        anchors.fill: parent
        theme: theme
        eyebrowText: "Disk desktop"
        titleText: "Create account"
        subtitleText: "Register first, then sign in with your new account"

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.fieldSpacing

            TextField {
                id: usernameField
                objectName: "authUsernameField"
                Layout.fillWidth: true
                implicitHeight: theme.primaryCtaHeight
                placeholderText: "Username"
                placeholderTextColor: theme.fieldPlaceholderColor
                color: theme.fieldTextColor
                maximumLength: 32
                enabled: !root.isBusy
                leftPadding: theme.helperSpacing
                rightPadding: theme.helperSpacing
                selectByMouse: true

                background: Rectangle {
                    radius: theme.primaryCtaRadius
                    color: theme.fieldBackgroundColor
                    border.width: 1
                    border.color: usernameField.activeFocus ? theme.fieldFocusBorderColor : theme.fieldBorderColor
                }

                onAccepted: emailField.forceActiveFocus()
            }

            TextField {
                id: emailField
                objectName: "authEmailField"
                Layout.fillWidth: true
                implicitHeight: theme.primaryCtaHeight
                placeholderText: "Email"
                placeholderTextColor: theme.fieldPlaceholderColor
                color: theme.fieldTextColor
                inputMethodHints: Qt.ImhEmailCharactersOnly
                enabled: !root.isBusy
                leftPadding: theme.helperSpacing
                rightPadding: theme.helperSpacing
                selectByMouse: true

                background: Rectangle {
                    radius: theme.primaryCtaRadius
                    color: theme.fieldBackgroundColor
                    border.width: 1
                    border.color: emailField.activeFocus ? theme.fieldFocusBorderColor : theme.fieldBorderColor
                }

                onAccepted: passwordField.forceActiveFocus()
            }

            TextField {
                id: passwordField
                objectName: "authPasswordField"
                Layout.fillWidth: true
                implicitHeight: theme.primaryCtaHeight
                placeholderText: "Password"
                placeholderTextColor: theme.fieldPlaceholderColor
                color: theme.fieldTextColor
                echoMode: TextInput.Password
                maximumLength: 64
                enabled: !root.isBusy
                leftPadding: theme.helperSpacing
                rightPadding: theme.helperSpacing
                selectByMouse: true

                background: Rectangle {
                    radius: theme.primaryCtaRadius
                    color: theme.fieldBackgroundColor
                    border.width: 1
                    border.color: passwordField.activeFocus ? theme.fieldFocusBorderColor : theme.fieldBorderColor
                }

                onAccepted: confirmPasswordField.forceActiveFocus()
            }

            TextField {
                id: confirmPasswordField
                objectName: "authConfirmPasswordField"
                Layout.fillWidth: true
                implicitHeight: theme.primaryCtaHeight
                placeholderText: "Confirm password"
                placeholderTextColor: theme.fieldPlaceholderColor
                color: theme.fieldTextColor
                echoMode: TextInput.Password
                maximumLength: 64
                enabled: !root.isBusy
                leftPadding: theme.helperSpacing
                rightPadding: theme.helperSpacing
                selectByMouse: true

                background: Rectangle {
                    radius: theme.primaryCtaRadius
                    color: theme.fieldBackgroundColor
                    border.width: 1
                    border.color: confirmPasswordField.activeFocus ? theme.fieldFocusBorderColor : theme.fieldBorderColor
                }

                onAccepted: registerButton.clicked()
            }

            Label {
                id: messageLabel
                objectName: "authErrorLabel"
                Layout.fillWidth: true
                color: theme.errorTextColor
                visible: text !== ""
                wrapMode: Text.WordWrap
            }

            Button {
                id: registerButton
                objectName: "authSubmitButton"
                Layout.fillWidth: true
                implicitHeight: theme.primaryCtaHeight
                text: root.isBusy ? "Creating account..." : "Create account"
                enabled: !root.isBusy

                contentItem: Text {
                    text: registerButton.text
                    color: theme.primaryCtaTextColor
                    font.pixelSize: 15
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: theme.primaryCtaRadius
                    border.width: theme.primaryCtaBorderWidth
                    border.color: theme.primaryCtaEndColor
                    opacity: registerButton.enabled ? 1.0 : 0.65

                    gradient: Gradient {
                        GradientStop {
                            position: 0.0
                            color: theme.primaryCtaStartColor
                        }

                        GradientStop {
                            position: 1.0
                            color: theme.primaryCtaEndColor
                        }
                    }
                }

                onClicked: {
                    messageLabel.color = theme.errorTextColor
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

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.helperSpacing

                Label {
                    Layout.fillWidth: true
                    text: "Already registered?"
                    color: theme.bodyTextColor
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                }

                Button {
                    id: loginModeCta
                    objectName: "authModeSwitchCta"
                    text: "Back to login"
                    flat: true
                    enabled: !root.isBusy

                    contentItem: Text {
                        text: loginModeCta.text
                        color: theme.secondaryCtaTextColor
                        font.pixelSize: 13
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: theme.primaryCtaRadius
                        color: loginModeCta.down || loginModeCta.hovered ? theme.secondaryCtaHoverColor : "transparent"
                    }

                    onClicked: shellController.navigateToLogin()
                }
            }
        }
    }

    Connections {
        target: authService

        function onRegisterSuccess(user) {
            root.isBusy = false
            messageLabel.color = theme.successTextColor
            messageLabel.text = "Account created for " + (user.username || usernameField.text.trim()) + ". Please sign in."
            returnToLoginTimer.start()
        }

        function onRegisterFailure(errorCode, message) {
            root.isBusy = false
            messageLabel.color = theme.errorTextColor
            messageLabel.text = message || "Registration failed. Please check your input."
        }
    }

    Timer {
        id: returnToLoginTimer
        objectName: "authRegisterSuccessTimer"
        interval: 1200
        repeat: false
        onTriggered: shellController.navigateToLogin()
    }
}
