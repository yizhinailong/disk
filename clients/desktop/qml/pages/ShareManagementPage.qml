import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Page {
    id: root

    property var selectedShareIds: []
    property string currentShareId: ""

    header: RowLayout {
        spacing: 12
        Layout.margins: 16

        Label {
            text: "My Shares"
            font.pixelSize: 24
            font.bold: true
            Layout.leftMargin: 16
        }

        Item { Layout.fillWidth: true }

        Button {
            text: "Create Share"
            highlighted: true
            onClicked: createDialog.open()
        }

        Button {
            text: "Cancel Selected"
            visible: root.selectedShareIds.length > 0
            onClicked: {
                shareManager.cancelShares(root.selectedShareIds)
                root.selectedShareIds = []
            }
        }
    }

    PageStateView {
        id: stateView
        anchors.fill: parent
        pageState: shellController.pageState

        emptyText: "No shares yet"
        errorText: "Failed to load shares"

        onRetryClicked: shareManager.listShares()

        batchResultComponent: Component {
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                Label {
                    text: "Batch Cancel Results"
                    font.pixelSize: 18
                    font.bold: true
                }

                RowLayout {
                    spacing: 24
                    Label {
                        text: "Total: " + shareManager.batchResultModel.totalCount
                        color: "#666"
                    }
                    Label {
                        text: "Succeeded: " + shareManager.batchResultModel.successCount
                        color: "#4caf50"
                    }
                    Label {
                        text: "Failed: " + shareManager.batchResultModel.failureCount
                        color: shareManager.batchResultModel.failureCount > 0 ? "#f44336" : "#666"
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: shareManager.batchResultModel
                    clip: true

                    delegate: ItemDelegate {
                        width: ListView.view.width
                        text: resourceKey + " — " + status
                        icon.color: status === "success" ? "#4caf50" : "#f44336"
                    }
                }

                Button {
                    text: "Back to Shares"
                    onClicked: {
                        shareManager.listShares()
                        shellController.setPageState("content")
                    }
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent

            ListView {
                id: shareListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: shareManager.listModel
                clip: true
                spacing: 1

                delegate: ItemDelegate {
                    id: shareDelegate
                    width: ListView.view.width
                    highlighted: root.selectedShareIds.indexOf(model.shareId) >= 0

                    onClicked: {
                        var idx = root.selectedShareIds.indexOf(model.shareId)
                        var copy = root.selectedShareIds.slice()
                        if (idx >= 0) {
                            copy.splice(idx, 1)
                        } else {
                            copy.push(model.shareId)
                        }
                        root.selectedShareIds = copy
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12

                        CheckBox {
                            checked: root.selectedShareIds.indexOf(model.shareId) >= 0
                            onClicked: mouse.accepted = false
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                text: model.primaryItemName || "Shared files"
                                font.pixelSize: 14
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            RowLayout {
                                spacing: 8
                                Label {
                                    text: model.permission
                                    font.pixelSize: 12
                                    color: "#666"
                                }
                                Label {
                                    text: model.hasPassword ? "🔒" : ""
                                    font.pixelSize: 12
                                }
                                Label {
                                    text: model.status || "active"
                                    font.pixelSize: 12
                                    color: model.status === "expired" ? "#f44336" :
                                           model.status === "cancelled" ? "#999" : "#4caf50"
                                }
                            }
                        }

                        ColumnLayout {
                            spacing: 2

                            Label {
                                text: (model.viewCount || 0) + " views"
                                font.pixelSize: 11
                                color: "#888"
                                horizontalAlignment: Text.AlignRight
                            }
                            Label {
                                text: (model.downloadCount || 0) + " downloads"
                                font.pixelSize: 11
                                color: "#888"
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        Button {
                            text: "Edit"
                            flat: true
                            onClicked: {
                                root.currentShareId = model.shareId
                                editDialog.permission = model.permission
                                editDialog.open()
                            }
                        }

                        Button {
                            text: "Cancel"
                            flat: true
                            onClicked: shareManager.cancelShares([model.shareId])
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: createDialog
        title: "Create Share"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string selectedFileIds: []

        ColumnLayout {
            spacing: 12
            width: 320

            Label {
                text: "Permission:"
            }

            ComboBox {
                id: createPermissionCombo
                model: ["download", "view"]
                Layout.fillWidth: true
            }

            Label {
                text: "Password (optional, 4-8 chars):"
            }

            TextField {
                id: createPasswordField
                placeholderText: "No password"
                echoMode: TextInput.Password
                Layout.fillWidth: true
                maximumLength: 8
            }

            Label {
                text: "Expire (days, 0 = permanent):"
            }

            SpinBox {
                id: createExpireSpin
                from: 0
                to: 365
                value: 7
                Layout.fillWidth: true
            }
        }

        onAccepted: {
            shareManager.createShare(
                createDialog.selectedFileIds,
                createPermissionCombo.currentText,
                createPasswordField.text,
                createExpireSpin.value
            )
            createPasswordField.text = ""
        }
    }

    Dialog {
        id: editDialog
        title: "Edit Share"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string permission: "download"

        ColumnLayout {
            spacing: 12
            width: 320

            Label {
                text: "Permission:"
            }

            ComboBox {
                id: editPermissionCombo
                model: ["download", "view"]
                currentIndex: editDialog.permission === "view" ? 1 : 0
                Layout.fillWidth: true
            }

            Label {
                text: "New Password (empty to remove):"
            }

            TextField {
                id: editPasswordField
                placeholderText: "Leave empty to keep current"
                echoMode: TextInput.Password
                Layout.fillWidth: true
                maximumLength: 8
            }
        }

        onAccepted: {
            shareManager.updateShare(
                root.currentShareId,
                editPermissionCombo.currentText,
                editPasswordField.text
            )
            editPasswordField.text = ""
        }
    }

    Connections {
        target: shareManager

        function onBatchResultReady() {
            shellController.setPageState("batchResult")
        }

        function onOperationSuccess(message) {
            if (shellController.pageState !== "batchResult") {
                shareManager.listShares()
            }
        }
    }

    Component.onCompleted: {
        shareManager.listShares()
    }
}
