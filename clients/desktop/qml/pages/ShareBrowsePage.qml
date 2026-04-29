import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/FormatUtils.js" as FormatUtils

Page {
    id: root

    property string shareId: ""
    property string currentFolderId: ""
    property string permission: "download"

    WorkspaceTheme { id: theme }

    readonly property color tertiaryColor: theme.tertiaryTextColor

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
            color: root.tertiaryColor
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
                                    return FormatUtils.formatSize(model.size || 0)
                                }
                                font.pixelSize: 11
                                color: root.tertiaryColor
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
