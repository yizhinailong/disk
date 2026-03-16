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
                font.pixelSize: 22
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            Label {
                text: SessionViewModel.isLoggedIn
                      ? "欢迎回来，" + SessionViewModel.loggedInUserName
                      : "欢迎使用 Disk 云盘"
                font.pixelSize: 14
                color: palette.placeholderText
                Layout.topMargin: 4
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            // ==================== 快捷操作 ====================

            Label {
                text: "快捷操作"
                font.pixelSize: 16
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
            }

            Row {
                Layout.topMargin: 12
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: 12

                // 上传文件
                Button {
                    width: 120
                    height: 80
                    onClicked: root.openUploadDialog()

                    contentItem: ColumnLayout {
                        spacing: 4

                        Label {
                            text: "⬆"
                            font.pixelSize: 24
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.buttonText
                        }

                        Label {
                            text: "上传文件"
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.buttonText
                        }
                    }

                    ToolTip.visible: hovered
                    ToolTip.text: "上传文件"
                }

                // 新建文件夹
                Button {
                    width: 120
                    height: 80
                    onClicked: root.navigateToFiles()

                    contentItem: ColumnLayout {
                        spacing: 4

                        Label {
                            text: "📁"
                            font.pixelSize: 24
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.buttonText
                        }

                        Label {
                            text: "新建文件夹"
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.buttonText
                        }
                    }

                    ToolTip.visible: hovered
                    ToolTip.text: "新建文件夹（进入文件页面）"
                }

                // 上传队列
                Button {
                    width: 120
                    height: 80
                    onClicked: root.navigateToUpload()

                    contentItem: ColumnLayout {
                        spacing: 4

                        Label {
                            text: "⬆"
                            font.pixelSize: 24
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.buttonText
                        }

                        Label {
                            text: "上传队列"
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.buttonText
                        }
                    }

                    ToolTip.visible: hovered
                    ToolTip.text: "查看上传任务"
                }

                // 下载队列
                Button {
                    width: 120
                    height: 80
                    onClicked: root.navigateToDownload()

                    contentItem: ColumnLayout {
                        spacing: 4

                        Label {
                            text: "⬇"
                            font.pixelSize: 24
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.buttonText
                        }

                        Label {
                            text: "下载队列"
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.buttonText
                        }
                    }

                    ToolTip.visible: hovered
                    ToolTip.text: "查看下载任务"
                }
            }

            // ==================== 存储空间 ====================

            Label {
                text: "存储空间"
                font.pixelSize: 16
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 28
                Layout.leftMargin: 24
            }

            Rectangle {
                Layout.topMargin: 12
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.preferredHeight: storageCol.implicitHeight + 24
                color: palette.base
                radius: 8
                border.color: palette.mid
                border.width: 1

                ColumnLayout {
                    id: storageCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: SessionViewModel.isLoggedIn
                                  ? "已使用 " + SessionViewModel.storageUsedFormatted
                                    + " / " + SessionViewModel.storageQuotaFormatted
                                  : "未登录"
                            font.pixelSize: 13
                            color: palette.windowText
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            visible: SessionViewModel.isLoggedIn
                            text: {
                                var pct = SessionViewModel.storagePercentage;
                                return pct.toFixed(1) + "%";
                            }
                            font.pixelSize: 13
                            color: {
                                var pct = SessionViewModel.storagePercentage;
                                if (pct >= 100) return "#D32F2F";
                                if (pct >= 80) return "#F57C00";
                                return palette.placeholderText;
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
                            implicitHeight: 8
                            radius: 4
                            color: palette.midlight
                        }

                        contentItem: Item {
                            implicitWidth: 200
                            implicitHeight: 8

                            Rectangle {
                                width: parent.parent.visualPosition * parent.width
                                height: parent.height
                                radius: 4
                                color: {
                                    var pct = SessionViewModel.storagePercentage;
                                    if (pct >= 100) return "#D32F2F";
                                    if (pct >= 80) return "#F57C00";
                                    return "#1976D2";
                                }
                            }
                        }
                    }

                    Label {
                        visible: SessionViewModel.storagePercentage >= 80
                                 && SessionViewModel.storagePercentage < 100
                        text: "⚠ 存储空间即将用尽，请清理文件"
                        font.pixelSize: 12
                        color: "#F57C00"
                    }

                    Label {
                        visible: SessionViewModel.storagePercentage >= 100
                        text: "❌ 存储空间已满，无法上传新文件"
                        font.pixelSize: 12
                        color: "#D32F2F"
                    }
                }
            }

            // ==================== 最近文件 ====================

            Label {
                text: "最近文件"
                font.pixelSize: 16
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 28
                Layout.leftMargin: 24
            }

            // 最近文件区域
            Item {
                id: recentFilesSection
                Layout.topMargin: 12
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.preferredHeight: {
                    if (FileListViewModel.recentFilesLoading) return 100
                    if (FileListViewModel.recentFilesModel.count === 0) return 160
                    return Math.min(10, FileListViewModel.recentFilesModel.count) * 48 + 44
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
                        spacing: 8

                        Label {
                            text: "最近文件"
                            font.pixelSize: 14
                            font.bold: true
                            color: palette.windowText
                        }

                        Label {
                            text: FileListViewModel.recentFilesModel.count > 0 
                                  ? "(" + FileListViewModel.recentFilesModel.count + ")"
                                  : ""
                            font.pixelSize: 12
                            color: palette.placeholderText
                        }

                        Item { Layout.fillWidth: true }

                        ToolButton {
                            text: "🔄"
                            font.pixelSize: 12
                            implicitWidth: 28
                            implicitHeight: 28
                            ToolTip.visible: hovered
                            ToolTip.text: "刷新"
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
                        spacing: 8
                        visible: !FileListViewModel.recentFilesLoading 
                                 && FileListViewModel.recentFilesError !== ""

                        Label {
                            text: "⚠️ " + FileListViewModel.recentFilesError
                            font.pixelSize: 13
                            color: palette.placeholderText
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Button {
                            text: "重试"
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignHCenter
                            onClicked: FileListViewModel.loadRecentFiles()
                        }
                    }

                    // 空状态
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8
                        visible: !FileListViewModel.recentFilesLoading 
                                 && FileListViewModel.recentFilesError === ""
                                 && FileListViewModel.recentFilesModel.count === 0

                        Label {
                            text: "📭"
                            font.pixelSize: 32
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.placeholderText
                        }

                        Label {
                            text: "暂无最近文件"
                            font.pixelSize: 14
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.placeholderText
                        }

                        Label {
                            text: "上传您的第一个文件吧"
                            font.pixelSize: 12
                            Layout.alignment: Qt.AlignHCenter
                            color: palette.placeholderText
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
                        spacing: 1

                        // 格式化函数现在集中在 FormatUtils 单例中

                        delegate: Rectangle {
                            width: recentListView.width
                            height: 48
                            color: recentItemMa.containsMouse ? palette.midlight : "transparent"

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: palette.mid
                                opacity: 0.3
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 8

                                // 图标
                                Label {
                                    text: FormatUtils.fileIcon(model.fileType, model.fileMimeType)
                                    font.pixelSize: 18
                                    Layout.preferredWidth: 24
                                }

                                // 文件名
                                Label {
                                    text: model.fileName
                                    font.pixelSize: 13
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                    color: palette.windowText
                                }

                                // 大小
                                Label {
                                    text: model.fileIsFolder 
                                          ? (model.fileItemCount + " 项")
                                          : FormatUtils.formatSize(model.fileSize)
                                    font.pixelSize: 11
                                    color: palette.placeholderText
                                    Layout.preferredWidth: 70
                                    horizontalAlignment: Text.AlignRight
                                }

                                // 修改日期
                                Label {
                                    text: FormatUtils.formatDate(model.fileUpdatedAt)
                                    font.pixelSize: 11
                                    color: palette.placeholderText
                                    Layout.preferredWidth: 80
                                    horizontalAlignment: Text.AlignRight
                                }
                            }

                            MouseArea {
                                id: recentItemMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor

                                onClicked: {
                                    if (model.fileIsFolder) {
                                        // 在文件页面导航到文件夹
                                        FileListViewModel.navigateToFolder(model.fileId)
                                        root.navigateToFiles()
                                    } else {
                                        // 触发文件下载
                                        TransfersViewModel.startDownload(model.fileId, SettingsViewModel.downloadDir)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 底部留白
            Item { Layout.preferredHeight: 24 }
        }
    }
}
