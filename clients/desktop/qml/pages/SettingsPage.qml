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
                text: "设置与资料"
                font.pixelSize: 24
                font.bold: true
            }
            
            // Profile Section
            GroupBox {
                title: "个人资料"
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    RowLayout {
                        Label { text: "昵称：" }
                        TextField {
                            id: nicknameField
                            text: profileManager.userProfile.nickname || ""
                            Layout.fillWidth: true
                        }
                    }
                    
                    Button {
                        text: "更新资料"
                        onClicked: profileManager.updateProfile(nicknameField.text, "")
                    }
                }
            }
            
            // Storage Section
            GroupBox {
                title: "存储空间"
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    Label {
                        text: "已使用：" + FormatUtils.formatStorageSize(profileManager.storageStats.used || 0) + 
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
                title: "修改密码"
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    RowLayout {
                        Label { text: "旧密码：" }
                        TextField {
                            id: oldPasswordField
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                        }
                    }
                    
                    RowLayout {
                        Label { text: "新密码：" }
                        TextField {
                            id: newPasswordField
                            echoMode: TextInput.Password
                            Layout.fillWidth: true
                        }
                    }
                    
                    Button {
                        text: "修改密码"
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