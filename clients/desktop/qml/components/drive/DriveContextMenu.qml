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

    MenuItem {
        objectName: "contextMenuOpen"
        text: "打开"
        visible: contextMenu.targetItemKind === "folder"
        onTriggered: page.navigateToFolder(contextMenu.targetItemId)
    }

    MenuItem {
        objectName: "contextMenuDownload"
        text: "下载"
        visible: contextMenu.targetItemKind === "file"
        onTriggered: page.openOwnerDownloadFileChooser(contextMenu.targetItemId, contextMenu.targetItemName)
    }

    MenuSeparator {
        visible: contextMenu.targetItemKind === "folder" || contextMenu.targetItemKind === "file"
    }

    MenuItem {
        objectName: "contextMenuRename"
        text: "重命名"
        onTriggered: page.openRenameDialog()
    }

    MenuItem {
        objectName: "contextMenuDelete"
        text: "删除"
        onTriggered: page.openDeleteDialog()
    }
}
