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
    title: root.authMode === "login" ? "Disk 桌面端 - 登录" : "Disk 桌面端 - 注册"

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
                    text: root.authMode === "login" ? "平静的桌面工作台入口。" : "更安静的方式开启您的桌面工作空间。"
                    color: theme.heroTextColor
                    font.pixelSize: 38
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: root.authMode === "login" ? "在右侧登录，继续专注的桌面流程。" : "在右侧创建您的账号，然后返回此处登录。"
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
