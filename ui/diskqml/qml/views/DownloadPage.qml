/**
 * @file DownloadPage.qml
 * @brief 下载页 — 下载队列、进度、暂停/恢复/取消
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0
import "../tokens"
import "../components/primitives"
import "../components"

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: StyleTokens.titleBarHeight
            color: StyleTokens.colorSurface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: StyleTokens.spacingLg
                anchors.rightMargin: StyleTokens.spacingLg
                spacing: StyleTokens.spacingSm

                Label {
                    text: "下载"
                    font.pixelSize: StyleTokens.fontSizeH1
                    font.weight: StyleTokens.fontWeightH1
                    color: StyleTokens.colorTextPrimary
                }

                Label {
                    text: TransfersViewModel.activeDownloadCount > 0
                          ? "(" + TransfersViewModel.activeDownloadCount + " 进行中)"
                          : ""
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorTextTertiary
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "⏸"
                    variant: "icon"
                    ToolTip.visible: hovered
                    ToolTip.text: "全部暂停"
                    enabled: TransfersViewModel.activeDownloadCount > 0
                    onClicked: TransfersViewModel.pauseAll()
                }

                AppButton {
                    text: "▶"
                    variant: "icon"
                    ToolTip.visible: hovered
                    ToolTip.text: "全部恢复"
                    onClicked: TransfersViewModel.resumeAll()
                }

                AppButton {
                    text: "🧹"
                    variant: "icon"
                    ToolTip.visible: hovered
                    ToolTip.text: "清除已完成"
                    onClicked: TransfersViewModel.clearCompleted()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: StyleTokens.colorBorder
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                spacing: StyleTokens.spacingMd
                visible: TransfersViewModel.downloadModel.count === 0

                Label {
                    text: "暂无传输任务"
                    font.pixelSize: StyleTokens.fontSizeH2
                    color: StyleTokens.colorTextTertiary
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
                spacing: StyleTokens.spacingXs
                
                topMargin: StyleTokens.spacingMd
                bottomMargin: StyleTokens.spacingMd
                leftMargin: StyleTokens.spacingMd
                rightMargin: StyleTokens.spacingMd

                delegate: AppCard {
                    width: downloadListView.width - downloadListView.leftMargin - downloadListView.rightMargin
                    implicitHeight: 80
                    hoverEnabled: true

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: StyleTokens.spacingMd
                        spacing: StyleTokens.spacingLg

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: StyleTokens.spacingXs

                            RowLayout {
                                spacing: StyleTokens.spacingSm

                                Label {
                                    text: model.fileName
                                    font.pixelSize: StyleTokens.fontSizeBody
                                    font.weight: StyleTokens.fontWeightBody
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                    color: StyleTokens.colorTextPrimary
                                }

                                AppBadge {
                                    text: TransferHelpers.statusText(model.status, false)
                                    status: {
                                        switch (model.status) {
                                        case 1: return "info"
                                        case 2: return "warning"
                                        case 3: return "success"
                                        case 4: return "error"
                                        default: return "info"
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                spacing: StyleTokens.spacingSm

                                ProgressBar {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 6
                                    from: 0
                                    to: 100
                                    value: model.progress

                                    background: Rectangle {
                                        implicitHeight: 6
                                        radius: 3
                                        color: StyleTokens.colorBorder
                                    }

                                    contentItem: Item {
                                        implicitHeight: 6
                                        Rectangle {
                                            width: parent.width * model.progress / 100
                                            height: parent.height
                                            radius: 3
                                            color: TransferHelpers.statusColor(model.status, null)
                                        }
                                    }
                                }

                                Label {
                                    text: model.progress + "%"
                                    font.pixelSize: StyleTokens.fontSizeSmall
                                    color: StyleTokens.colorTextSecondary
                                    Layout.preferredWidth: 40
                                    horizontalAlignment: Text.AlignRight
                                }
                            }

                            RowLayout {
                                spacing: StyleTokens.spacingMd

                                Label {
                                    text: TransferHelpers.formatSize(model.doneBytes) + " / " + TransferHelpers.formatSize(model.totalBytes)
                                    font.pixelSize: StyleTokens.fontSizeSmall
                                    color: StyleTokens.colorTextTertiary
                                }

                                Label {
                                    text: model.status === 1 ? TransferHelpers.formatSpeed(model.speed) : ""
                                    font.pixelSize: StyleTokens.fontSizeSmall
                                    color: StyleTokens.colorTextTertiary
                                    visible: text !== ""
                                }

                                Label {
                                    text: model.status === 1 && model.eta > 0 ? "剩余 " + TransferHelpers.formatEta(model.eta) : ""
                                    font.pixelSize: StyleTokens.fontSizeSmall
                                    color: StyleTokens.colorTextTertiary
                                    visible: text !== ""
                                }

                                Label {
                                    text: model.error || ""
                                    font.pixelSize: StyleTokens.fontSizeSmall
                                    color: StyleTokens.colorError
                                    visible: model.status === 4 && (model.error || "") !== ""
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Item { Layout.fillWidth: true }
                            }
                        }

                        RowLayout {
                            spacing: StyleTokens.spacingXs

                            AppButton {
                                text: "⏸"
                                variant: "icon"
                                visible: model.status === 1
                                ToolTip.visible: hovered
                                ToolTip.text: "暂停"
                                onClicked: TransfersViewModel.pauseTransfer(model.transferId)
                            }

                            AppButton {
                                text: "▶"
                                variant: "icon"
                                visible: model.status === 2
                                ToolTip.visible: hovered
                                ToolTip.text: "恢复"
                                onClicked: TransfersViewModel.resumeTransfer(model.transferId)
                            }

                            AppButton {
                                text: "🔄"
                                variant: "icon"
                                visible: model.status === 4
                                ToolTip.visible: hovered
                                ToolTip.text: "重试"
                                onClicked: TransfersViewModel.retryTransfer(model.transferId)
                            }

                            AppButton {
                                text: "✕"
                                variant: "icon"
                                visible: model.status !== 3
                                ToolTip.visible: hovered
                                ToolTip.text: "取消"
                                onClicked: TransfersViewModel.cancelTransfer(model.transferId)
                            }
                        }
                    }
                }
            }
        }
    }
}
