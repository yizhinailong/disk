import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform
import "../components"

Page {
    id: root

    property string currentFolderId: "0"
    property string selectedItemId: ""
    property string selectedItemKind: ""
    property string selectedItemName: ""
    property bool mutationInFlight: false
    property string pendingMutationAction: ""
    property string createFolderErrorMessage: ""
    property string renameErrorMessage: ""
    property string deleteErrorMessage: ""
    property string uploadErrorMessage: ""
    property string downloadErrorMessage: ""
    property string pendingOwnerDownloadFileId: ""
    property string pendingOwnerDownloadFilename: ""

    function clearSelection() {
        selectedItemId = ""
        selectedItemKind = ""
        selectedItemName = ""
    }

    function selectItem(itemId, itemKind, itemName) {
        selectedItemId = String(itemId)
        selectedItemKind = String(itemKind || "")
        selectedItemName = String(itemName || "")
    }

    function clearMutationErrors() {
        createFolderErrorMessage = ""
        renameErrorMessage = ""
        deleteErrorMessage = ""
    }

    function resetMutationState() {
        mutationInFlight = false
        pendingMutationAction = ""
        clearMutationErrors()
    }

    function validateDriveItemName(name) {
        var value = name === undefined || name === null ? "" : String(name)
        if (value.length < 1 || value.length > 255) {
            return {
                valid: false,
                value: value,
                error: "Name must be 1-255 characters"
            }
        }
        if (value === "." || value === "..") {
            return {
                valid: false,
                value: value,
                error: 'Name cannot be "." or ".."'
            }
        }
        if (value.charAt(0) === ".") {
            return {
                valid: false,
                value: value,
                error: 'Name cannot start with "."'
            }
        }

        var forbiddenChars = "/\\\\:*?\"<>|"
        for (var index = 0; index < value.length; ++index) {
            var code = value.charCodeAt(index)
            var character = value.charAt(index)
            if (code < 0x20 || code > 0x7e) {
                return {
                    valid: false,
                    value: value,
                    error: "Name must use ASCII printable characters only"
                }
            }
            if (forbiddenChars.indexOf(character) >= 0) {
                return {
                    valid: false,
                    value: value,
                    error: 'Name cannot contain any of / \\ : * ? " < > |'
                }
            }
        }

        return {
            valid: true,
            value: value,
            error: ""
        }
    }

    function formatSize(bytes) {
        if (!bytes || bytes < 1024)
            return (bytes || 0) + " B"
        if (bytes < 1048576)
            return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1073741824)
            return (bytes / 1048576).toFixed(1) + " MB"
        return (bytes / 1073741824).toFixed(1) + " GB"
    }

    function openUploadFileChooser() {
        uploadErrorMessage = ""
        uploadFileDialogLoader.active = true
        if (uploadFileDialogLoader.item) {
            uploadFileDialogLoader.item.open()
        }
    }

    function normalizeUploadPath(path) {
        var value = path === undefined || path === null ? "" : String(path).trim()
        if (value.indexOf("file://") === 0) {
            value = decodeURIComponent(value.slice("file://".length))
            if (Qt.platform.os === "windows" && /^\/[A-Za-z]:/.test(value)) {
                value = value.slice(1)
            }
        }
        return value
    }

    function startUploadFromPath(path) {
        var localPath = root.normalizeUploadPath(path)
        if (localPath === "") {
            uploadErrorMessage = "Please choose one local file."
            uploadButton.forceActiveFocus()
            return false
        }

        uploadErrorMessage = ""

        var parentFolderId = Number(root.currentFolderId)
        if (!isFinite(parentFolderId) || parentFolderId < 0) {
            parentFolderId = 0
        }

        var uploadCountBefore = transferManager.uploadModel.rowCount()
        transferManager.StartUpload(localPath, parentFolderId)
        if (transferManager.uploadModel.rowCount() === uploadCountBefore) {
            uploadErrorMessage = "Please choose an existing local file."
            uploadButton.forceActiveFocus()
            return false
        }

        return true
    }

    function openOwnerDownloadFileChooser(fileId, filename) {
        pendingOwnerDownloadFileId = String(fileId || "")
        pendingOwnerDownloadFilename = String(filename || "")
        downloadErrorMessage = ""
        downloadFileDialogLoader.active = true
        if (downloadFileDialogLoader.item) {
            downloadFileDialogLoader.item.open()
        }
    }

    function normalizeDownloadPath(path) {
        var value = path === undefined || path === null ? "" : String(path).trim()
        if (value.indexOf("file://") === 0) {
            value = decodeURIComponent(value.slice("file://".length))
            if (Qt.platform.os === "windows" && /^\/[A-Za-z]:/.test(value)) {
                value = value.slice(1)
            }
        }
        return value
    }

    function startOwnerDownloadToPath(fileId, filename, targetPath) {
        var ownerFileId = Number(fileId)
        if (!isFinite(ownerFileId) || ownerFileId <= 0) {
            downloadErrorMessage = "Please select one file to download."
            downloadButton.forceActiveFocus()
            return false
        }

        var localPath = root.normalizeDownloadPath(targetPath)
        if (localPath === "") {
            downloadErrorMessage = "Please choose one download destination."
            downloadButton.forceActiveFocus()
            return false
        }

        downloadErrorMessage = ""

        var downloadCountBefore = transferManager.downloadModel.rowCount()
        transferManager.StartDownload(ownerFileId, localPath, "owner")
        if (transferManager.downloadModel.rowCount() === downloadCountBefore) {
            downloadErrorMessage = "Failed to create the download task."
            downloadButton.forceActiveFocus()
            return false
        }

        return true
    }

    function openCreateFolderDialog() {
        resetMutationState()
        newFolderNameField.text = ""
        newFolderDialog.open()
        newFolderNameField.forceActiveFocus()
    }

    function openRenameDialog() {
        if (selectedItemId === "") {
            return
        }

        resetMutationState()
        renameNameField.text = selectedItemName
        renameDialog.open()
        renameNameField.forceActiveFocus()
        renameNameField.selectAll()
    }

    function openDeleteDialog() {
        if (selectedItemId === "") {
            return
        }

        resetMutationState()
        deleteDialog.open()
    }

    function submitCreateFolder() {
        var validationResult = root.validateDriveItemName(newFolderNameField.text)
        if (!validationResult.valid) {
            createFolderErrorMessage = validationResult.error
            newFolderNameField.forceActiveFocus()
            return
        }

        clearMutationErrors()
        mutationInFlight = true
        pendingMutationAction = "create"
        driveManager.createFolder(root.currentFolderId, validationResult.value)
    }

    function submitRenameItem() {
        if (selectedItemId === "") {
            return
        }

        var validationResult = root.validateDriveItemName(renameNameField.text)
        if (!validationResult.valid) {
            renameErrorMessage = validationResult.error
            renameNameField.forceActiveFocus()
            return
        }

        clearMutationErrors()
        mutationInFlight = true
        pendingMutationAction = "rename"
        driveManager.renameItem(root.selectedItemId, validationResult.value)
    }

    function submitDeleteItem() {
        if (selectedItemId === "") {
            return
        }

        clearMutationErrors()
        mutationInFlight = true
        pendingMutationAction = "delete"
        driveManager.deleteItems([root.selectedItemId])
    }

    function applyMutationError(message) {
        var errorMessage = message || "Request failed. Please try again."
        mutationInFlight = false

        if (pendingMutationAction === "create") {
            createFolderErrorMessage = errorMessage
            newFolderNameField.forceActiveFocus()
            return
        }
        if (pendingMutationAction === "rename") {
            renameErrorMessage = errorMessage
            renameNameField.forceActiveFocus()
            return
        }
        if (pendingMutationAction === "delete") {
            deleteErrorMessage = errorMessage
        }
    }

    function finishMutationSuccess() {
        mutationInFlight = false
        pendingMutationAction = ""
        clearMutationErrors()
        newFolderDialog.close()
        renameDialog.close()
        deleteDialog.close()
        root.refreshCurrentFolder()
    }

    function refreshCurrentFolder() {
        clearSelection()
        shellController.setPageState("loading")
        driveManager.loadFolderTree()
        driveManager.listFiles(currentFolderId)
        driveManager.loadBreadcrumb(currentFolderId)
    }

    function openFolder(folderId) {
        currentFolderId = folderId ? String(folderId) : "0"
        refreshCurrentFolder()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 8

            Button {
                text: "Refresh"
                highlighted: true
                onClicked: root.refreshCurrentFolder()
            }

            Button {
                text: "New Folder"
                onClicked: root.openCreateFolderDialog()
            }

            Button {
                id: uploadButton
                text: "Upload"
                onClicked: root.openUploadFileChooser()
            }

            Button {
                text: "Rename"
                enabled: root.selectedItemId !== ""
                onClicked: root.openRenameDialog()
            }

            Button {
                text: "Delete"
                enabled: root.selectedItemId !== ""
                onClicked: root.openDeleteDialog()
            }

            Button {
                id: downloadButton
                text: "Download"
                enabled: root.selectedItemKind === "file"
                onClicked: root.openOwnerDownloadFileChooser(root.selectedItemId, root.selectedItemName)
            }

            Item {
                Layout.fillWidth: true
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            text: root.uploadErrorMessage
            color: "#f44336"
            visible: text !== ""
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            text: root.downloadErrorMessage
            color: "#f44336"
            visible: text !== ""
            wrapMode: Text.WordWrap
        }

        PageStateView {
            id: stateView
            Layout.fillWidth: true
            Layout.fillHeight: true
            pageState: shellController.pageState

            emptyText: "This folder is empty"
            errorText: "Failed to load folder contents"

            onRetryClicked: root.refreshCurrentFolder()

            ColumnLayout {
                anchors.fill: parent

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 10

                    BreadcrumbBar {
                        id: breadcrumbBar
                        Layout.fillWidth: true
                        onPathClicked: function(folderId) { root.openFolder(folderId) }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.bottomMargin: 10
                    spacing: 10

                    FolderTreePanel {
                        Layout.preferredWidth: 220
                        Layout.fillHeight: true
                        model: driveManager.treeModel
                        onFolderClicked: function(folderId) { root.openFolder(folderId) }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "#f5f5f5"

                        ListView {
                            id: fileListView
                            anchors.fill: parent
                            anchors.margins: 10
                            model: driveManager.listModel
                            clip: true
                            spacing: 1

                            delegate: ItemDelegate {
                                width: ListView.view.width
                                highlighted: root.selectedItemId === String(model.id)

                                onClicked: root.selectItem(model.id, model.kind, model.name)

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 12

                                    Label {
                                        text: model.kind === "folder" ? "📁" : "📄"
                                        font.pixelSize: 18
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
                                            text: model.kind === "folder"
                                                  ? ((model.itemCount || 0) + " items")
                                                  : root.formatSize(model.size)
                                            font.pixelSize: 11
                                            color: "#888"
                                        }
                                    }

                                    Button {
                                        text: "Open"
                                        flat: true
                                        visible: model.kind === "folder"
                                        onClicked: root.openFolder(model.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Loader {
        id: uploadFileDialogLoader
        active: false
        sourceComponent: uploadFileDialogComponent
    }

    Loader {
        id: downloadFileDialogLoader
        active: false
        sourceComponent: downloadFileDialogComponent
    }

    Component {
        id: uploadFileDialogComponent

        Platform.FileDialog {
            title: "Choose File to Upload"
            fileMode: Platform.FileDialog.OpenFile
            nameFilters: ["All files (*)"]

            onAccepted: root.startUploadFromPath(file)
            onRejected: root.startUploadFromPath("")
        }
    }

    Component {
        id: downloadFileDialogComponent

        Platform.FileDialog {
            title: root.pendingOwnerDownloadFilename === ""
                   ? "Choose Download Destination"
                   : "Save " + root.pendingOwnerDownloadFilename
            fileMode: Platform.FileDialog.SaveFile
            nameFilters: ["All files (*)"]

            onAccepted: root.startOwnerDownloadToPath(root.pendingOwnerDownloadFileId, root.pendingOwnerDownloadFilename, file)
            onRejected: root.startOwnerDownloadToPath(root.pendingOwnerDownloadFileId, root.pendingOwnerDownloadFilename, "")
        }
    }

    Dialog {
        id: newFolderDialog
        modal: true
        width: 360
        title: "New Folder"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        onClosed: {
            if (!root.mutationInFlight) {
                root.resetMutationState()
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: "Enter a name for the new folder."
                color: "#666"
                wrapMode: Text.WordWrap
            }

            TextField {
                id: newFolderNameField
                Layout.fillWidth: true
                maximumLength: 255
                enabled: !root.mutationInFlight
                placeholderText: "Folder name"
                onAccepted: root.submitCreateFolder()
            }

            Label {
                Layout.fillWidth: true
                text: root.createFolderErrorMessage
                color: "#f44336"
                visible: text !== ""
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "Cancel"
                    enabled: !root.mutationInFlight
                    onClicked: newFolderDialog.close()
                }

                Button {
                    text: root.pendingMutationAction === "create" && root.mutationInFlight
                          ? "Creating..." : "Create"
                    highlighted: true
                    enabled: !root.mutationInFlight
                    onClicked: root.submitCreateFolder()
                }
            }
        }
    }

    Dialog {
        id: renameDialog
        modal: true
        width: 360
        title: "Rename"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        onClosed: {
            if (!root.mutationInFlight) {
                root.resetMutationState()
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: "Enter a new name for the selected item."
                color: "#666"
                wrapMode: Text.WordWrap
            }

            TextField {
                id: renameNameField
                Layout.fillWidth: true
                maximumLength: 255
                enabled: !root.mutationInFlight
                placeholderText: "Item name"
                onAccepted: root.submitRenameItem()
            }

            Label {
                Layout.fillWidth: true
                text: root.renameErrorMessage
                color: "#f44336"
                visible: text !== ""
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "Cancel"
                    enabled: !root.mutationInFlight
                    onClicked: renameDialog.close()
                }

                Button {
                    text: root.pendingMutationAction === "rename" && root.mutationInFlight
                          ? "Saving..." : "Save"
                    highlighted: true
                    enabled: !root.mutationInFlight
                    onClicked: root.submitRenameItem()
                }
            }
        }
    }

    Dialog {
        id: deleteDialog
        modal: true
        width: 360
        title: "Delete"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        onClosed: {
            if (!root.mutationInFlight) {
                root.resetMutationState()
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: "Delete \"" + root.selectedItemName + "\"? This moves the selected item to trash."
                color: "#666"
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: root.deleteErrorMessage
                color: "#f44336"
                visible: text !== ""
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "Cancel"
                    enabled: !root.mutationInFlight
                    onClicked: deleteDialog.close()
                }

                Button {
                    text: root.pendingMutationAction === "delete" && root.mutationInFlight
                          ? "Deleting..." : "Delete"
                    highlighted: true
                    enabled: !root.mutationInFlight
                    onClicked: root.submitDeleteItem()
                }
            }
        }
    }

    Connections {
        target: driveManager

        function onApiError(message, code) {
            if (root.pendingMutationAction !== "") {
                root.applyMutationError(message)
            }
        }

        function onOperationSuccess(message) {
            if (root.pendingMutationAction === "create" || root.pendingMutationAction === "rename" || root.pendingMutationAction === "delete") {
                root.finishMutationSuccess()
            }
        }

        function onBreadcrumbLoaded(breadcrumb) {
            breadcrumbBar.path = breadcrumb
        }

        function onPaginationLoaded(page, totalPages, total) {
            shellController.setPageState(total > 0 ? "content" : "empty")
        }

        function onListLoadFailed(message, code) {
            shellController.setPageState("error")
        }
    }
    
    Component.onCompleted: {
        root.refreshCurrentFolder()
    }
}
