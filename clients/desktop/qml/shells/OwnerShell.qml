import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../pages"

ApplicationWindow {
    id: root
    visible: true
    width: 1024
    height: 768
    title: "Disk Desktop"

    readonly property int railWidth: 220
    readonly property int railSectionSpacing: 12
    readonly property int railOuterPadding: 16
    readonly property color railBackgroundColor: "#f3f5f7"
    readonly property color railBorderColor: "#d6dde5"
    readonly property color railPanelColor: "#ffffff"
    readonly property color railTextColor: "#1f2933"
    readonly property color railMutedTextColor: "#6b7785"
    readonly property color railHoverColor: "#eef2f6"
    readonly property color railActiveColor: "#dce8f5"
    readonly property color railActiveStripeColor: "#4f6b8a"
    readonly property color railLogoutColor: "#8a4f4f"
    readonly property color headerMutedTextColor: "#5f6b76"
    readonly property color contentBackgroundColor: "#f8fafb"
    readonly property color contentBorderColor: "#dfe5eb"
    readonly property color storageTrackColor: "#e5ebf1"
    readonly property real storageUsedBytes: Number(profileManager.storageStats.used || 0)
    readonly property real storageTotalBytes: Number((profileManager.storageStats.total || profileManager.storageStats.quota) || 0)
    readonly property real storageUsageRatio: root.storageTotalBytes > 0
                                             ? Math.min(1, root.storageUsedBytes / root.storageTotalBytes)
                                             : 0
    readonly property string currentPageTitle: root.destinationTitle(root.activeDestination)
    readonly property string currentPageSubtitle: root.destinationSubtitle(root.activeDestination)
    readonly property string accountDisplayName: profileManager.userProfile.nickname
                                                 || profileManager.userProfile.username
                                                 || "Owner account"
    readonly property string accountSecondaryText: profileManager.userProfile.username
                                                   ? "@" + profileManager.userProfile.username
                                                   : "Signed in workspace"
    property string activeDestination: "files"

    function destinationForPageComponent(pageComponent) {
        if (pageComponent === transferCenterPageComponent)
            return "transfers"
        if (pageComponent === shareManagementPageComponent)
            return "shares"
        if (pageComponent === trashPageComponent)
            return "trash"
        if (pageComponent === settingsPageComponent)
            return "settings"
        return "files"
    }

    function showPage(pageComponent) {
        root.activeDestination = root.destinationForPageComponent(pageComponent)
        stackView.replace(pageComponent)
    }

    function destinationTitle(destination) {
        switch (destination) {
        case "transfers":
            return "Transfers"
        case "shares":
            return "Shares"
        case "trash":
            return "Trash"
        case "settings":
            return "Settings"
        default:
            return "Files"
        }
    }

    function destinationSubtitle(destination) {
        switch (destination) {
        case "transfers":
            return "Track uploads and downloads"
        case "shares":
            return "Manage outbound file access"
        case "trash":
            return "Review recently deleted items"
        case "settings":
            return "Profile, password, and storage"
        default:
            return "Browse and manage your drive"
        }
    }

    function formatBytes(bytes) {
        if (bytes === 0)
            return "0 Bytes"
        const k = 1024
        const sizes = ["Bytes", "KB", "MB", "GB", "TB"]
        const i = Math.floor(Math.log(bytes) / Math.log(k))
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i]
    }

    Component.onCompleted: {
        profileManager.loadStorageStats()
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
                            text: "Workspace"
                            color: root.railMutedTextColor
                            font.pixelSize: 12
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.railPanelColor
                    radius: 10
                    border.color: root.railBorderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            text: "Navigation"
                            color: root.railMutedTextColor
                            font.pixelSize: 11
                            font.bold: true
                            leftPadding: 8
                            topPadding: 4
                            bottomPadding: 8
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "Files"
                            flat: true
                            leftPadding: 18
                            rightPadding: 12
                            topPadding: 10
                            bottomPadding: 10
                            hoverEnabled: true

                            background: Rectangle {
                                radius: 8
                                color: root.activeDestination === "files"
                                       ? root.railActiveColor
                                       : (parent.hovered ? root.railHoverColor : "transparent")

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 4
                                    radius: 2
                                    color: root.railActiveStripeColor
                                    visible: root.activeDestination === "files"
                                }
                            }

                            contentItem: Text {
                                text: parent.text
                                color: root.activeDestination === "files"
                                       ? root.railTextColor
                                       : root.railMutedTextColor
                                font.pixelSize: 14
                                font.bold: root.activeDestination === "files"
                                verticalAlignment: Text.AlignVCenter
                            }

                            onClicked: {
                                root.activeDestination = "files"
                                root.showPage(driveBrowserPageComponent)
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "Transfers"
                            flat: true
                            leftPadding: 18
                            rightPadding: 12
                            topPadding: 10
                            bottomPadding: 10
                            hoverEnabled: true

                            background: Rectangle {
                                radius: 8
                                color: root.activeDestination === "transfers"
                                       ? root.railActiveColor
                                       : (parent.hovered ? root.railHoverColor : "transparent")

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 4
                                    radius: 2
                                    color: root.railActiveStripeColor
                                    visible: root.activeDestination === "transfers"
                                }
                            }

                            contentItem: Text {
                                text: parent.text
                                color: root.activeDestination === "transfers"
                                       ? root.railTextColor
                                       : root.railMutedTextColor
                                font.pixelSize: 14
                                font.bold: root.activeDestination === "transfers"
                                verticalAlignment: Text.AlignVCenter
                            }

                            onClicked: {
                                root.activeDestination = "transfers"
                                root.showPage(transferCenterPageComponent)
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "Shares"
                            flat: true
                            leftPadding: 18
                            rightPadding: 12
                            topPadding: 10
                            bottomPadding: 10
                            hoverEnabled: true

                            background: Rectangle {
                                radius: 8
                                color: root.activeDestination === "shares"
                                       ? root.railActiveColor
                                       : (parent.hovered ? root.railHoverColor : "transparent")

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 4
                                    radius: 2
                                    color: root.railActiveStripeColor
                                    visible: root.activeDestination === "shares"
                                }
                            }

                            contentItem: Text {
                                text: parent.text
                                color: root.activeDestination === "shares"
                                       ? root.railTextColor
                                       : root.railMutedTextColor
                                font.pixelSize: 14
                                font.bold: root.activeDestination === "shares"
                                verticalAlignment: Text.AlignVCenter
                            }

                            onClicked: {
                                root.activeDestination = "shares"
                                root.showPage(shareManagementPageComponent)
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "Trash"
                            flat: true
                            leftPadding: 18
                            rightPadding: 12
                            topPadding: 10
                            bottomPadding: 10
                            hoverEnabled: true

                            background: Rectangle {
                                radius: 8
                                color: root.activeDestination === "trash"
                                       ? root.railActiveColor
                                       : (parent.hovered ? root.railHoverColor : "transparent")

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 4
                                    radius: 2
                                    color: root.railActiveStripeColor
                                    visible: root.activeDestination === "trash"
                                }
                            }

                            contentItem: Text {
                                text: parent.text
                                color: root.activeDestination === "trash"
                                       ? root.railTextColor
                                       : root.railMutedTextColor
                                font.pixelSize: 14
                                font.bold: root.activeDestination === "trash"
                                verticalAlignment: Text.AlignVCenter
                            }

                            onClicked: {
                                root.activeDestination = "trash"
                                root.showPage(trashPageComponent)
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "Settings"
                            flat: true
                            leftPadding: 18
                            rightPadding: 12
                            topPadding: 10
                            bottomPadding: 10
                            hoverEnabled: true

                            background: Rectangle {
                                radius: 8
                                color: root.activeDestination === "settings"
                                       ? root.railActiveColor
                                       : (parent.hovered ? root.railHoverColor : "transparent")

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 4
                                    radius: 2
                                    color: root.railActiveStripeColor
                                    visible: root.activeDestination === "settings"
                                }
                            }

                            contentItem: Text {
                                text: parent.text
                                color: root.activeDestination === "settings"
                                       ? root.railTextColor
                                       : root.railMutedTextColor
                                font.pixelSize: 14
                                font.bold: root.activeDestination === "settings"
                                verticalAlignment: Text.AlignVCenter
                            }

                            onClicked: {
                                root.activeDestination = "settings"
                                root.showPage(settingsPageComponent)
                            }
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104
                    color: root.railPanelColor
                    radius: 10
                    border.color: root.railBorderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: "Storage"
                            color: root.railMutedTextColor
                            font.pixelSize: 11
                            font.bold: true
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.formatBytes(root.storageUsedBytes) + " / "
                                  + root.formatBytes(root.storageTotalBytes)
                            color: root.railTextColor
                            font.pixelSize: 14
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            value: root.storageUsageRatio

                            background: Rectangle {
                                implicitHeight: 8
                                radius: 4
                                color: root.storageTrackColor
                            }

                            contentItem: Item {
                                implicitHeight: 8

                                Rectangle {
                                    width: parent.width * root.storageUsageRatio
                                    height: parent.height
                                    radius: 4
                                    color: root.railActiveStripeColor
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.storageTotalBytes > 0
                                  ? Math.round(root.storageUsageRatio * 100) + "% in use"
                                  : "Loading account storage"
                            color: root.railMutedTextColor
                            font.pixelSize: 11
                        }
                    }
                }

                Rectangle {
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
                            text: "Session"
                            color: root.railMutedTextColor
                            font.pixelSize: 11
                            font.bold: true
                            leftPadding: 8
                            topPadding: 4
                            bottomPadding: 8
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "Logout"
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
                            text: "Owner workspace"
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

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 188
                        Layout.preferredHeight: 56
                        color: root.contentBackgroundColor
                        radius: 10
                        border.color: root.contentBorderColor

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 2

                            Label {
                                text: root.accountDisplayName
                                color: root.railTextColor
                                font.pixelSize: 14
                                font.bold: true
                            }

                            Label {
                                text: root.accountSecondaryText
                                color: root.headerMutedTextColor
                                font.pixelSize: 11
                            }
                        }
                    }
                }
                 
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: root.contentBorderColor
                }
            }
            
            // StackView for pages
            StackView {
                id: stackView
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                initialItem: DriveBrowserPage {}
            }
        }
    }

    Component {
        id: driveBrowserPageComponent
        DriveBrowserPage {}
    }

    Component {
        id: transferCenterPageComponent
        TransferCenterPage {}
    }

    Component {
        id: shareManagementPageComponent
        ShareManagementPage {}
    }

    Component {
        id: trashPageComponent
        TrashPage {}
    }

    Component {
        id: settingsPageComponent
        SettingsPage {}
    }
}
