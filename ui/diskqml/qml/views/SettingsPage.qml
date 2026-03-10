/**
 * @file SettingsPage.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 设置页 - 服务器地址、下载目录、并发数、UI偏好
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    // ==================== 保存成功提示 ====================

    Connections {
        target: SettingsViewModel
        function onSettingsSaved() {
            savedBanner.visible = true
            savedBannerTimer.restart()
        }
    }

    Timer {
        id: savedBannerTimer
        interval: 3000
        onTriggered: savedBanner.visible = false
    }

    // ==================== 内容 ====================

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            // ==================== 页面标题 ====================

            Label {
                text: "⚙ 设置"
                font.pixelSize: 22
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            // ==================== 保存成功横幅 ====================

            Rectangle {
                id: savedBanner
                visible: false
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 36
                radius: 6
                color: "#E8F5E9"
                border.color: "#4CAF50"
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: "✓ 设置已保存"
                    font.pixelSize: 13
                    color: "#2E7D32"
                }
            }

            // ==================== 错误提示 ====================

            Rectangle {
                visible: SettingsViewModel.errorMessage !== ""
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 36
                radius: 6
                color: "#FFEBEE"
                border.color: "#EF5350"
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: SettingsViewModel.errorMessage
                    font.pixelSize: 13
                    color: "#C62828"
                }
            }

            // ==================== 服务器设置 ====================

            Label {
                text: "服务器设置"
                font.pixelSize: 16
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
            }

            Rectangle {
                Layout.topMargin: 8
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.preferredHeight: serverCol.implicitHeight + 24
                color: palette.base
                radius: 8
                border.color: palette.mid
                border.width: 1

                ColumnLayout {
                    id: serverCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Label {
                        text: "服务器地址"
                        font.pixelSize: 13
                        color: palette.windowText
                    }

                    TextField {
                        id: serverUrlField
                        Layout.fillWidth: true
                        text: SettingsViewModel.serverUrl
                        placeholderText: "http://127.0.0.1:8080"
                        font.pixelSize: 13
                        onTextEdited: SettingsViewModel.serverUrl = text
                    }
                }
            }

            // ==================== 传输设置 ====================

            Label {
                text: "传输设置"
                font.pixelSize: 16
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
            }

            Rectangle {
                Layout.topMargin: 8
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.preferredHeight: transferCol.implicitHeight + 24
                color: palette.base
                radius: 8
                border.color: palette.mid
                border.width: 1

                ColumnLayout {
                    id: transferCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    // 下载目录
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "下载目录"
                            font.pixelSize: 13
                            color: palette.windowText
                        }

                        TextField {
                            id: downloadDirField
                            Layout.fillWidth: true
                            text: SettingsViewModel.downloadDir
                            placeholderText: "~/Downloads"
                            font.pixelSize: 13
                            onTextEdited: SettingsViewModel.downloadDir = text
                        }
                    }

                    // 并发上传数
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Label {
                            text: "并发上传数"
                            font.pixelSize: 13
                            color: palette.windowText
                            Layout.preferredWidth: 80
                        }

                        SpinBox {
                            id: concurrentUploadsSpinBox
                            from: 1
                            to: 10
                            value: SettingsViewModel.concurrentUploads
                            onValueModified: SettingsViewModel.concurrentUploads = value
                            Layout.preferredWidth: 120
                        }
                    }

                    // 并发下载数
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Label {
                            text: "并发下载数"
                            font.pixelSize: 13
                            color: palette.windowText
                            Layout.preferredWidth: 80
                        }

                        SpinBox {
                            id: concurrentDownloadsSpinBox
                            from: 1
                            to: 10
                            value: SettingsViewModel.concurrentDownloads
                            onValueModified: SettingsViewModel.concurrentDownloads = value
                            Layout.preferredWidth: 120
                        }
                    }
                }
            }

            // ==================== 外观设置 ====================

            Label {
                text: "外观设置"
                font.pixelSize: 16
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
            }

            Rectangle {
                Layout.topMargin: 8
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.preferredHeight: uiCol.implicitHeight + 24
                color: palette.base
                radius: 8
                border.color: palette.mid
                border.width: 1

                ColumnLayout {
                    id: uiCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    CheckBox {
                        id: autoStartCheck
                        text: "开机自启动"
                        font.pixelSize: 13
                        checked: SettingsViewModel.autoStart
                        onToggled: SettingsViewModel.autoStart = checked
                    }

                    CheckBox {
                        id: minimizeToTrayCheck
                        text: "最小化到系统托盘"
                        font.pixelSize: 13
                        checked: SettingsViewModel.minimizeToTray
                        onToggled: SettingsViewModel.minimizeToTray = checked
                    }

                    CheckBox {
                        id: showNotificationsCheck
                        text: "显示系统通知"
                        font.pixelSize: 13
                        checked: SettingsViewModel.showNotifications
                        onToggled: SettingsViewModel.showNotifications = checked
                    }

                    CheckBox {
                        id: confirmDeleteCheck
                        text: "删除前确认"
                        font.pixelSize: 13
                        checked: SettingsViewModel.confirmDelete
                        onToggled: SettingsViewModel.confirmDelete = checked
                    }
                }
            }

            // ==================== 操作按钮 ====================

            RowLayout {
                Layout.topMargin: 24
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                spacing: 12

                Button {
                    text: "恢复默认"
                    font.pixelSize: 13
                    onClicked: SettingsViewModel.resetDefaults()
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    font.pixelSize: 13
                    enabled: SettingsViewModel.hasUnsavedChanges
                    onClicked: SettingsViewModel.revert()
                }

                Button {
                    text: "保存"
                    font.pixelSize: 13
                    highlighted: true
                    enabled: SettingsViewModel.hasUnsavedChanges
                    onClicked: SettingsViewModel.save()
                }
            }

            // 底部留白
            Item { Layout.preferredHeight: 24 }
        }
    }
}
