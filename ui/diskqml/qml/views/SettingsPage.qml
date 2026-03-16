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
import "../tokens"
import "../components/primitives"

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
                font.pixelSize: StyleTokens.fontSizeH1
                font.weight: StyleTokens.fontWeightH1
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
            }

            // ==================== 保存成功横幅 ====================

            Rectangle {
                id: savedBanner
                visible: false
                Layout.fillWidth: true
                Layout.topMargin: StyleTokens.spacingMd
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.preferredHeight: 36
                radius: StyleTokens.radiusMedium
                color: "#E8F5E9"
                border.color: StyleTokens.colorSuccess
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: "✓ 设置已保存"
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: "#2E7D32"
                }
            }

            // ==================== 错误提示 ====================

            Rectangle {
                visible: SettingsViewModel.errorMessage !== ""
                Layout.fillWidth: true
                Layout.topMargin: StyleTokens.spacingMd
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.preferredHeight: 36
                radius: StyleTokens.radiusMedium
                color: "#FFEBEE"
                border.color: StyleTokens.colorError
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: SettingsViewModel.errorMessage
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: "#C62828"
                }
            }

            // ==================== 服务器设置 ====================

            Label {
                text: "服务器设置"
                font.pixelSize: StyleTokens.fontSizeH2
                font.weight: StyleTokens.fontWeightH2
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
            }

            Rectangle {
                Layout.topMargin: StyleTokens.spacingSm
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.fillWidth: true
                Layout.preferredHeight: serverCol.implicitHeight + StyleTokens.spacingLg
                color: StyleTokens.colorSurface
                radius: StyleTokens.radiusLarge
                border.color: StyleTokens.colorBorder
                border.width: 1

                ColumnLayout {
                    id: serverCol
                    anchors.fill: parent
                    anchors.margins: StyleTokens.spacingMd
                    spacing: StyleTokens.spacingSm

                    Label {
                        text: "服务器地址"
                        font.pixelSize: StyleTokens.fontSizeBody
                        color: StyleTokens.colorTextSecondary
                    }

                    AppTextInput {
                        id: serverUrlField
                        Layout.fillWidth: true
                        text: SettingsViewModel.serverUrl
                        placeholderText: "http://127.0.0.1:8080"
                        onTextEdited: SettingsViewModel.serverUrl = text
                    }
                }
            }

            // ==================== 传输设置 ====================

            Label {
                text: "传输设置"
                font.pixelSize: StyleTokens.fontSizeH2
                font.weight: StyleTokens.fontWeightH2
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
            }

            Rectangle {
                Layout.topMargin: StyleTokens.spacingSm
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.fillWidth: true
                Layout.preferredHeight: transferCol.implicitHeight + StyleTokens.spacingLg
                color: StyleTokens.colorSurface
                radius: StyleTokens.radiusLarge
                border.color: StyleTokens.colorBorder
                border.width: 1

                ColumnLayout {
                    id: transferCol
                    anchors.fill: parent
                    anchors.margins: StyleTokens.spacingMd
                    spacing: StyleTokens.spacingMd

                    // 下载目录
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "下载目录"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                        }

                        AppTextInput {
                            id: downloadDirField
                            Layout.fillWidth: true
                            text: SettingsViewModel.downloadDir
                            placeholderText: "~/Downloads"
                            onTextEdited: SettingsViewModel.downloadDir = text
                        }
                    }

                    // 并发上传数
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingMd

                        Label {
                            text: "并发上传数"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
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
                        spacing: StyleTokens.spacingMd

                        Label {
                            text: "并发下载数"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
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
                font.pixelSize: StyleTokens.fontSizeH2
                font.weight: StyleTokens.fontWeightH2
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
            }

            Rectangle {
                Layout.topMargin: StyleTokens.spacingSm
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.fillWidth: true
                Layout.preferredHeight: uiCol.implicitHeight + StyleTokens.spacingLg
                color: StyleTokens.colorSurface
                radius: StyleTokens.radiusLarge
                border.color: StyleTokens.colorBorder
                border.width: 1

                ColumnLayout {
                    id: uiCol
                    anchors.fill: parent
                    anchors.margins: StyleTokens.spacingMd
                    spacing: StyleTokens.spacingSm

                    CheckBox {
                        id: autoStartCheck
                        text: "开机自启动"
                        font.pixelSize: StyleTokens.fontSizeBody
                        checked: SettingsViewModel.autoStart
                        onToggled: SettingsViewModel.autoStart = checked
                    }

                    CheckBox {
                        id: minimizeToTrayCheck
                        text: "最小化到系统托盘"
                        font.pixelSize: StyleTokens.fontSizeBody
                        checked: SettingsViewModel.minimizeToTray
                        onToggled: SettingsViewModel.minimizeToTray = checked
                    }

                    CheckBox {
                        id: showNotificationsCheck
                        text: "显示系统通知"
                        font.pixelSize: StyleTokens.fontSizeBody
                        checked: SettingsViewModel.showNotifications
                        onToggled: SettingsViewModel.showNotifications = checked
                    }

                    CheckBox {
                        id: confirmDeleteCheck
                        text: "删除前确认"
                        font.pixelSize: StyleTokens.fontSizeBody
                        checked: SettingsViewModel.confirmDelete
                        onToggled: SettingsViewModel.confirmDelete = checked
                    }
                }
            }

            // ==================== 操作按钮 ====================

            RowLayout {
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.fillWidth: true
                spacing: StyleTokens.spacingMd

                AppButton {
                    text: "恢复默认"
                    variant: "secondary"
                    onClicked: SettingsViewModel.resetDefaults()
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "取消"
                    variant: "secondary"
                    enabled: SettingsViewModel.hasUnsavedChanges
                    onClicked: SettingsViewModel.revert()
                }

                AppButton {
                    text: "保存"
                    variant: "primary"
                    enabled: SettingsViewModel.hasUnsavedChanges
                    onClicked: SettingsViewModel.save()
                }
            }

            // 底部留白
            Item { Layout.preferredHeight: StyleTokens.spacingLg }
        }
    }
}
