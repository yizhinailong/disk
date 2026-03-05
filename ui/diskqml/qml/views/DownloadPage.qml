/**
 * @file DownloadPage.qml
 * @brief 下载页 — 下载队列、进度、暂停/恢复/取消
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    function formatSpeed(bytesPerSec) {
        if (bytesPerSec <= 0) return ""
        if (bytesPerSec < 1024) return bytesPerSec + " B/s"
        if (bytesPerSec < 1048576) return (bytesPerSec / 1024).toFixed(1) + " KB/s"
        if (bytesPerSec < 1073741824) return (bytesPerSec / 1048576).toFixed(1) + " MB/s"
        return (bytesPerSec / 1073741824).toFixed(1) + " GB/s"
    }

    function formatEta(seconds) {
        if (seconds <= 0) return ""
        if (seconds < 60) return seconds + "秒"
        if (seconds < 3600) return Math.floor(seconds / 60) + "分" + (seconds % 60) + "秒"
        var h = Math.floor(seconds / 3600)
        var m = Math.floor((seconds % 3600) / 60)
        return h + "时" + m + "分"
    }

    function formatSize(bytes) {
        if (bytes <= 0) return "0 B"
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + " MB"
        return (bytes / 1073741824).toFixed(1) + " GB"
    }

    function statusColor(status) {
        switch (status) {
        case 1: return palette.highlight       // Running
        case 2: return palette.mid             // Paused
        case 3: return "#4CAF50"               // Completed (green)
        case 4: return "#F44336"               // Failed (red)
        default: return palette.mid            // Queued
        }
    }

    function statusText(status) {
        switch (status) {
        case 0: return "等待中"
        case 1: return "下载中"
        case 2: return "已暂停"
        case 3: return "已完成"
        case 4: return "失败"
        default: return ""
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Label {
                    text: "下载"
                    font.pixelSize: 20
                    font.bold: true
                    color: palette.windowText
                }

                Label {
                    text: TransfersViewModel.activeDownloadCount > 0
                          ? "(" + TransfersViewModel.activeDownloadCount + " 进行中)"
                          : ""
                    font.pixelSize: 13
                    color: palette.placeholderText
                }

                Item { Layout.fillWidth: true }

                ToolButton {
                    text: "⏸"
                    font.pixelSize: 14
                    ToolTip.visible: hovered
                    ToolTip.text: "全部暂停"
                    enabled: TransfersViewModel.activeDownloadCount > 0
                    onClicked: TransfersViewModel.pauseAll()
                }

                ToolButton {
                    text: "▶"
                    font.pixelSize: 14
                    ToolTip.visible: hovered
                    ToolTip.text: "全部恢复"
                    onClicked: TransfersViewModel.resumeAll()
                }

                ToolButton {
                    text: "🧹"
                    font.pixelSize: 14
                    ToolTip.visible: hovered
                    ToolTip.text: "清除已完成"
                    onClicked: TransfersViewModel.clearCompleted()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: palette.mid
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                visible: TransfersViewModel.downloadModel.count === 0

                Label {
                    text: "暂无传输任务"
                    font.pixelSize: 16
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            ListView {
                id: downloadListView
                anchors.fill: parent
                visible: TransfersViewModel.downloadModel.count > 0
                clip: true
                model: TransfersViewModel.downloadModel
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    width: downloadListView.width
                    height: 64
                    color: delegateMa.containsMouse ? palette.midlight : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            RowLayout {
                                spacing: 8

                                Label {
                                    text: model.fileName
                                    font.pixelSize: 13
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                    color: palette.windowText
                                }

                                Label {
                                    text: statusText(model.status)
                                    font.pixelSize: 11
                                    color: statusColor(model.status)
                                }
                            }

                            RowLayout {
                                spacing: 8

                                ProgressBar {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 6
                                    from: 0
                                    to: 100
                                    value: model.progress

                                    background: Rectangle {
                                        implicitHeight: 6
                                        radius: 3
                                        color: palette.mid
                                        opacity: 0.3
                                    }

                                    contentItem: Item {
                                        implicitHeight: 6
                                        Rectangle {
                                            width: parent.width * model.progress / 100
                                            height: parent.height
                                            radius: 3
                                            color: statusColor(model.status)
                                        }
                                    }
                                }

                                Label {
                                    text: model.progress + "%"
                                    font.pixelSize: 11
                                    color: palette.placeholderText
                                    Layout.preferredWidth: 36
                                    horizontalAlignment: Text.AlignRight
                                }
                            }

                            RowLayout {
                                spacing: 8

                                Label {
                                    text: formatSize(model.doneBytes) + " / " + formatSize(model.totalBytes)
                                    font.pixelSize: 11
                                    color: palette.placeholderText
                                }

                                Label {
                                    text: model.status === 1 ? formatSpeed(model.speed) : ""
                                    font.pixelSize: 11
                                    color: palette.placeholderText
                                    visible: text !== ""
                                }

                                Label {
                                    text: model.status === 1 && model.eta > 0 ? "剩余 " + formatEta(model.eta) : ""
                                    font.pixelSize: 11
                                    color: palette.placeholderText
                                    visible: text !== ""
                                }

                                Label {
                                    text: model.error || ""
                                    font.pixelSize: 11
                                    color: "#F44336"
                                    visible: model.status === 4 && (model.error || "") !== ""
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        RowLayout {
                            spacing: 4

                            ToolButton {
                                text: "⏸"
                                font.pixelSize: 12
                                visible: model.status === 1
                                ToolTip.visible: hovered
                                ToolTip.text: "暂停"
                                onClicked: TransfersViewModel.pauseTransfer(model.transferId)
                            }

                            ToolButton {
                                text: "▶"
                                font.pixelSize: 12
                                visible: model.status === 2
                                ToolTip.visible: hovered
                                ToolTip.text: "恢复"
                                onClicked: TransfersViewModel.resumeTransfer(model.transferId)
                            }

                            ToolButton {
                                text: "🔄"
                                font.pixelSize: 12
                                visible: model.status === 4
                                ToolTip.visible: hovered
                                ToolTip.text: "重试"
                                onClicked: TransfersViewModel.retryTransfer(model.transferId)
                            }

                            ToolButton {
                                text: "✕"
                                font.pixelSize: 12
                                visible: model.status !== 3
                                ToolTip.visible: hovered
                                ToolTip.text: "取消"
                                onClicked: TransfersViewModel.cancelTransfer(model.transferId)
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: palette.mid
                        opacity: 0.3
                    }

                    MouseArea {
                        id: delegateMa
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
            }
        }
    }
}
