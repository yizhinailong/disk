import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components/auth"

Item {
    id: root
    objectName: "authLoginPage"

    property bool isBusy: false

    function resetState() {
        usernameField.text = ""
        passwordField.text = ""
        errorLabel.text = ""
        root.isBusy = false
        usernameField.forceActiveFocus()
    }

    AuthTheme {
        id: theme
    }

    implicitWidth: loginCard.implicitWidth
    implicitHeight: loginCard.implicitHeight
    width: implicitWidth
    height: implicitHeight

    Component.onCompleted: resetState()

    AuthCard {
        id: loginCard

        anchors.fill: parent
        theme: theme
        eyebrowText: "Disk 桌面端"
        titleText: "欢迎回来"
        subtitleText: "使用用户名和密码继续。"

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

                onAccepted: loginButton.clicked()
            }

            Label {
                id: errorLabel
                objectName: "authErrorLabel"
                Layout.fillWidth: true
                color: theme.errorTextColor
                visible: text !== ""
                wrapMode: Text.WordWrap
            }

            Button {
                id: loginButton
                objectName: "authSubmitButton"
                Layout.fillWidth: true
                implicitHeight: theme.primaryCtaHeight
                text: root.isBusy ? "正在登录..." : "登录"
                enabled: !root.isBusy

                contentItem: Text {
                    text: loginButton.text
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
                    opacity: loginButton.enabled ? 1.0 : 0.65

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
                    errorLabel.text = ""

                    var username = usernameField.text.trim()
                    if (username === "") {
                        errorLabel.text = "请输入用户名"
                        usernameField.forceActiveFocus()
                        return
                    }

                    if (passwordField.text === "") {
                        errorLabel.text = "请输入密码"
                        passwordField.forceActiveFocus()
                        return
                    }

                    root.isBusy = true
                    sessionStore.owner.StartLogin()
                    authService.Login(username, passwordField.text)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.helperSpacing

                Label {
                    Layout.fillWidth: true
                    text: "没有账号？"
                    color: theme.bodyTextColor
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                }

                Button {
                    id: registerModeCta
                    objectName: "authModeSwitchCta"
                    text: "注册"
                    flat: true
                    enabled: !root.isBusy

                    contentItem: Text {
                        text: registerModeCta.text
                        color: theme.secondaryCtaTextColor
                        font.pixelSize: 13
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: theme.primaryCtaRadius
                        color: registerModeCta.down || registerModeCta.hovered ? theme.secondaryCtaHoverColor : "transparent"
                    }

                    onClicked: shellController.navigateToRegister()
                }
            }
        }
    }

    Connections {
        target: authService

        function onLoginSuccess(accessToken, refreshToken, expiresIn, user) {
            root.isBusy = false
        }

        function onLoginFailure(errorCode, message) {
            root.isBusy = false
            errorLabel.text = message || "登录失败。请重试。"
        }
    }
}
