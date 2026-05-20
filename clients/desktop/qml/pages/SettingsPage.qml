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
        pageState: "content"

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

            GroupBox {
                title: "服务器设置"
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Label { text: "服务器地址：" }
                        TextField {
                            id: serverUrlField
                            text: networkSettingsManager.serverUrl
                            placeholderText: "http://127.0.0.1:8080/"
                            Layout.fillWidth: true
                            selectByMouse: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Button {
                            text: "保存服务器地址"
                            onClicked: networkSettingsManager.saveServerUrl(serverUrlField.text)
                        }

                        Button {
                            text: "恢复默认"
                            onClicked: {
                                networkSettingsManager.resetServerUrl()
                                serverUrlField.text = networkSettingsManager.serverUrl
                            }
                        }

                        Button {
                            text: healthManager.checking ? "检测中..." : "测试连接"
                            enabled: !healthManager.checking
                            onClicked: healthManager.checkHealth()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: networkSettingsManager.errorMessage
                        color: theme.errorTextColor
                        visible: text !== ""
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        Layout.fillWidth: true
                        text: {
                            var status = healthManager.health.overallStatus || ""
                            if (status === "") {
                                return "健康状态：未检测"
                            }
                            return "健康状态：" + status + "，耗时 " + (healthManager.health.totalCheckMs || 0) + " ms"
                        }
                        color: healthManager.health.overallStatus === "healthy" ? theme.successTextColor : theme.secondaryTextColor
                        wrapMode: Text.WordWrap
                    }
                }
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
                              " / " + FormatUtils.formatStorageSize((profileManager.storageStats.total || profileManager.storageStats.quota) || 0)
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        value: (profileManager.storageStats.used || 0) / Math.max(1, (profileManager.storageStats.total || profileManager.storageStats.quota) || 1)
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
