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
    property string pendingDownloadFileHash: ""
    property var selectedItemIds: []
    property bool saveInFlight: false
    property string saveErrorMessage: ""
    property string targetSaveFolderId: "0"
    property string targetSaveFolderName: "我的网盘"

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

    function openDownloadDialog(fileId, filename, fileSize, fileHash) {
        root.pendingDownloadFileId = String(fileId || "")
        root.pendingDownloadFilename = root.downloadBasename(filename)
        root.pendingDownloadFileSize = Number(fileSize || 0)
        root.pendingDownloadFileHash = String(fileHash || "")
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
            root.pendingDownloadFileSize,
            root.pendingDownloadFileHash
        )
        if (transferManager.downloadModel.rowCount() === downloadCountBefore) {
            root.downloadErrorMessage = "创建下载任务失败"
            return false
        }

        return true
    }

    function isSelected(itemId) {
        return root.selectedItemIds.indexOf(String(itemId || "")) >= 0
    }

    function toggleSelection(itemId) {
        var value = String(itemId || "")
        if (value === "") {
            return
        }
        var copy = root.selectedItemIds.slice()
        var index = copy.indexOf(value)
        if (index >= 0) {
            copy.splice(index, 1)
        } else {
            copy.push(value)
        }
        root.selectedItemIds = copy
    }

    function selectedSaveIds() {
        var fileIds = []
        var folderIds = []
        for (var index = 0; index < root.selectedItemIds.length; ++index) {
            var id = String(root.selectedItemIds[index] || "")
            var item = shareManager.browseModel.GetItemById(id)
            if (!item) {
                continue
            }
            if (String(item.kind || "") === "folder") {
                folderIds.push(id)
            } else {
                fileIds.push(id)
            }
        }
        return { fileIds: fileIds, folderIds: folderIds }
    }

    function openSaveDestinationDialog() {
        if (root.permission !== "download" || root.selectedItemIds.length === 0 || root.saveInFlight) {
            return
        }
        var ids = root.selectedSaveIds()
        if (ids.fileIds.length === 0 && ids.folderIds.length === 0) {
            root.saveErrorMessage = "请选择要保存的项目"
            return
        }
        root.saveErrorMessage = ""
        root.targetSaveFolderId = "0"
        root.targetSaveFolderName = "我的网盘"
        driveManager.loadFolderTree()
        saveDestinationDialog.open()
    }

    function saveSelectedItems() {
        if (root.permission !== "download" || root.selectedItemIds.length === 0 || root.saveInFlight) {
            return
        }
        var ids = root.selectedSaveIds()
        if (ids.fileIds.length === 0 && ids.folderIds.length === 0) {
            root.saveErrorMessage = "请选择要保存的项目"
            return
        }
        root.saveErrorMessage = ""
        root.saveInFlight = true
        shareManager.saveShareItems(root.shareId, ids.fileIds, ids.folderIds, root.targetSaveFolderId)
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
                        root.selectedItemIds = []
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
                text: root.saveInFlight ? "保存中..." : "保存到我的网盘"
                visible: root.permission === "download" && root.selectedItemIds.length > 0
                enabled: !root.saveInFlight
                onClicked: root.openSaveDestinationDialog()
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

    Dialog {
        id: saveDestinationDialog
        objectName: "shareSaveDestinationDialog"
        modal: true
        width: 420
        height: 520
        title: "保存到我的网盘"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        ColumnLayout {
            width: parent.width
            height: parent.height
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: "选择保存目标文件夹。当前目标：" + root.targetSaveFolderName
                color: root.tertiaryColor
                wrapMode: Text.WordWrap
            }

            Button {
                Layout.fillWidth: true
                text: "我的网盘（根目录）"
                enabled: !root.saveInFlight
                onClicked: {
                    root.targetSaveFolderId = "0"
                    root.targetSaveFolderName = "我的网盘"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: theme.panelMutedFillColor
                radius: theme.innerPanelRadius
                border.color: theme.panelBorderColor

                FolderTreePanel {
                    anchors.fill: parent
                    model: driveManager.treeModel
                    currentFolderId: root.targetSaveFolderId
                    onFolderClicked: function(folderId) {
                        root.targetSaveFolderId = folderId
                        root.targetSaveFolderName = "文件夹 " + folderId
                    }
                    onCloseRequested: saveDestinationDialog.close()
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.saveErrorMessage
                color: theme.errorTextColor
                visible: text !== ""
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    enabled: !root.saveInFlight
                    onClicked: saveDestinationDialog.close()
                }

                Button {
                    text: root.saveInFlight ? "保存中..." : "保存"
                    highlighted: true
                    enabled: !root.saveInFlight
                    onClicked: root.saveSelectedItems()
                }
            }
        }
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

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                text: root.saveErrorMessage
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
                        if (model.kind === "folder") {
                            root.selectedItemIds = []
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

                        CheckBox {
                            checked: root.isSelected(model.id)
                            onClicked: root.toggleSelection(model.id)
                        }

                        Label {
                            text: model.kind === "folder" ? "📁" : "📄"
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
                                    if (model.kind === "folder") {
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
                            visible: model.kind !== "folder" && root.permission === "download"
                            flat: true
                            onClicked: root.openDownloadDialog(model.id, model.name, model.size || 0, model.hash || "")
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

        function onOperationSuccess(message) {
            if (!root.saveInFlight) {
                return
            }
            root.saveInFlight = false
            root.selectedItemIds = []
            root.saveErrorMessage = ""
            saveDestinationDialog.close()
        }

        function onApiError(message, code) {
            if (root.saveInFlight) {
                root.saveInFlight = false
            }
            root.saveErrorMessage = message
        }
    }

    Component.onCompleted: {
        shareManager.browseShare(root.shareId, "")
    }
}
