/**
 * @file HomePage.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 首页 - 快捷操作、最近文件（占位）、存储空间概览
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0
import "../components/primitives"
import "../tokens"

Item {
    id: root

    // ==================== 导航信号 ====================

    /// 切换到传输模式 - 上传页面
    signal navigateToUpload()
    /// 切换到传输模式 - 下载页面
    signal navigateToDownload()
    /// 切换到文件模式 - 我的文件
    signal navigateToFiles()
    /// 打开上传对话框（选择文件 + 目标文件夹）
    signal openUploadDialog()
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
                text: "首页"
                font.pixelSize: StyleTokens.fontSizeH1
                font.weight: StyleTokens.fontWeightH1
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
            }

            Label {
                text: SessionViewModel.isLoggedIn
                      ? "欢迎回来，" + SessionViewModel.loggedInUserName
                      : "欢迎使用 Disk 云盘"
                font.pixelSize: StyleTokens.fontSizeBody
                color: StyleTokens.colorTextSecondary
                Layout.topMargin: StyleTokens.spacingXs
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
            }

            // ==================== 快捷操作 ====================

            Label {
                text: "快捷操作"
                font.pixelSize: StyleTokens.fontSizeH2
                font.weight: StyleTokens.fontWeightH2
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
            }

            Row {
                Layout.topMargin: StyleTokens.spacingMd
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                spacing: StyleTokens.spacingMd

                // 上传文件
                AppCard {
                    implicitWidth: 120
                    implicitHeight: 80
                    onClicked: root.openUploadDialog()

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "⬆"
                            font.pixelSize: StyleTokens.fontSizeH1
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorPrimary
                        }

                        Label {
                            text: "上传文件"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorTextPrimary
                        }
                    }
                }

                // 新建文件夹
                AppCard {
                    implicitWidth: 120
                    implicitHeight: 80
                    onClicked: root.navigateToFiles()

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "📁"
                            font.pixelSize: StyleTokens.fontSizeH1
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorPrimary
                        }

                        Label {
                            text: "新建文件夹"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorTextPrimary
                        }
                    }
                }

                // 上传队列
                AppCard {
                    implicitWidth: 120
                    implicitHeight: 80
                    onClicked: root.navigateToUpload()

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "⬆"
                            font.pixelSize: StyleTokens.fontSizeH1
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorPrimary
                        }

                        Label {
                            text: "上传队列"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorTextPrimary
                        }
                    }
                }

                // 下载队列
                AppCard {
                    implicitWidth: 120
                    implicitHeight: 80
                    onClicked: root.navigateToDownload()

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "⬇"
                            font.pixelSize: StyleTokens.fontSizeH1
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorPrimary
                        }

                        Label {
                            text: "下载队列"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorTextPrimary
                        }
                    }
                }
            }

            // ==================== 存储空间 ====================

            Label {
                text: "存储空间"
                font.pixelSize: StyleTokens.fontSizeH2
                font.weight: StyleTokens.fontWeightH2
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
            }

            Rectangle {
                Layout.topMargin: StyleTokens.spacingMd
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.fillWidth: true
                Layout.preferredHeight: storageCol.implicitHeight + StyleTokens.spacingLg
                color: StyleTokens.colorSurface
                radius: StyleTokens.radiusMedium
                border.color: StyleTokens.colorBorder
                border.width: 1

                ColumnLayout {
                    id: storageCol
                    anchors.fill: parent
                    anchors.margins: StyleTokens.spacingMd
                    spacing: StyleTokens.spacingSm

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: SessionViewModel.isLoggedIn
                                  ? "已使用 " + SessionViewModel.storageUsedFormatted
                                    + " / " + SessionViewModel.storageQuotaFormatted
                                  : "未登录"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextPrimary
                        }

                        Item { Layout.fillWidth: true }

                        AppBadge {
                            visible: SessionViewModel.isLoggedIn
                            text: {
                                var pct = SessionViewModel.storagePercentage;
                                return pct.toFixed(1) + "%";
                            }
                            status: {
                                var pct = SessionViewModel.storagePercentage;
                                if (pct >= 100) return "error";
                                if (pct >= 80) return "warning";
                                return "success";
                            }
                        }
                    }

                    ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: SessionViewModel.isLoggedIn ? SessionViewModel.storagePercentage : 0

                        background: Rectangle {
                            implicitWidth: 200
                            implicitHeight: 6
                            radius: 3
                            color: StyleTokens.colorBorder
                        }

                        contentItem: Item {
                            implicitWidth: 200
                            implicitHeight: 6

                            Rectangle {
                                width: parent.parent.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                color: {
                                    var pct = SessionViewModel.storagePercentage;
                                    if (pct >= 100) return StyleTokens.colorError;
                                    if (pct >= 80) return StyleTokens.colorWarning;
                                    return StyleTokens.colorPrimary;
                                }
                            }
                        }
                    }
                }
            }

            // ==================== 最近文件 ====================

            Label {
                text: "最近文件"
                font.pixelSize: StyleTokens.fontSizeH2
                font.weight: StyleTokens.fontWeightH2
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
            }

            // 最近文件区域
            Item {
                id: recentFilesSection
                Layout.topMargin: StyleTokens.spacingMd
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.fillWidth: true
                Layout.preferredHeight: {
                    if (FileListViewModel.recentFilesLoading) return 100
                    if (FileListViewModel.recentFilesModel.count === 0) return 160
                    return Math.min(10, FileListViewModel.recentFilesModel.count) * 40 + 44
                }

                // 首次显示时加载最近文件
                Component.onCompleted: {
                    FileListViewModel.loadRecentFiles()
                }

                // 带有刷新按钮的标题栏
                Rectangle {
                    id: recentHeader
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 36
                    color: "transparent"

                    RowLayout {
                        anchors.fill: parent
                        spacing: StyleTokens.spacingSm

                        Label {
                            text: "最近文件"
                            font.pixelSize: StyleTokens.fontSizeBody
                            font.weight: StyleTokens.fontWeightBody
                            color: StyleTokens.colorTextPrimary
                        }

                        Label {
                            text: FileListViewModel.recentFilesModel.count > 0 
                                  ? "(" + FileListViewModel.recentFilesModel.count + ")"
                                  : ""
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                        }

                        Item { Layout.fillWidth: true }

                        AppButton {
                            variant: "icon"
                            text: "🔄"
                            enabled: !FileListViewModel.recentFilesLoading
                            onClicked: FileListViewModel.loadRecentFiles()
                        }
                    }
                }

                // 内容区域
                Item {
                    anchors.top: recentHeader.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom

                    // 加载状态
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: FileListViewModel.recentFilesLoading
                        visible: FileListViewModel.recentFilesLoading
                    }

                    // 错误状态
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: StyleTokens.spacingSm
                        visible: !FileListViewModel.recentFilesLoading 
                                 && FileListViewModel.recentFilesError !== ""

                        Label {
                            text: "⚠️ " + FileListViewModel.recentFilesError
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                            Layout.alignment: Qt.AlignHCenter
                        }

                        AppButton {
                            variant: "secondary"
                            text: "重试"
                            Layout.alignment: Qt.AlignHCenter
                            onClicked: FileListViewModel.loadRecentFiles()
                        }
                    }

                    // 空状态
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: StyleTokens.spacingSm
                        visible: !FileListViewModel.recentFilesLoading 
                                 && FileListViewModel.recentFilesError === ""
                                 && FileListViewModel.recentFilesModel.count === 0

                        Label {
                            text: "📭"
                            font.pixelSize: 32
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorTextSecondary
                        }

                        Label {
                            text: "暂无最近文件"
                            font.pixelSize: StyleTokens.fontSizeBody
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorTextSecondary
                        }

                        Label {
                            text: "上传您的第一个文件吧"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            Layout.alignment: Qt.AlignHCenter
                            color: StyleTokens.colorTextSecondary
                        }
                    }

                    // 最近文件列表
                    ListView {
                        id: recentListView
                        anchors.fill: parent
                        visible: !FileListViewModel.recentFilesLoading 
                                 && FileListViewModel.recentFilesError === ""
                                 && FileListViewModel.recentFilesModel.count > 0
                        clip: true
                        model: FileListViewModel.recentFilesModel
                        ScrollBar.vertical: ScrollBar {}
                        spacing: 0

                        delegate: AppMenuRow {
                            width: recentListView.width
                            iconText: FormatUtils.fileIcon(model.fileType, model.fileMimeType)
                            labelText: model.fileName
                            
                            onClicked: {
                                if (model.fileIsFolder) {
                                    FileListViewModel.navigateToFolder(model.fileId)
                                    root.navigateToFiles()
                                } else {
                                    TransfersViewModel.startDownload(model.fileId, SettingsViewModel.downloadDir)
                                }
                            }
                        }
                    }
                }
            }

            // 底部留白
            Item { Layout.preferredHeight: StyleTokens.spacingLg }
        }
    }
}
