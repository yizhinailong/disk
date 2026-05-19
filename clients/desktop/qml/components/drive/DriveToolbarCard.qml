import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var page
    property alias uploadButton: uploadButton
    property alias downloadButton: downloadButton

    color: page.panelBackgroundColor
    radius: page.panelRadius
    border.color: page.panelBorderColor
    implicitHeight: toolbarCardLayout.implicitHeight + 24

    ColumnLayout {
        id: toolbarCardLayout
        anchors.fill: parent
        anchors.margins: page.panelInset
        spacing: page.compactSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: page.compactSpacing

            Button {
                objectName: "homepageUpButton"
                text: "上级"
                visible: page.isMyFilesMode
                enabled: page.canNavigateUp
                onClicked: page.navigateToFolder(page.resolvedParentFolderId)
            }

            Button {
                objectName: "folderNavigatorToggleButton"
                text: page.folderNavigatorExpanded ? "隐藏文件夹" : "文件夹"
                visible: page.isMyFilesMode
                onClicked: page.folderNavigatorExpanded = !page.folderNavigatorExpanded
            }

            Button {
                text: "刷新"
                highlighted: true
                onClicked: page.refreshCurrentView()
            }

            TextField {
                objectName: "visitorShareInput"
                Layout.preferredWidth: 240
                visible: page.isSharedMode
                placeholderText: "输入分享码或粘贴分享链接"
                text: page.visitorShareInputText
                onTextChanged: page.visitorShareInputText = text
                onAccepted: page.openVisitorShare()
            }

            Button {
                objectName: "visitorAccessButton"
                text: "访问分享"
                visible: page.isSharedMode
                highlighted: true
                enabled: page.visitorShareInputText.trim().length > 0
                onClicked: page.openVisitorShare()
            }

            Button {
                objectName: "sharedCreateButton"
                text: "创建分享"
                visible: page.isMyFilesMode && page.selectedItemId !== ""
                enabled: !page.shareMutationInFlight
                highlighted: true
                onClicked: page.openCreateShareDialog()
            }

            Button {
                objectName: "sharedCancelSelectedButton"
                text: page.pendingShareMutationAction === "cancel" && page.shareMutationInFlight
                      ? "正在取消..." : "取消选中"
                visible: page.isSharedMode && page.selectedShareIds.length > 0
                enabled: !page.shareMutationInFlight
                onClicked: page.submitCancelSelectedShares()
            }

            Button {
                objectName: "trashRestoreSelectedButton"
                text: "恢复选中"
                visible: page.isTrashMode && page.selectedTrashIds.length > 0
                onClicked: page.submitRestoreSelectedTrash()
            }

            Button {
                objectName: "trashDeleteSelectedButton"
                text: "删除选中"
                visible: page.isTrashMode && page.selectedTrashIds.length > 0
                onClicked: page.submitDeleteSelectedTrash()
            }

            Button {
                objectName: "trashClearAllButton"
                text: "清空全部"
                visible: page.isTrashMode
                highlighted: true
                onClicked: page.submitClearTrash()
            }

            Button {
                text: "新建文件夹"
                visible: page.isMyFilesMode
                onClicked: page.openCreateFolderDialog()
            }

            Button {
                id: uploadButton
                text: "上传"
                visible: page.isMyFilesMode
                onClicked: page.openUploadFileChooser()
            }

            Button {
                text: "重命名"
                visible: page.isMyFilesMode
                enabled: page.selectedItemId !== ""
                onClicked: page.openRenameDialog()
            }

            Button {
                text: "删除"
                visible: page.isMyFilesMode
                enabled: page.selectedItemId !== ""
                onClicked: page.openDeleteDialog()
            }

            Button {
                id: downloadButton
                text: "下载"
                visible: page.isMyFilesMode
                enabled: page.selectedDownloadFileCount() > 0
                onClicked: page.openOwnerDownloadFileChooser(page.selectedItemId, page.selectedItemName)
            }

            Label {
                objectName: "multiSelectCount"
                text: page.selectedItemIds.length > 1 ? page.selectedItemIds.length + " 项已选择" : ""
                color: page.panelAccentTextColor
                font.pixelSize: 12
                visible: page.hasMultiSelection && page.isMyFilesMode
            }

            Item {
                Layout.fillWidth: true
            }
        }

        Label {
            Layout.fillWidth: true
            text: page.visitorEntryError
            color: page.panelErrorTextColor
            visible: text !== "" && page.isSharedMode
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            text: page.uploadErrorMessage
            color: page.panelErrorTextColor
            visible: text !== "" && page.isMyFilesMode
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            text: page.downloadErrorMessage
            color: page.panelErrorTextColor
            visible: text !== "" && page.isMyFilesMode
            wrapMode: Text.WordWrap
        }
    }
}
