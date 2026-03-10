/**
 * @file UploadPage.qml
 * @brief 上传页 — 上传队列、进度、暂停/恢复/取消
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root


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
                    text: "上传"
                    font.pixelSize: 20
                    font.bold: true
                    color: palette.windowText
                }

                Label {
                    text: TransfersViewModel.activeUploadCount > 0
                          ? "(" + TransfersViewModel.activeUploadCount + " 进行中)"
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
                    enabled: TransfersViewModel.activeUploadCount > 0
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
                visible: TransfersViewModel.uploadModel.count === 0

                Label {
                    text: "暂无传输任务"
                    font.pixelSize: 16
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            ListView {
                id: uploadListView
                anchors.fill: parent
                visible: TransfersViewModel.uploadModel.count > 0
                clip: true
                model: TransfersViewModel.uploadModel
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    width: uploadListView.width
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
                                    text: TransferHelpers.statusText(model.status, true)
                                    font.pixelSize: 11
                                    color: TransferHelpers.statusColor(model.status, palette)
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
                                            color: TransferHelpers.statusColor(model.status, palette)
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
                                    text: TransferHelpers.formatSize(model.doneBytes) + " / " + TransferHelpers.formatSize(model.totalBytes)
                                    font.pixelSize: 11
                                    color: palette.placeholderText
                                }

                                Label {
                                    text: model.status === 1 ? TransferHelpers.formatSpeed(model.speed) : ""
                                    font.pixelSize: 11
                                    color: palette.placeholderText
                                    visible: text !== ""
                                }

                                Label {
                                    text: model.status === 1 && model.eta > 0 ? "剩余 " + TransferHelpers.formatEta(model.eta) : ""
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
