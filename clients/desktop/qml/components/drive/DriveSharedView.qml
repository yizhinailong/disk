import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

PageStateView {
    id: root

    required property var page

    objectName: "sharedStateView"
    visible: page.isSharedMode
    pageState: shellController.pageState

    emptyText: "No shares yet"
    errorText: "Failed to load shares"

    onRetryClicked: page.refreshSharedList()

    batchResultComponent: Component {
        Rectangle {
            objectName: "sharedBatchResultView"
            color: root.page.panelBackgroundColor
            radius: root.page.panelRadius
            border.color: root.page.panelBorderColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: root.page.panelSpacing

                Label {
                    text: root.page.shareBatchResultTitle()
                    color: root.page.panelStrongTextColor
                    font.pixelSize: 18
                    font.bold: true
                }

                RowLayout {
                    spacing: 24

                    Label {
                        text: "Total: " + shareManager.batchResultModel.totalCount
                        color: root.page.panelMutedTextColor
                    }

                    Label {
                        text: "Succeeded: " + shareManager.batchResultModel.successCount
                        color: root.page.panelSuccessTextColor
                    }

                    Label {
                        text: "Failed: " + shareManager.batchResultModel.failureCount
                        color: root.page.shareBatchResultSummaryColor(shareManager.batchResultModel.failureCount)
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
                        id: sharedBatchResultListView
                        objectName: "sharedBatchResultListView"
                        anchors.fill: parent
                        anchors.margins: 10
                        model: shareManager.batchResultModel
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
                    objectName: "sharedBatchResultBackButton"
                    text: "Back to Shares"
                    highlighted: true
                    onClicked: root.page.refreshSharedList()
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
                    text: page.currentShareItemCount === 1 ? "1 share" : page.currentShareItemCount + " shares"
                    color: page.panelMutedTextColor
                    font.pixelSize: 12
                    font.bold: true
                }

                Label {
                    text: page.selectedShareIds.length > 0
                          ? (page.selectedShareIds.length === 1 ? "1 selected" : page.selectedShareIds.length + " selected")
                          : "Selection updates share actions"
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
                    id: sharedListView
                    objectName: "sharedListView"
                    anchors.fill: parent
                    anchors.margins: 10
                    model: shareManager.listModel
                    clip: true
                    spacing: 1

                    delegate: ItemDelegate {
                        id: sharedRowDelegate
                        objectName: "sharedRowDelegate_" + String(model.shareId)
                        width: ListView.view.width
                        implicitHeight: Math.max(80, sharedRowLayout.implicitHeight + 20)
                        highlighted: page.isShareSelected(model.shareId)
                        hoverEnabled: true

                        onClicked: page.toggleShareSelection(model.shareId)

                        background: Rectangle {
                            radius: page.innerPanelRadius
                            color: sharedRowDelegate.highlighted
                                   ? page.panelAccentFillColor
                                   : (sharedRowDelegate.hovered ? page.panelBackgroundColor : "transparent")
                            border.color: sharedRowDelegate.highlighted ? page.panelBorderColor : "transparent"
                        }

                        RowLayout {
                            id: sharedRowLayout
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: page.tableColumnSpacing

                            CheckBox {
                                checked: page.isShareSelected(model.shareId)
                                onClicked: page.toggleShareSelection(model.shareId)
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                spacing: 2

                                Label {
                                    objectName: "sharedPrimaryItemLabel_" + String(model.shareId)
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: model.primaryItemName || "Shared files"
                                    color: page.tableBodyPrimaryTextColor
                                    font.pixelSize: 14
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    objectName: "sharedLinkLabel_" + String(model.shareId)
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: model.shareLink || ""
                                    color: page.tableBodySecondaryTextColor
                                    font.pixelSize: 12
                                    elide: Text.ElideMiddle
                                    visible: text !== ""
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    spacing: 8

                                    Label {
                                        text: page.formatSharePermission(model.permission)
                                        color: page.tableBodySecondaryTextColor
                                        font.pixelSize: 12
                                    }

                                    Label {
                                        text: model.hasPassword ? "Password protected" : "Open link"
                                        color: page.tableBodySecondaryTextColor
                                        font.pixelSize: 12
                                    }

                                    Label {
                                        text: page.formatShareStatus(model.status)
                                        color: page.shareStatusColor(model.status)
                                        font.pixelSize: 12
                                        font.bold: true
                                    }

                                    Label {
                                        text: model.itemCount !== undefined && model.itemCount !== null
                                              ? (model.itemCount === 1 ? "1 item" : model.itemCount + " items")
                                              : ""
                                        color: page.tableBodySecondaryTextColor
                                        font.pixelSize: 12
                                        visible: text !== ""
                                    }
                                }
                            }

                            ColumnLayout {
                                spacing: 2

                                Label {
                                    text: (model.viewCount || 0) + " views"
                                    color: page.tableBodySecondaryTextColor
                                    font.pixelSize: 11
                                }

                                Label {
                                    text: (model.downloadCount || 0) + " downloads"
                                    color: page.tableBodySecondaryTextColor
                                    font.pixelSize: 11
                                }

                                Label {
                                    text: page.formatShareDateTime(model.updatedAt, "Updated date unavailable")
                                    color: page.tableBodyTertiaryTextColor
                                    font.pixelSize: 11
                                }

                                Label {
                                    text: page.formatShareDateTime(model.expiresAt, "No expiry")
                                    color: page.tableBodyTertiaryTextColor
                                    font.pixelSize: 11
                                }
                            }

                            Button {
                                objectName: "sharedEditButton_" + String(model.shareId)
                                text: "Edit"
                                flat: true
                                enabled: !page.shareMutationInFlight
                                onClicked: page.openEditShareDialog(model.shareId, model.permission)
                            }

                            Button {
                                objectName: "sharedCancelButton_" + String(model.shareId)
                                text: "Cancel"
                                flat: true
                                enabled: !page.shareMutationInFlight
                                onClicked: page.submitCancelShare(model.shareId)
                            }
                        }
                    }
                }
            }
        }
    }
}
