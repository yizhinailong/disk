import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/FormatUtils.js" as FormatUtils

Page {
    id: root

    property var selectedTrashIds: []

    WorkspaceTheme { id: theme }

    readonly property color successColor: theme.successChipColor
    readonly property color errorColor: theme.errorTextColor
    readonly property color secondaryColor: theme.secondaryTextColor
    readonly property color tertiaryColor: theme.tertiaryTextColor

    header: RowLayout {
        spacing: 12

        Label {
            text: "Trash"
            font.pixelSize: 24
            font.bold: true
            Layout.leftMargin: 16
        }

        Item { Layout.fillWidth: true }

        Button {
            text: "Restore Selected"
            visible: root.selectedTrashIds.length > 0
            onClicked: {
                trashManager.restoreItems(root.selectedTrashIds)
                root.selectedTrashIds = []
            }
        }

        Button {
            text: "Delete Selected"
            visible: root.selectedTrashIds.length > 0
            onClicked: {
                trashManager.deleteItems(root.selectedTrashIds)
                root.selectedTrashIds = []
            }
        }

        Button {
            text: "Clear All"
            highlighted: true
            onClicked: trashManager.clearAll()
        }
    }

    PageStateView {
        id: stateView
        anchors.fill: parent
        pageState: shellController.pageState

        emptyText: "Trash is empty"
        errorText: "Failed to load trash items"

        onRetryClicked: trashManager.listTrash()

        batchResultComponent: Component {
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                Label {
                    text: trashManager.batchResultModel.operation === "trash_restore" ?
                           "Restore Results" : "Delete Results"
                    font.pixelSize: 18
                    font.bold: true
                }

                RowLayout {
                    spacing: 24
                    Label {
                        text: "Total: " + trashManager.batchResultModel.totalCount
                        color: root.secondaryColor
                    }
                    Label {
                        text: "Succeeded: " + trashManager.batchResultModel.successCount
                        color: root.successColor
                    }
                    Label {
                        text: "Failed: " + trashManager.batchResultModel.failureCount
                        color: trashManager.batchResultModel.failureCount > 0 ? root.errorColor : root.secondaryColor
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: trashManager.batchResultModel
                    clip: true

                    delegate: ItemDelegate {
                        width: ListView.view.width

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12

                            Label {
                                text: status === "success" ? "✓" : "✗"
                                color: status === "success" ? root.successColor : root.errorColor
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Label {
                                text: resourceKey
                                font.pixelSize: 14
                            }

                            Label {
                                text: status
                                font.pixelSize: 12
                                color: root.tertiaryColor
                            }

                            Label {
                                text: resolvedPath ? ("→ " + resolvedPath) : ""
                                font.pixelSize: 12
                                color: root.secondaryColor
                                visible: resolvedPath !== undefined && resolvedPath !== ""
                            }

                            Label {
                                text: freedSpace ? ("Freed: " + freedSpace + " bytes") : ""
                                font.pixelSize: 12
                                color: root.secondaryColor
                                visible: freedSpace !== undefined && freedSpace > 0
                            }

                            Label {
                                text: {
                                    if (error && error.message) return error.message
                                    return ""
                                }
                                font.pixelSize: 12
                                color: root.errorColor
                                visible: text !== ""
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }

                Button {
                    text: "Back to Trash"
                    onClicked: {
                        trashManager.listTrash()
                        shellController.setPageState("content")
                    }
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent

            ListView {
                id: trashListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: trashManager.listModel
                clip: true
                spacing: 1

                delegate: ItemDelegate {
                    width: ListView.view.width
                    highlighted: root.selectedTrashIds.indexOf(model.trashId.toString()) >= 0

                    onClicked: {
                        var idStr = model.trashId.toString()
                        var idx = root.selectedTrashIds.indexOf(idStr)
                        var copy = root.selectedTrashIds.slice()
                        if (idx >= 0) {
                            copy.splice(idx, 1)
                        } else {
                            copy.push(idStr)
                        }
                        root.selectedTrashIds = copy
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12

                        CheckBox {
                            checked: root.selectedTrashIds.indexOf(model.trashId.toString()) >= 0
                            onClicked: mouse.accepted = false
                        }

                        Label {
                            text: model.kind === "folder" ? "📁" : "📄"
                            font.pixelSize: 18
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                text: model.name
                                font.pixelSize: 14
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: model.originalPath
                                font.pixelSize: 11
                                color: root.tertiaryColor
                                elide: Text.ElideLeft
                                Layout.fillWidth: true
                            }
                        }

                        Label {
                            text: FormatUtils.formatSize(model.size)
                            font.pixelSize: 12
                            color: root.tertiaryColor
                        }

                        Label {
                            text: {
                                var d = model.deletedAt
                                return d ? Qt.formatDateTime(d, "yyyy-MM-dd") : ""
                            }
                            font.pixelSize: 11
                            color: root.tertiaryColor
                        }

                        Button {
                            text: "Restore"
                            flat: true
                            onClicked: trashManager.restoreItems([model.trashId.toString()])
                        }

                        Button {
                            text: "Delete"
                            flat: true
                            onClicked: trashManager.deleteItems([model.trashId.toString()])
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: trashManager

        function onBatchResultReady() {
            shellController.setPageState("batchResult")
        }

        function onOperationSuccess(message) {
            if (shellController.pageState !== "batchResult") {
                trashManager.listTrash()
            }
        }

        function onClearAllCompleted(deletedCount, freedSpace) {
            root.selectedTrashIds = []
        }
    }

    Component.onCompleted: {
        trashManager.listTrash()
    }
}