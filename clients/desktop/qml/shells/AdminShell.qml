import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/admin"

ApplicationWindow {
    id: root
    objectName: "adminShellWindow"
    visible: true
    width: 1024
    height: 768
    title: "Disk 管理后台"

    WorkspaceTheme { id: workspaceTheme }

    readonly property int railWidth: workspaceTheme.railWidth
    readonly property int railSectionSpacing: workspaceTheme.railSectionSpacing
    readonly property int railOuterPadding: workspaceTheme.railOuterPadding
    readonly property color railBackgroundColor: workspaceTheme.railBackgroundColor
    readonly property color railBorderColor: workspaceTheme.railBorderColor
    readonly property color railPanelColor: workspaceTheme.railPanelColor
    readonly property color railTextColor: workspaceTheme.railTextColor
    readonly property color railMutedTextColor: workspaceTheme.railMutedTextColor
    readonly property color railHoverColor: workspaceTheme.railHoverColor
    readonly property color railActiveColor: workspaceTheme.railActiveColor
    readonly property color railActiveStripeColor: workspaceTheme.railActiveStripeColor
    readonly property color railLogoutColor: workspaceTheme.railLogoutColor
    readonly property color headerMutedTextColor: workspaceTheme.headerMutedTextColor
    readonly property color contentBackgroundColor: workspaceTheme.pageBackgroundColor
    readonly property color contentBorderColor: workspaceTheme.contentBorderColor

    readonly property string currentPageTitle: root.destinationTitle(root.activeDestination)
    readonly property string currentPageSubtitle: root.destinationSubtitle(root.activeDestination)

    readonly property string accountDisplayName: profileManager.userProfile.nickname
                                                 || profileManager.userProfile.username
                                                 || "Admin"
    readonly property string accountSecondaryText: profileManager.userProfile.username
                                                    ? "@" + profileManager.userProfile.username
                                                    : "管理员"

    property string activeDestination: "users"

    function activateNavPage(itemId) {
        root.activeDestination = itemId
    }

    function destinationTitle(destination) {
        switch (destination) {
        case "users": return "用户管理"
        case "shares": return "分享管理"
        case "system": return "系统监控"
        default: return "用户管理"
        }
    }

    function destinationSubtitle(destination) {
        switch (destination) {
        case "users": return "管理系统用户账户"
        case "shares": return "查看和管理分享链接"
        case "system": return "监控系统状态和统计"
        default: return "管理系统用户账户"
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: root.railWidth
            Layout.fillHeight: true
            color: root.railBackgroundColor
            border.color: root.railBorderColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: root.railOuterPadding
                spacing: root.railSectionSpacing

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104
                    color: root.railPanelColor
                    radius: 10
                    border.color: root.railBorderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 4

                        Label {
                            text: "Disk"
                            color: root.railTextColor
                            font.pixelSize: 24
                            font.bold: true
                        }

                        Label {
                            text: "管理后台"
                            color: root.railMutedTextColor
                            font.pixelSize: 12
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 200
                    objectName: "adminNavigationPanel"
                    color: root.railPanelColor
                    radius: 10
                    border.color: root.railBorderColor

                    ColumnLayout {
                        objectName: "adminNavigationContent"
                        anchors.fill: parent
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            text: "管理"
                            color: root.railMutedTextColor
                            font.pixelSize: 11
                            font.bold: true
                            leftPadding: 8
                            topPadding: 4
                            bottomPadding: 8
                        }

                        OwnerSidebarNavButton {
                            objectName: "adminNavUsersButton"
                            Layout.fillWidth: true
                            buttonText: "用户管理"
                            active: root.activeDestination === "users"
                            activeFillColor: root.railActiveColor
                            hoverFillColor: root.railHoverColor
                            activeStripeColor: root.railActiveStripeColor
                            activeTextColor: root.railTextColor
                            idleTextColor: root.railMutedTextColor
                            onClicked: root.activateNavPage("users")
                        }

                        OwnerSidebarNavButton {
                            objectName: "adminNavSharesButton"
                            Layout.fillWidth: true
                            buttonText: "分享管理"
                            active: root.activeDestination === "shares"
                            activeFillColor: root.railActiveColor
                            hoverFillColor: root.railHoverColor
                            activeStripeColor: root.railActiveStripeColor
                            activeTextColor: root.railTextColor
                            idleTextColor: root.railMutedTextColor
                            onClicked: root.activateNavPage("shares")
                        }

                        OwnerSidebarNavButton {
                            objectName: "adminNavSystemButton"
                            Layout.fillWidth: true
                            buttonText: "系统监控"
                            active: root.activeDestination === "system"
                            activeFillColor: root.railActiveColor
                            hoverFillColor: root.railHoverColor
                            activeStripeColor: root.railActiveStripeColor
                            activeTextColor: root.railTextColor
                            idleTextColor: root.railMutedTextColor
                            onClicked: root.activateNavPage("system")
                        }
                    }
                }

                Rectangle {
                    objectName: "adminSessionCard"
                    Layout.fillWidth: true
                    color: root.railPanelColor
                    radius: 10
                    border.color: root.railBorderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            text: "会话"
                            color: root.railMutedTextColor
                            font.pixelSize: 11
                            font.bold: true
                            leftPadding: 8
                            topPadding: 4
                            bottomPadding: 8
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "退出登录"
                            flat: true
                            leftPadding: 18
                            rightPadding: 12
                            topPadding: 10
                            bottomPadding: 10
                            hoverEnabled: true

                            background: Rectangle {
                                radius: 8
                                color: parent.hovered ? "#f5ebeb" : "transparent"
                            }

                            contentItem: Text {
                                text: parent.text
                                color: root.railLogoutColor
                                font.pixelSize: 14
                                verticalAlignment: Text.AlignVCenter
                            }

                            onClicked: sessionStore.owner.StartLogout()
                        }
                    }
                }
            }
        }

        // Main Content Area
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 84
                color: "white"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Label {
                            text: "管理员工作空间"
                            color: root.headerMutedTextColor
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Label {
                            text: root.currentPageTitle
                            color: root.railTextColor
                            font.pixelSize: 24
                            font.bold: true
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.currentPageSubtitle
                            color: root.headerMutedTextColor
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    OwnerAccountCard {
                        backgroundColor: root.contentBackgroundColor
                        borderColor: root.contentBorderColor
                        primaryTextColor: root.railTextColor
                        secondaryTextColor: root.headerMutedTextColor
                        primaryText: root.accountDisplayName
                        secondaryText: root.accountSecondaryText
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: root.contentBorderColor
                }
            }

            // Content stack
            StackView {
                id: stackView
                objectName: "adminStackView"
                Layout.fillWidth: true
                Layout.fillHeight: true
                initialItem: userTabPage
            }
        }
    }

    Component {
        id: userTabPage
        UserTab {}
    }

    Component {
        id: shareTabPage
        ShareTab {}
    }

    Component {
        id: systemTabPage
        SystemTab {}
    }

    onActiveDestinationChanged: {
        switch (root.activeDestination) {
        case "users":
            stackView.replace(userTabPage)
            break
        case "shares":
            stackView.replace(shareTabPage)
            break
        case "system":
            stackView.replace(systemTabPage)
            break
        }
    }
}
