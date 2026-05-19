import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform
import "../components"
import "../components/FormatUtils.js" as FormatUtils

Page {
    id: root

    property string shareId: ""
    property string currentFolderId: ""
    property string permission: "download"
    property string downloadErrorMessage: ""
    property string pendingDownloadFileId: ""
    property string pendingDownloadFilename: ""
    property double pendingDownloadFileSize: 0

    WorkspaceTheme { id: theme }

    readonly property color tertiaryColor: theme.tertiaryTextColor

    function normalizeDownloadPath(path) {
        var value = String(path || "")
        if (value.indexOf("file://") === 0) {
            value = decodeURIComponent(value.slice(7))
            if (Qt.platform.os === "windows" && value.length > 0 && value.charAt(0) === "/") {
                value = value.slice(1)
            }
        }
        return value
    }

    function downloadBasename(filename) {
        var value = String(filename || "")
        value = value.replace(/\\/g, "/")
        var slashIndex = value.lastIndexOf("/")
        if (slashIndex >= 0) {
            value = value.slice(slashIndex + 1)
        }
        return value === "" ? "download" : value
    }

    function openDownloadDialog(fileId, filename, fileSize) {
        root.pendingDownloadFileId = String(fileId || "")
        root.pendingDownloadFilename = root.downloadBasename(filename)
        root.pendingDownloadFileSize = Number(fileSize || 0)
        root.downloadErrorMessage = ""
        shareDownloadFileDialogLoader.active = true
        shareDownloadFileDialogLoader.item.currentFile = root.pendingDownloadFilename
        shareDownloadFileDialogLoader.item.open()
    }

    function startShareDownloadToPath(targetPath) {
        var fileId = Number(root.pendingDownloadFileId)
        if (!isFinite(fileId) || fileId <= 0) {
            root.downloadErrorMessage = "请选择一个文件下载"
            return false
        }

        var localPath = root.normalizeDownloadPath(targetPath)
        if (localPath === "") {
            root.downloadErrorMessage = "请选择下载保存位置"
            return false
        }

        root.downloadErrorMessage = ""
        var downloadCountBefore = transferManager.downloadModel.rowCount()
        transferManager.StartShareDownload(
            root.shareId,
            fileId,
            localPath,
            root.pendingDownloadFilename,
            root.pendingDownloadFileSize
        )
        if (transferManager.downloadModel.rowCount() === downloadCountBefore) {
            root.downloadErrorMessage = "创建下载任务失败"
            return false
        }

        return true
    }

    header: Rectangle {
        implicitHeight: shareBrowseHeaderLayout.implicitHeight + 24
        color: theme.panelBackgroundColor
        border.color: theme.panelBorderColor

        RowLayout {
            id: shareBrowseHeaderLayout
            anchors.fill: parent
            anchors.margins: 12
            spacing: 12

            Label {
                text: "分享访问"
                font.pixelSize: 18
                font.bold: true
            }

            BreadcrumbBar {
                id: breadcrumbBar
                Layout.fillWidth: true
                path: [{ id: "root", name: "共享文件" }]

                onPathClicked: function(index) {
                    if (index === 0) {
                        root.currentFolderId = ""
                        shareManager.browseShare(root.shareId, "")
                    }
                }
            }

            Label {
                text: "权限：" + root.permission
                font.pixelSize: 12
                color: root.tertiaryColor
            }

            Button {
                text: "返回网盘"
                onClicked: sessionStore.visitor.CloseShare()
            }
        }
    }

    Loader {
        id: shareDownloadFileDialogLoader
        active: false
        sourceComponent: shareDownloadFileDialogComponent
    }

    Component {
        id: shareDownloadFileDialogComponent

        Platform.FileDialog {
            title: root.pendingDownloadFilename === ""
                   ? "选择下载保存位置"
                   : "保存 " + root.pendingDownloadFilename
            fileMode: Platform.FileDialog.SaveFile
            nameFilters: ["All files (*)"]

            onAccepted: root.startShareDownloadToPath(file)
            onRejected: root.startShareDownloadToPath("")
        }
    }

    PageStateView {
        id: stateView
        anchors.fill: parent
        pageState: shellController.pageState

        emptyText: "此分享为空"
        errorText: "加载分享内容失败"

        onRetryClicked: shareManager.browseShare(root.shareId, root.currentFolderId)

        ColumnLayout {
            anchors.fill: parent

            Label {
                Layout.fillWidth: true
                Layout.margins: 12
                text: root.downloadErrorMessage
                color: theme.errorTextColor
                visible: text !== ""
                wrapMode: Text.WordWrap
            }

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
                                        return (model.itemCount || 0) + " 项"
                                    }
                                    return FormatUtils.formatSize(model.size || 0)
                                }
                                font.pixelSize: 11
                                color: root.tertiaryColor
                            }
                        }

                        Button {
                            text: "下载"
                            visible: !model.isDir && root.permission === "download"
                            flat: true
                            onClicked: root.openDownloadDialog(model.id, model.name, model.size || 0)
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
