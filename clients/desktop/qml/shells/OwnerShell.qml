import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/owner"
import "../components/FormatUtils.js" as FormatUtils
import "../pages"

ApplicationWindow {
    id: root
    objectName: "ownerShellWindow"
    visible: true
    width: 1024
    height: 768
    title: "Disk Desktop"

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
    readonly property color storageTrackColor: workspaceTheme.storageTrackColor
    readonly property real storageUsedBytes: Number(profileManager.storageStats.used || 0)
    readonly property real storageTotalBytes: Number((profileManager.storageStats.total || profileManager.storageStats.quota) || 0)
    readonly property real storageUsageRatio: root.storageTotalBytes > 0
                                             ? Math.min(1, root.storageUsedBytes / root.storageTotalBytes)
                                             : 0
    readonly property string currentPageTitle: root.destinationTitle(root.activeDestination, root.activeDriveViewMode)
    readonly property string currentPageSubtitle: root.destinationSubtitle(root.activeDestination, root.activeDriveViewMode)
    readonly property string accountDisplayName: profileManager.userProfile.nickname
                                                 || profileManager.userProfile.username
                                                 || "Owner account"
    readonly property string accountSecondaryText: profileManager.userProfile.username
                                                    ? "@" + profileManager.userProfile.username
                                                    : "Signed in workspace"
readonly property var fileViewNavItems: [
        {
            id: "myfiles",
            label: "My Files",
            objectName: "ownerNavMyFilesButton"
        },
        {
            id: "shared",
            label: "Shares",
            objectName: "ownerNavSharesButton"
        },
        {
            id: "trash",
            label: "Trash",
            objectName: "ownerNavTrashButton"
        }
    ]
    readonly property var independentPageNavItems: [
        {
            id: "transfers",
            label: "Transfers",
            objectName: "ownerNavTransfersButton"
        },
        {
            id: "settings",
            label: "Settings",
            objectName: "ownerNavSettingsButton"
        }
    ]
    property string activeDestination: "drive"
    property string activeDriveViewMode: "myfiles"

    function destinationForPageComponent(pageComponent) {
        if (pageComponent === transferCenterPageComponent)
            return "transfers"
        if (pageComponent === settingsPageComponent)
            return "settings"
        return "drive"
    }

    function showPage(pageComponent) {
        root.activeDestination = root.destinationForPageComponent(pageComponent)
        root.activeDriveViewMode = ""
        stackView.replace(pageComponent)
    }

    function showDriveViewMode(viewMode) {
        root.activeDriveViewMode = viewMode || "myfiles"
        root.activeDestination = "drive"
        // Reuse the mounted drive host when the current page already supports
        // internal view-mode switching (i.e. we are already on PAGE-DRIVE).
        if (stackView.currentItem
            && typeof stackView.currentItem.activateViewMode === "function") {
            stackView.currentItem.activateViewMode(root.activeDriveViewMode)
            return
        }
        // Entering Drive from a non-drive page (Transfers / Settings).
        stackView.replace(driveBrowserPageComponent)
        if (stackView.currentItem && typeof stackView.currentItem.activateViewMode === "function") {
            stackView.currentItem.activateViewMode(root.activeDriveViewMode)
        }
    }

function activateFileView(itemId) {
        switch (itemId) {
        case "shared":
            root.showDriveViewMode("shared")
            return
        case "trash":
            root.showDriveViewMode("trash")
            return
        default:
            root.showDriveViewMode("myfiles")
            return
        }
    }

    function activateIndependentPage(itemId) {
        switch (itemId) {
        case "transfers":
            root.activeDestination = "transfers"
            root.showPage(transferCenterPageComponent)
            return
        case "settings":
            root.activeDestination = "settings"
            root.showPage(settingsPageComponent)
            return
        }
    }

    function destinationTitle(destination, viewMode) {
        if (destination === "drive") {
            switch (viewMode) {
            case "shared": return "Shares"
            case "trash": return "Trash"
            default: return "My Files"
            }
        }
        switch (destination) {
        case "transfers":
            return "Transfers"
        case "settings":
            return "Settings"
        default:
            return "My Files"
        }
    }

    function destinationSubtitle(destination, viewMode) {
        if (destination === "drive") {
            switch (viewMode) {
            case "shared": return "Manage outbound file access"
            case "trash": return "Review recently deleted items"
            default: return "Browse and manage your drive"
            }
        }
        switch (destination) {
        case "transfers":
            return "Track uploads and downloads"
        case "settings":
            return "Profile, password, and storage"
        default:
            return "Browse and manage your drive"
        }
    }

    function formatBytes(bytes) {
        return FormatUtils.formatStorageSize(bytes)
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
                    Layout.minimumHeight: 200
                    objectName: "ownerNavigationPanel"
                    color: root.railPanelColor
                    radius: 10
                    border.color: root.railBorderColor

                    ScrollView {
                        objectName: "ownerNavigationScrollView"
                        anchors.fill: parent
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        ColumnLayout {
                            objectName: "ownerNavigationContent"
                            width: parent.parent.width - 16
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

                            OwnerSidebarSection {
                                Layout.fillWidth: true
                                sectionObjectName: "ownerFileViewGroup"
                                title: "File Views"
                                items: root.fileViewNavItems
                                activeItemId: root.activeDestination === "drive" ? root.activeDriveViewMode : ""
                                titleTextColor: root.railMutedTextColor
                                activeFillColor: root.railActiveColor
                                hoverFillColor: root.railHoverColor
                                activeStripeColor: root.railActiveStripeColor
                                activeTextColor: root.railTextColor
                                idleTextColor: root.railMutedTextColor
                                onItemActivated: function(itemId) {
                                    root.activateFileView(itemId)
                                }
                            }

                            OwnerSidebarSection {
                                Layout.fillWidth: true
                                sectionObjectName: "ownerIndependentPageGroup"
                                title: "Independent Pages"
                                items: root.independentPageNavItems
                                activeItemId: root.activeDestination === "drive" ? "" : root.activeDestination
                                titleTextColor: root.railMutedTextColor
                                activeFillColor: root.railActiveColor
                                hoverFillColor: root.railHoverColor
                                activeStripeColor: root.railActiveStripeColor
                                activeTextColor: root.railTextColor
                                idleTextColor: root.railMutedTextColor
                                onItemActivated: function(itemId) {
                                    root.activateIndependentPage(itemId)
                                }
                            }
                        }
                    }
                }

                OwnerStorageCard {
                    objectName: "ownerStorageCard"
                    usageText: root.formatBytes(root.storageUsedBytes) + " / "
                               + root.formatBytes(root.storageTotalBytes)
                    usageRatio: root.storageUsageRatio
                    totalBytes: root.storageTotalBytes
                    panelColor: root.railPanelColor
                    borderColor: root.railBorderColor
                    titleTextColor: root.railMutedTextColor
                    bodyTextColor: root.railTextColor
                    trackColor: root.storageTrackColor
                    accentColor: root.railActiveStripeColor
                }

                Rectangle {
                    objectName: "ownerSessionCard"
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
            
            // StackView for pages
            StackView {
                id: stackView
                objectName: "ownerStackView"
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
        id: settingsPageComponent
        SettingsPage {}
    }
}
