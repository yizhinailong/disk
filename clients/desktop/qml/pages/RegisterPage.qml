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
            return "用户名必须为 4-32 个字符"
        }
        if (!/^[A-Za-z0-9_]+$/.test(username)) {
            return "用户名只能包含字母、数字和下划线"
        }
        return ""
    }

    function validateEmail(email) {
        if (email === "") {
            return "请输入邮箱"
        }
        if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
            return "请输入有效的邮箱地址"
        }
        return ""
    }

    function validatePassword(password) {
        if (password.length < 8 || password.length > 64) {
            return "密码必须为 8-64 个字符"
        }
        if (!/^[A-Za-z0-9]+$/.test(password)) {
            return "密码只能包含字母和数字"
        }
        if (!/[a-z]/.test(password) || !/[A-Z]/.test(password) || !/[0-9]/.test(password)) {
            return "密码必须包含大写字母、小写字母和数字"
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
        eyebrowText: "Disk 桌面端"
        titleText: "创建账户"
        subtitleText: "先注册，然后使用新账号登录"

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.fieldSpacing

            TextField {
                id: usernameField
                objectName: "authUsernameField"
                Layout.fillWidth: true
                implicitHeight: theme.primaryCtaHeight
                placeholderText: "用户名"
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
                placeholderText: "邮箱"
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
                placeholderText: "密码"
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
                placeholderText: "确认密码"
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
                text: root.isBusy ? "正在创建账户..." : "创建账户"
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
                        messageLabel.text = "两次输入的密码不一致"
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
                    text: "已有账号？"
                    color: theme.bodyTextColor
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                }

                Button {
                    id: loginModeCta
                    objectName: "authModeSwitchCta"
                    text: "返回登录"
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
            messageLabel.text = "已为 " + (user.username || usernameField.text.trim()) + " 创建账户。请登录。"
            returnToLoginTimer.start()
        }

        function onRegisterFailure(errorCode, message) {
            root.isBusy = false
            messageLabel.color = theme.errorTextColor
            messageLabel.text = message || "注册失败。请检查您的输入。"
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
