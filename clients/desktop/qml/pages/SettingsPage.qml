import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/FormatUtils.js" as FormatUtils

Page {
    id: root

    WorkspaceTheme { id: theme }
    
    PageStateView {
        id: stateView
        anchors.fill: parent
        pageState: shellController.pageState
        
        // Content state
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 20
            
            Label {
                text: "Settings & Profile"
                font.pixelSize: 24
                font.bold: true
            }
            
            // Profile Section
            GroupBox {
                title: "Profile"
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    RowLayout {
                        Label { text: "Nickname:" }
                        TextField {
                            id: nicknameField
                            text: profileManager.userProfile.nickname || ""
                            Layout.fillWidth: true
                        }
                    }
                    
                    Button {
                        text: "Update Profile"
                        onClicked: profileManager.updateProfile(nicknameField.text, "")
                    }
                }
            }
            
            // Storage Section
            GroupBox {
                title: "Storage"
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    Label {
                        text: "Used: " + FormatUtils.formatStorageSize(profileManager.storageStats.used || 0) + 
                              " / " + FormatUtils.formatStorageSize(profileManager.storageStats.total || 0)
                    }
                    
                    ProgressBar {
                        Layout.fillWidth: true
                        value: (profileManager.storageStats.used || 0) / Math.max(1, profileManager.storageStats.total || 1)
                    }
                }
            }
            
            // Password Section
            GroupBox {
                title: "Change Password"
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    RowLayout {
                        Label { text: "Old Password:" }
                        TextField {
                            id: oldPasswordField
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                        }
                    }
                    
                    RowLayout {
                        Label { text: "New Password:" }
                        TextField {
                            id: newPasswordField
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                        }
                    }
                    
                    Button {
                        text: "Change Password"
                        onClicked: profileManager.changePassword(oldPasswordField.text, newPasswordField.text)
                    }
                }
            }
            
            Item { Layout.fillHeight: true } // Spacer
        }
    }
    
    Component.onCompleted: {
        profileManager.loadProfile();
        profileManager.loadStorageStats();
    }
}