import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

PageStateView {
    id: root

    required property var page

    property string visitorEntryError: ""

    objectName: "sharedStateView"
    visible: page.isSharedMode
    pageState: shellController.pageState

    emptyText: "暂无分享"
    errorText: "加载分享失败"

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
                        text: "总计：" + shareManager.batchResultModel.totalCount
                        color: root.page.panelMutedTextColor
                    }

                    Label {
                        text: "成功：" + shareManager.batchResultModel.successCount
                        color: root.page.panelSuccessTextColor
                    }

                    Label {
                        text: "失败：" + shareManager.batchResultModel.failureCount
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
                    text: "返回分享"
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
                    text: page.currentShareItemCount === 1 ? "1 个分享" : page.currentShareItemCount + " 个分享"
                    color: page.panelMutedTextColor
                    font.pixelSize: 12
                    font.bold: true
                }

                Label {
                    text: page.selectedShareIds.length > 0
                          ? (page.selectedShareIds.length === 1 ? "1 个已选" : page.selectedShareIds.length + " 个已选")
                          : "选择项目以启用分享操作"
                    color: page.panelSecondaryTextColor
                    font.pixelSize: 12
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            RowLayout {
                id: visitorEntrySection
                Layout.fillWidth: true
                spacing: page.tableColumnSpacing

                TextField {
                    id: visitorShareInput
                    objectName: "visitorShareInput"
                    Layout.fillWidth: true
                    placeholderText: "输入分享码或粘贴分享链接"
                }

                Button {
                    id: visitorAccessButton
                    objectName: "visitorAccessButton"
                    text: "访问分享"
                    highlighted: true
                    enabled: visitorShareInput.text.trim().length > 0
                    onClicked: {
                        var shareId = shareManager.parseShareInput(visitorShareInput.text.trim())
                        if (shareId.length > 0) {
                            root.visitorEntryError = ""
                            shellController.navigateToVisitor(shareId)
                        } else {
                            root.visitorEntryError = "无效的分享码或链接"
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.visitorEntryError
                    color: page.panelErrorTextColor
                    font.pixelSize: 12
                    visible: text !== ""
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
                                    text: model.primaryItemName || "共享文件"
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
                                        text: model.hasPassword ? "密码保护" : "公开链接"
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
                                              ? (model.itemCount === 1 ? "1 项" : model.itemCount + " 项")
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
                                    text: (model.viewCount || 0) + " 次浏览"
                                    color: page.tableBodySecondaryTextColor
                                    font.pixelSize: 11
                                }

                                Label {
                                    text: (model.downloadCount || 0) + " 次下载"
                                    color: page.tableBodySecondaryTextColor
                                    font.pixelSize: 11
                                }

                                Label {
                                    text: page.formatShareDateTime(model.updatedAt, "无法获取更新日期")
                                    color: page.tableBodyTertiaryTextColor
                                    font.pixelSize: 11
                                }

                                Label {
                                    text: page.formatShareDateTime(model.expiresAt, "永久有效")
                                    color: page.tableBodyTertiaryTextColor
                                    font.pixelSize: 11
                                }
                            }

                            Button {
                                id: copyLinkButton
                                objectName: "sharedCopyLinkButton_" + String(model.shareId)
                                text: copyFeedbackActive ? "✓" : "复制链接"
                                flat: true
                                onClicked: {
                                    QGuiApplication.clipboard().setText(model.shareLink)
                                    copyFeedbackActive = true
                                    copyFeedbackTimer.start()
                                }

                                property bool copyFeedbackActive: false

                                Timer {
                                    id: copyFeedbackTimer
                                    interval: 2000
                                    onTriggered: copyLinkButton.copyFeedbackActive = false
                                }
                            }

                            Button {
                                objectName: "sharedEditButton_" + String(model.shareId)
                                text: "编辑"
                                flat: true
                                enabled: !page.shareMutationInFlight
                                onClicked: page.openEditShareDialog(model.shareId, model.permission)
                            }

                            Button {
                                objectName: "sharedCancelButton_" + String(model.shareId)
                                text: "取消"
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
