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
                    text: qsTr("Uploads (%1)").arg(uploadList.count)
                    width: implicitWidth
                }
                TabButton {
                    text: qsTr("Downloads (%1)").arg(downloadList.count)
                    width: implicitWidth
                }
            }

            Button {
                text: qsTr("Clear Completed")
                onClicked: transferManager.ClearCompletedUploads()
                visible: tabBar.currentIndex === 0
            }
            Button {
                text: qsTr("Clear Completed")
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
                                        "queued": qsTr("Queued"),
                                        "hashing": qsTr("Computing hash..."),
                                        "initializing": qsTr("Initializing..."),
                                        "uploading": qsTr("Uploading..."),
                                        "completing": qsTr("Completing..."),
                                        "cancelling": qsTr("Cancelling..."),
                                        "completed": qsTr("Completed"),
                                        "cancelled": qsTr("Cancelled"),
                                        "expired": qsTr("Expired"),
                                        "failed": qsTr("Failed"),
                                        "retrying": qsTr("Retrying...")
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
                                text: qsTr("Cancel")
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
                                text: qsTr("Retry")
                                flat: true
                                palette.buttonText: "#1976d2"
                                onClicked: transferManager.RetryUpload(model.taskId)
                            }
                            Button {
                                visible: model.status !== "completed"
                                text: qsTr("Dismiss")
                                flat: true
                                onClicked: transferManager.ClearCompletedUploads()
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    text: qsTr("No uploads")
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
                                        "idle": qsTr("Idle"),
                                        "fetching_metadata": qsTr("Fetching info..."),
                                        "ready": qsTr("Ready"),
                                        "downloading": qsTr("Downloading..."),
                                        "paused": qsTr("Paused"),
                                        "retry_waiting": qsTr("Waiting to retry..."),
                                        "completed": qsTr("Completed"),
                                        "cancelled": qsTr("Cancelled"),
                                        "failed": qsTr("Failed")
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
                                text: qsTr("Pause")
                                flat: true
                                palette.buttonText: root.warningColor
                                onClicked: transferManager.PauseDownload(model.taskId)
                            }
                            Button {
                                text: qsTr("Cancel")
                                flat: true
                                palette.buttonText: root.errorColor
                                onClicked: transferManager.CancelDownload(model.taskId)
                            }
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "paused"

                            Button {
                                text: qsTr("Resume")
                                flat: true
                                palette.buttonText: root.successColor
                                onClicked: transferManager.ResumeDownload(model.taskId)
                            }
                            Button {
                                text: qsTr("Cancel")
                                flat: true
                                palette.buttonText: root.errorColor
                                onClicked: transferManager.CancelDownload(model.taskId)
                            }
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "failed"

                            Button {
                                text: qsTr("Retry")
                                flat: true
                                palette.buttonText: "#1976d2"
                                onClicked: transferManager.RetryDownload(model.taskId)
                            }
                            Button {
                                text: qsTr("Dismiss")
                                flat: true
                                onClicked: transferManager.ClearCompletedDownloads()
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    text: qsTr("No downloads")
                    color: root.tertiaryColor
                    visible: downloadList.count === 0
                }
            }
        }
    }
}
