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
                text: "Up"
                visible: page.isMyFilesMode
                enabled: page.canNavigateUp
                onClicked: page.navigateToFolder(page.resolvedParentFolderId)
            }

            Button {
                objectName: "folderNavigatorToggleButton"
                text: page.folderNavigatorExpanded ? "Hide folders" : "Folders"
                visible: page.isMyFilesMode
                onClicked: page.folderNavigatorExpanded = !page.folderNavigatorExpanded
            }

            Button {
                text: "Refresh"
                highlighted: true
                onClicked: page.refreshCurrentView()
            }

            Button {
                objectName: "sharedCreateButton"
                text: "Create Share"
                visible: page.isSharedMode
                enabled: !page.shareMutationInFlight
                highlighted: true
                onClicked: page.openCreateShareDialog()
            }

            Button {
                objectName: "sharedCancelSelectedButton"
                text: page.pendingShareMutationAction === "cancel" && page.shareMutationInFlight
                      ? "Cancelling..." : "Cancel Selected"
                visible: page.isSharedMode && page.selectedShareIds.length > 0
                enabled: !page.shareMutationInFlight
                onClicked: page.submitCancelSelectedShares()
            }

            Button {
                objectName: "trashRestoreSelectedButton"
                text: "Restore Selected"
                visible: page.isTrashMode && page.selectedTrashIds.length > 0
                onClicked: page.submitRestoreSelectedTrash()
            }

            Button {
                objectName: "trashDeleteSelectedButton"
                text: "Delete Selected"
                visible: page.isTrashMode && page.selectedTrashIds.length > 0
                onClicked: page.submitDeleteSelectedTrash()
            }

            Button {
                objectName: "trashClearAllButton"
                text: "Clear All"
                visible: page.isTrashMode
                highlighted: true
                onClicked: page.submitClearTrash()
            }

            Button {
                text: "New Folder"
                visible: page.isMyFilesMode
                onClicked: page.openCreateFolderDialog()
            }

            Button {
                id: uploadButton
                text: "Upload"
                visible: page.isMyFilesMode
                onClicked: page.openUploadFileChooser()
            }

            Button {
                text: "Rename"
                visible: page.isMyFilesMode
                enabled: page.selectedItemId !== ""
                onClicked: page.openRenameDialog()
            }

            Button {
                text: "Delete"
                visible: page.isMyFilesMode
                enabled: page.selectedItemId !== ""
                onClicked: page.openDeleteDialog()
            }

            Button {
                id: downloadButton
                text: "Download"
                visible: page.isMyFilesMode
                enabled: page.selectedItemKind === "file"
                onClicked: page.openOwnerDownloadFileChooser(page.selectedItemId, page.selectedItemName)
            }

            Label {
                objectName: "multiSelectCount"
                text: page.selectedItemIds.length > 1 ? page.selectedItemIds.length + " items selected" : ""
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
