import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Menu {
    id: contextMenu
    objectName: "driveContextMenu"

    required property var page
    property string targetItemId: ""
    property string targetItemKind: ""
    property string targetItemName: ""

    function hasTargetItem() {
        return contextMenu.targetItemId !== ""
    }

    function hasSelection() {
        return page.selectedItemIds.length > 0
    }

    function canMutateSelection() {
        return contextMenu.hasSelection() && !page.mutationInFlight
    }

    MenuItem {
        objectName: "contextMenuOpen"
        text: "打开"
        visible: contextMenu.targetItemKind === "folder"
        enabled: contextMenu.hasTargetItem()
        onTriggered: page.navigateToFolder(contextMenu.targetItemId)
    }

    MenuItem {
        objectName: "contextMenuDetail"
        text: "详情"
        visible: contextMenu.targetItemKind === "file"
        enabled: contextMenu.hasTargetItem()
        onTriggered: page.openFileDetailDialog(contextMenu.targetItemId)
    }

    MenuItem {
        objectName: "contextMenuDownload"
        text: "下载"
        enabled: page.selectedDownloadFileCount() > 0
        onTriggered: page.openOwnerDownloadFileChooser(contextMenu.targetItemId, contextMenu.targetItemName)
    }

    MenuSeparator {
        visible: contextMenu.hasTargetItem() || contextMenu.hasSelection()
    }

    MenuItem {
        objectName: "contextMenuRefresh"
        text: "刷新"
        onTriggered: page.refreshCurrentView()
    }

    MenuItem {
        objectName: "contextMenuCreateFolder"
        text: "新建文件夹"
        enabled: !page.mutationInFlight
        onTriggered: page.openCreateFolderDialog()
    }

    MenuItem {
        objectName: "contextMenuUpload"
        text: "上传"
        onTriggered: page.openUploadFileChooser()
    }

    MenuSeparator {}

    MenuItem {
        objectName: "contextMenuCreateShare"
        text: "创建分享"
        enabled: contextMenu.hasSelection() && !page.shareMutationInFlight
        onTriggered: page.openCreateShareDialog()
    }

    MenuItem {
        objectName: "contextMenuRename"
        text: "重命名"
        enabled: page.selectedItemIds.length === 1 && !page.mutationInFlight
        onTriggered: page.openRenameDialog()
    }

    MenuItem {
        objectName: "contextMenuMove"
        text: "移动"
        enabled: contextMenu.canMutateSelection()
        onTriggered: page.openMoveDialog()
    }

    MenuItem {
        objectName: "contextMenuCopy"
        text: "复制"
        enabled: contextMenu.canMutateSelection()
        onTriggered: page.openCopyDialog()
    }

    MenuItem {
        objectName: "contextMenuDelete"
        text: "删除"
        enabled: contextMenu.canMutateSelection()
        onTriggered: page.openDeleteDialog()
    }
}
