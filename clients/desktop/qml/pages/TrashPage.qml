import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Page {
    id: root

    property var selectedTrashIds: []

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
                        color: "#666"
                    }
                    Label {
                        text: "Succeeded: " + trashManager.batchResultModel.successCount
                        color: "#4caf50"
                    }
                    Label {
                        text: "Failed: " + trashManager.batchResultModel.failureCount
                        color: trashManager.batchResultModel.failureCount > 0 ? "#f44336" : "#666"
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
                                color: status === "success" ? "#4caf50" : "#f44336"
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
                                color: "#888"
                            }

                            Label {
                                text: resolvedPath ? ("→ " + resolvedPath) : ""
                                font.pixelSize: 12
                                color: "#666"
                                visible: resolvedPath !== undefined && resolvedPath !== ""
                            }

                            Label {
                                text: freedSpace ? ("Freed: " + freedSpace + " bytes") : ""
                                font.pixelSize: 12
                                color: "#666"
                                visible: freedSpace !== undefined && freedSpace > 0
                            }

                            Label {
                                text: {
                                    if (error && error.message) return error.message
                                    return ""
                                }
                                font.pixelSize: 12
                                color: "#f44336"
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
                                color: "#888"
                                elide: Text.ElideLeft
                                Layout.fillWidth: true
                            }
                        }

                        Label {
                            text: {
                                var sz = model.size
                                if (sz < 1024) return sz + " B"
                                if (sz < 1048576) return (sz / 1024).toFixed(1) + " KB"
                                if (sz < 1073741824) return (sz / 1048576).toFixed(1) + " MB"
                                return (sz / 1073741824).toFixed(1) + " GB"
                            }
                            font.pixelSize: 12
                            color: "#888"
                        }

                        Label {
                            text: {
                                var d = model.deletedAt
                                return d ? Qt.formatDateTime(d, "yyyy-MM-dd") : ""
                            }
                            font.pixelSize: 11
                            color: "#aaa"
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