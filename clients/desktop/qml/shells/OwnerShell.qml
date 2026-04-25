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

    function showPage(pageComponent) {
        stackView.replace(pageComponent)
    }
    
    RowLayout {
        anchors.fill: parent
        spacing: 0
        
        // Sidebar
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: "#2c3e50"
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                
                Text {
                    text: "Disk"
                    color: "white"
                    font.pixelSize: 24
                    font.bold: true
                    Layout.bottomMargin: 24
                }
                
                Button {
                    Layout.fillWidth: true
                    text: "Files"
                    flat: true
                    onClicked: root.showPage(driveBrowserPageComponent)
                }
                
                Button {
                    Layout.fillWidth: true
                    text: "Transfers"
                    flat: true
                    onClicked: root.showPage(transferCenterPageComponent)
                }
                
                Button {
                    Layout.fillWidth: true
                    text: "Shares"
                    flat: true
                    onClicked: root.showPage(shareManagementPageComponent)
                }
                
                Button {
                    Layout.fillWidth: true
                    text: "Trash"
                    flat: true
                    onClicked: root.showPage(trashPageComponent)
                }
                
                Item {
                    Layout.fillHeight: true
                }
                
                Button {
                    Layout.fillWidth: true
                    text: "Settings"
                    flat: true
                    onClicked: root.showPage(settingsPageComponent)
                }
                
                Button {
                    Layout.fillWidth: true
                    text: "Logout"
                    flat: true
                    onClicked: {
                        // In a real app, this would call authService.Logout
                        shellController.navigateToLogin()
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
                Layout.preferredHeight: 64
                color: "white"
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    
                    TextField {
                        Layout.preferredWidth: 300
                        placeholderText: "Search..."
                    }
                    
                    Item {
                        Layout.fillWidth: true
                    }
                    
                    Text {
                        text: "User"
                    }
                }
                
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: "#e0e0e0"
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
