import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    background: Rectangle { color: "#ffffff" }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
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
                    color: index % 2 === 0 ? "#fafafa" : "#ffffff"
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12

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
                                    if (s === "completed") return "#4caf50"
                                    if (s === "failed" || s === "expired") return "#f44336"
                                    if (s === "cancelled") return "#9e9e9e"
                                    if (s === "cancelling") return "#ff9800"
                                    return "#666666"
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
                            function formatSize(bytes) {
                                if (bytes < 1024) return bytes + " B"
                                if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KB"
                                if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + " MB"
                                return (bytes / 1073741824).toFixed(2) + " GB"
                            }
                            text: formatSize(model.fileSize)
                            font.pixelSize: 12
                            color: "#999"
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "uploading" ||
                                     model.status === "initializing" ||
                                     model.status === "hashing"

                            Button {
                                text: qsTr("Cancel")
                                flat: true
                                palette.buttonText: "#f44336"
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
                    color: "#999"
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
                    color: index % 2 === 0 ? "#fafafa" : "#ffffff"
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12

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
                                    if (s === "completed") return "#4caf50"
                                    if (s === "failed") return "#f44336"
                                    if (s === "cancelled") return "#9e9e9e"
                                    if (s === "paused") return "#ff9800"
                                    return "#666666"
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
                            function formatSize(bytes) {
                                if (bytes < 1024) return bytes + " B"
                                if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KB"
                                if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + " MB"
                                return (bytes / 1073741824).toFixed(2) + " GB"
                            }
                            text: formatSize(model.fileSize)
                            font.pixelSize: 12
                            color: "#999"
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "downloading" ||
                                     model.status === "fetching_metadata"

                            Button {
                                text: qsTr("Pause")
                                flat: true
                                palette.buttonText: "#ff9800"
                                onClicked: transferManager.PauseDownload(model.taskId)
                            }
                            Button {
                                text: qsTr("Cancel")
                                flat: true
                                palette.buttonText: "#f44336"
                                onClicked: transferManager.CancelDownload(model.taskId)
                            }
                        }

                        Row {
                            spacing: 4
                            visible: model.status === "paused"

                            Button {
                                text: qsTr("Resume")
                                flat: true
                                palette.buttonText: "#4caf50"
                                onClicked: transferManager.ResumeDownload(model.taskId)
                            }
                            Button {
                                text: qsTr("Cancel")
                                flat: true
                                palette.buttonText: "#f44336"
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
                    color: "#999"
                    visible: downloadList.count === 0
                }
            }
        }
    }
}
