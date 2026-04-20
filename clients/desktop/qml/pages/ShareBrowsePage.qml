import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Page {
    id: root

    property string shareId: ""
    property string currentFolderId: ""
    property string permission: "download"

    header: RowLayout {
        spacing: 12

        BreadcrumbBar {
            id: breadcrumbBar
            Layout.fillWidth: true
            Layout.leftMargin: 16

            path: [{ id: "root", name: "Shared Files" }]

            onPathClicked: function(index) {
                if (index === 0) {
                    root.currentFolderId = ""
                    shareManager.browseShare(root.shareId, "")
                }
            }
        }

        Label {
            text: "Permission: " + root.permission
            font.pixelSize: 12
            color: "#888"
            Layout.rightMargin: 16
        }
    }

    PageStateView {
        id: stateView
        anchors.fill: parent
        pageState: shellController.pageState

        emptyText: "This share is empty"
        errorText: "Failed to load share contents"

        onRetryClicked: shareManager.browseShare(root.shareId, root.currentFolderId)

        ColumnLayout {
            anchors.fill: parent

            ListView {
                id: browseListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: shareManager.browseModel
                clip: true
                spacing: 1

                delegate: ItemDelegate {
                    width: ListView.view.width

                    onClicked: {
                        if (model.isDir) {
                            root.currentFolderId = model.id
                            shareManager.browseShare(root.shareId, model.id)

                            var newPath = breadcrumbBar.path.slice()
                            newPath.push({ id: model.id, name: model.name })
                            breadcrumbBar.path = newPath
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12

                        Label {
                            text: model.isDir ? "📁" : "📄"
                            font.pixelSize: 20
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
                                text: {
                                    if (model.isDir) {
                                        return (model.itemCount || 0) + " items"
                                    }
                                    var sz = model.size || 0
                                    if (sz < 1024) return sz + " B"
                                    if (sz < 1048576) return (sz / 1024).toFixed(1) + " KB"
                                    if (sz < 1073741824) return (sz / 1048576).toFixed(1) + " MB"
                                    return (sz / 1073741824).toFixed(1) + " GB"
                                }
                                font.pixelSize: 11
                                color: "#888"
                            }
                        }

                        Button {
                            text: "Download"
                            visible: !model.isDir && root.permission === "download"
                            flat: true
                            onClicked: {
                                transferManager.startShareDownload(root.shareId, model.id)
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: shareManager

        function onBrowseLoaded(shareId) {
            if (shareId === root.shareId && shareManager.browseModel.rowCount() === 0) {
                shellController.setPageState("empty")
            }
        }
    }

    Component.onCompleted: {
        shareManager.browseShare(root.shareId, "")
    }
}
