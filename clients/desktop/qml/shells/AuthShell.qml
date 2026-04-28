import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../pages"
import "../components/auth"

ApplicationWindow {
    id: root
    objectName: "authShell"
    visible: true
    width: 1024
    height: 768
    title: root.authMode === "login" ? "Disk Desktop - Login" : "Disk Desktop - Register"

    property string authMode: "login"
    readonly property bool busy: pageLoader.item && pageLoader.item.isBusy || false

    AuthTheme {
        id: theme
    }

    color: theme.pageBackgroundColor

    background: Rectangle {
        color: theme.pageBackgroundColor
    }

    onAuthModeChanged: {
        if (_modeGuard) return
        if (root.busy) {
            _modeGuard = true
            authMode = (authMode === "login") ? "register" : "login"
            _modeGuard = false
            return
        }
    }

    property bool _modeGuard: false

    RowLayout {
        anchors.fill: parent
        anchors.margins: theme.pageOuterPadding
        spacing: theme.panelGap

        Rectangle {
            Layout.preferredWidth: theme.heroWidth
            Layout.fillHeight: true
            radius: theme.cardRadius + theme.helperSpacing
            clip: true

            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: theme.heroGradientStartColor
                }

                GradientStop {
                    position: 1.0
                    color: theme.heroGradientEndColor
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.contentHorizontalPadding
                spacing: theme.sectionSpacing

                Item {
                    Layout.fillHeight: true
                }

                Label {
                    Layout.fillWidth: true
                    text: "DISK DESKTOP"
                    color: theme.secondaryCtaTextColor
                    font.pixelSize: 13
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    text: root.authMode === "login" ? "Calm entry for your desktop workspace." : "A quieter way to start your desktop workspace."
                    color: theme.heroTextColor
                    font.pixelSize: 38
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: root.authMode === "login" ? "Sign in on the right and continue with the focused desktop flow." : "Create your account on the right, then return here to sign in."
                    color: theme.heroMutedTextColor
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Loader {
                id: pageLoader
                anchors.fill: parent
                sourceComponent: root.authMode === "login" ? loginPageComponent : registerPageComponent
            }
        }
    }

    Component {
        id: loginPageComponent

        Item {
            anchors.fill: parent
            readonly property alias isBusy: loginPage.isBusy

            LoginPage {
                id: loginPage
                anchors.centerIn: parent
            }
        }
    }

    Component {
        id: registerPageComponent

        Item {
            anchors.fill: parent
            readonly property alias isBusy: registerPage.isBusy

            RegisterPage {
                id: registerPage
                anchors.centerIn: parent
            }
        }
    }
}
