import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/FormatUtils.js" as FormatUtils

Page {
    id: root

    WorkspaceTheme { id: theme }

    readonly property color pageBackground: theme.panelBackgroundColor
    readonly property color rowAltColor: theme.panelMutedFillColor
    readonly property color successColor: theme.successChipColor
    readonly property color errorColor: theme.errorTextColor
    readonly property color disabledColor: theme.disabledChipColor
    readonly property color warningColor: theme.warningChipColor
    readonly property color secondaryColor: theme.secondaryTextColor
    readonly property color tertiaryColor: theme.tertiaryTextColor

    background: Rectangle { color: root.pageBackground }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.pagePadding
        spacing: 12

        RowLayout {
            Layout.fillWidth: true

            TabBar {
                id: tabBar
                Layout.fillWidth: true

                TabButton {
                    text: "上传 (%1)".arg(uploadList.count)
                    width: implicitWidth
                }
                TabButton {
                    text: "下载 (%1)".arg(downloadList.count)
                    width: implicitWidth
                }
            }

            Button {
                text: "清除已完成"
                onClicked: transferManager.ClearCompletedUploads()
                visible: tabBar.currentIndex === 0
            }
            Button {
                text: "清除已完成"
                onClicked: transferManager.ClearCompletedDownloads()
                visible: tabBar.currentIndex === 1
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            // Uploads tab
            ListView {
                id: uploadList
                model: transferManager.uploadModel
                spacing: 4
                clip: true

                delegate: Rectangle {
                    width: uploadList.width
                    height: 72
                    color: index % 2 === 0 ? root.rowAltColor : root.pageBackground
                    radius: theme.innerPanelRadius

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: theme.panelSpacing
                        anchors.rightMargin: theme.panelSpacing
                        spacing: theme.panelSpacing

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: model.filename
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                function statusText(s) {
                                    var map = {
                                        "queued": "排队中",
                                        "hashing": "正在计算哈希...",
                                        "initializing": "正在初始化...",
                                        "uploading": "正在上传...",
                                        "completing": "正在完成...",
                                        "cancelling": "正在取消...",
                                        "completed": "已完成",
                                        "cancelled": "已取消",
                                        "expired": "已过期",
                                        "failed": "失败",
                                        "retrying": "正在重试..."
                                    }
                                    return map[s] || s
                                }

                                function statusColor(s) {
                                    if (s === "completed") return root.successColor
                                    if (s === "failed" || s === "expired") return root.errorColor
                                    if (s === "cancelled") return root.disabledColor
                                    if (s === "cancelling") return root.warningColor
                                    return root.secondaryColor
                                }

                                text: statusText(model.status) +
                                    (model.error && model.error.message
                                        ? " — " + model.error.message : "")
                                color: statusColor(model.status)
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            ProgressBar {
                                from: 0
                                to: 1
                                value: {
                                    if (model.status === "completed") return 1
                                    if (!model.totalChunks || model.totalChunks === 0) return 0
                                    var uploaded = model.uploadedChunkIndices
                                        ? model.uploadedChunkIndices.length : 0
                                    return uploaded / model.totalChunks
                                }
                                Layout.fillWidth: true
                                visible: model.status === "uploading" ||
                                         model.status === "completing" ||
                                         model.status === "completed"
                            }
                        }

                        Text {
                            text: FormatUtils.formatSize(model.fileSize, 2)
                            font.pixelSize: 12
                            color: root.tertiaryColor
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "uploading" ||
                                     model.status === "initializing" ||
                                     model.status === "hashing"

                            Button {
                                text: "取消"
                                flat: true
                                palette.buttonText: root.errorColor
                                onClicked: transferManager.CancelUpload(model.taskId)
                            }
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "failed" ||
                                     model.status === "expired"

                            Button {
                                text: "重试"
                                flat: true
                                palette.buttonText: "#1976d2"
                                onClicked: transferManager.RetryUpload(model.taskId)
                            }
                            Button {
                                visible: model.status !== "completed"
                                text: "忽略"
                                flat: true
                                onClicked: transferManager.ClearCompletedUploads()
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    text: "暂无上传"
                    color: root.tertiaryColor
                    visible: uploadList.count === 0
                }
            }

            // Downloads tab
            ListView {
                id: downloadList
                model: transferManager.downloadModel
                spacing: 4
                clip: true

                delegate: Rectangle {
                    width: downloadList.width
                    height: 72
                    color: index % 2 === 0 ? root.rowAltColor : root.pageBackground
                    radius: theme.innerPanelRadius

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: theme.panelSpacing
                        anchors.rightMargin: theme.panelSpacing
                        spacing: theme.panelSpacing

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: model.filename
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Text {
                                function statusText(s) {
                                    var map = {
                                        "idle": "空闲",
                                        "fetching_metadata": "正在获取信息...",
                                        "ready": "就绪",
                                        "downloading": "正在下载...",
                                        "paused": "已暂停",
                                        "retry_waiting": "等待重试...",
                                        "completed": "已完成",
                                        "cancelled": "已取消",
                                        "failed": "失败"
                                    }
                                    return map[s] || s
                                }

                                function statusColor(s) {
                                    if (s === "completed") return root.successColor
                                    if (s === "failed") return root.errorColor
                                    if (s === "cancelled") return root.disabledColor
                                    if (s === "paused") return root.warningColor
                                    return root.secondaryColor
                                }

                                text: statusText(model.status) +
                                    (model.error && model.error.message
                                        ? " — " + model.error.message : "")
                                color: statusColor(model.status)
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            ProgressBar {
                                from: 0
                                to: model.fileSize > 0 ? model.fileSize : 1
                                value: model.receivedBytes
                                Layout.fillWidth: true
                                visible: model.status === "downloading" ||
                                         model.status === "paused" ||
                                         model.status === "completed"
                            }
                        }

                        Text {
                            text: FormatUtils.formatSize(model.fileSize, 2)
                            font.pixelSize: 12
                            color: root.tertiaryColor
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "downloading" ||
                                     model.status === "fetching_metadata"

                            Button {
                                text: "暂停"
                                flat: true
                                palette.buttonText: root.warningColor
                                onClicked: transferManager.PauseDownload(model.taskId)
                            }
                            Button {
                                text: "取消"
                                flat: true
                                palette.buttonText: root.errorColor
                                onClicked: transferManager.CancelDownload(model.taskId)
                            }
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "paused"

                            Button {
                                text: "继续"
                                flat: true
                                palette.buttonText: root.successColor
                                onClicked: transferManager.ResumeDownload(model.taskId)
                            }
                            Button {
                                text: "取消"
                                flat: true
                                palette.buttonText: root.errorColor
                                onClicked: transferManager.CancelDownload(model.taskId)
                            }
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "failed"

                            Button {
                                text: "重试"
                                flat: true
                                palette.buttonText: "#1976d2"
                                onClicked: transferManager.RetryDownload(model.taskId)
                            }
                            Button {
                                text: "忽略"
                                flat: true
                                onClicked: transferManager.ClearCompletedDownloads()
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    text: "暂无下载"
                    color: root.tertiaryColor
                    visible: downloadList.count === 0
                }
            }
        }
    }
}
