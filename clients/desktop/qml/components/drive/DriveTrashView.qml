import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

PageStateView {
    id: root

    required property var page

    objectName: "trashStateView"
    visible: page.isTrashMode
    pageState: shellController.pageState

    emptyText: "回收站为空"
    errorText: "加载回收站项目失败"

    onRetryClicked: page.refreshTrashList()

    batchResultComponent: Component {
        Rectangle {
            objectName: "trashBatchResultView"
            color: root.page.panelBackgroundColor
            radius: root.page.panelRadius
            border.color: root.page.panelBorderColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: root.page.panelSpacing

                Label {
                    text: root.page.trashBatchResultTitle()
                    color: root.page.panelStrongTextColor
                    font.pixelSize: 18
                    font.bold: true
                }

                RowLayout {
                    spacing: 24

                    Label {
                        text: "总计：" + trashManager.batchResultModel.totalCount
                        color: root.page.panelMutedTextColor
                    }

                    Label {
                        text: "成功：" + trashManager.batchResultModel.successCount
                        color: root.page.panelSuccessTextColor
                    }

                    Label {
                        text: "失败：" + trashManager.batchResultModel.failureCount
                        color: root.page.shareBatchResultSummaryColor(trashManager.batchResultModel.failureCount)
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.page.panelMutedFillColor
                    radius: root.page.innerPanelRadius
                    border.color: root.page.panelBorderColor

                    ListView {
                        id: trashBatchResultListView
                        objectName: "trashBatchResultListView"
                        anchors.fill: parent
                        anchors.margins: 10
                        model: trashManager.batchResultModel
                        clip: true
                        spacing: 1

                        delegate: ItemDelegate {
                            width: ListView.view.width

                            background: Rectangle {
                                radius: root.page.innerPanelRadius
                                color: hovered ? root.page.panelBackgroundColor : "transparent"
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: root.page.tableColumnSpacing

                                Label {
                                    text: status === "success" ? "✓" : "✗"
                                    color: status === "success" ? root.page.panelSuccessTextColor : root.page.panelErrorTextColor
                                    font.pixelSize: 16
                                    font.bold: true
                                }

                                Label {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: resourceKey
                                    color: root.page.tableBodyPrimaryTextColor
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: status
                                    color: root.page.tableBodySecondaryTextColor
                                    font.pixelSize: 12
                                }

                                Label {
                                    text: resolvedPath ? ("→ " + resolvedPath) : ""
                                    color: root.page.tableBodySecondaryTextColor
                                    font.pixelSize: 12
                                    visible: text !== ""
                                    wrapMode: Text.WordWrap
                                }

                                Label {
                                    text: freedSpace ? ("已释放：" + root.page.formatSize(freedSpace)) : ""
                                    color: root.page.tableBodySecondaryTextColor
                                    font.pixelSize: 12
                                    visible: text !== ""
                                }

                                Label {
                                    text: error && error.message ? error.message : ""
                                    color: root.page.panelErrorTextColor
                                    font.pixelSize: 12
                                    visible: text !== ""
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }
                }

                Button {
                    objectName: "trashBatchResultBackButton"
                    text: "返回回收站"
                    highlighted: true
                    onClicked: root.page.refreshTrashList()
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: page.panelBackgroundColor
        radius: page.panelRadius
        border.color: page.panelBorderColor

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: page.contentInset
            spacing: page.panelSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: page.tableColumnSpacing

                Label {
                    text: page.currentTrashItemCount === 1 ? "1 项" : page.currentTrashItemCount + " 项"
                    color: page.panelMutedTextColor
                    font.pixelSize: 12
                    font.bold: true
                }

                Label {
                    text: page.selectedTrashIds.length > 0
                          ? (page.selectedTrashIds.length === 1 ? "1 个已选" : page.selectedTrashIds.length + " 个已选")
                          : "选择项目以启用回收站操作"
                    color: page.panelSecondaryTextColor
                    font.pixelSize: 12
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: page.panelMutedFillColor
                radius: page.innerPanelRadius
                border.color: page.panelBorderColor

                ListView {
                    id: trashListView
                    objectName: "trashListView"
                    anchors.fill: parent
                    anchors.margins: 10
                    model: trashManager.listModel
                    clip: true
                    spacing: 1

                    delegate: ItemDelegate {
                        id: trashRowDelegate
                        objectName: "trashRowDelegate_" + String(model.trashId)
                        width: ListView.view.width
                        implicitHeight: Math.max(80, trashRowLayout.implicitHeight + 20)
                        highlighted: page.isTrashSelected(model.trashId)
                        hoverEnabled: true

                        onClicked: page.toggleTrashSelection(model.trashId)

                        background: Rectangle {
                            radius: page.innerPanelRadius
                            color: trashRowDelegate.highlighted
                                   ? page.panelAccentFillColor
                                   : (trashRowDelegate.hovered ? page.panelBackgroundColor : "transparent")
                            border.color: trashRowDelegate.highlighted ? page.panelBorderColor : "transparent"
                        }

                        RowLayout {
                            id: trashRowLayout
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: page.tableColumnSpacing

                            CheckBox {
                                checked: page.isTrashSelected(model.trashId)
                                onClicked: mouse.accepted = false
                            }

                            Label {
                                text: model.kind === "folder" ? "📁" : "📄"
                                font.pixelSize: 18
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 2

                                Label {
                                    objectName: "trashNameLabel_" + String(model.trashId)
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: model.name
                                    color: page.tableBodyPrimaryTextColor
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    objectName: "trashOriginalPathLabel_" + String(model.trashId)
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: model.originalPath
                                    color: page.tableBodySecondaryTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideLeft
                                }
                            }

                            Label {
                                text: page.formatSize(model.size)
                                color: page.tableBodySecondaryTextColor
                                font.pixelSize: 12
                            }

                            Label {
                                objectName: "trashDeletedAtLabel_" + String(model.trashId)
                                text: model.deletedAt ? Qt.formatDateTime(model.deletedAt, "yyyy-MM-dd") : ""
                                color: page.tableBodyTertiaryTextColor
                                font.pixelSize: 12
                            }

                            Button {
                                objectName: "trashRestoreButton_" + String(model.trashId)
                                text: "恢复"
                                flat: true
                                onClicked: page.submitRestoreTrash(model.trashId)
                            }

                            Button {
                                objectName: "trashDeleteButton_" + String(model.trashId)
                                text: "删除"
                                flat: true
                                onClicked: page.submitDeleteTrash(model.trashId)
                            }
                        }
                    }
                }
            }
        }
    }
}
