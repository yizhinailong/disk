import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform
import "../components"

Page {
    id: root

    readonly property int pagePadding: 16
    readonly property int compactSpacing: 8
    readonly property int panelSpacing: 12
    readonly property int panelInset: 12
    readonly property int contentInset: 14
    readonly property int panelRadius: 10
    readonly property int innerPanelRadius: 8
    readonly property int fileRowHeight: 56
    readonly property int tableColumnSpacing: 12
    readonly property color pageBackgroundColor: "#f8fafb"
    readonly property color panelBackgroundColor: "#ffffff"
    readonly property color panelBorderColor: "#dfe5eb"
    readonly property color panelMutedFillColor: "#f3f5f7"
    readonly property color panelMutedTextColor: "#5f6b76"
    readonly property color panelSecondaryTextColor: "#666666"
    readonly property color panelTertiaryTextColor: "#888888"
    readonly property color panelStrongTextColor: "#1f2933"
    readonly property color panelAccentFillColor: "#dce8f5"
    readonly property color panelAccentTextColor: "#4f6b8a"
    readonly property color panelErrorTextColor: "#f44336"
    readonly property color tableHeaderTextColor: "#4d5c6b"
    readonly property color tableBodyPrimaryTextColor: "#1b2a38"
    readonly property color tableBodySecondaryTextColor: "#4d5c6b"
    readonly property color tableBodyTertiaryTextColor: "#667585"
    readonly property int fileTypeColumnWidth: 160
    readonly property int fileSizeColumnWidth: 120
    readonly property int fileUpdatedColumnWidth: 152
    readonly property int fileActionColumnWidth: 76
    readonly property int currentFolderItemCount: driveManager.listModel ? driveManager.listModel.rowCount() : 0
    readonly property string driveTitle: breadcrumbBar.path.length > 0
                                         && breadcrumbBar.path[breadcrumbBar.path.length - 1].name
                                         ? String(breadcrumbBar.path[breadcrumbBar.path.length - 1].name)
                                         : "My Drive"

    background: Rectangle {
        color: root.pageBackgroundColor
    }

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

    function folderScopeLabel() {
        return currentFolderId === "0" ? "Root folder" : "Nested folder"
    }

    function pageStateLabel() {
        switch (shellController.pageState) {
        case "loading":
            return "Refreshing"
        case "empty":
            return "Empty"
        case "error":
            return "Needs retry"
        default:
            return "Ready"
        }
    }

    function driveStatusSummary() {
        if (shellController.pageState === "loading") {
            return "Refreshing folder contents and drive navigation."
        }
        if (shellController.pageState === "error") {
            return "We could not load this folder. Retry to restore the drive view."
        }
        if (shellController.pageState === "empty") {
            return "This folder is empty. Use the toolbar to add new content."
        }
        if (selectedItemId !== "") {
            return 'Selected: "' + selectedItemName + '"'
        }

        return currentFolderItemCount === 1
               ? "1 item in this folder"
               : currentFolderItemCount + " items in this folder"
    }

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

    function formatItemType(kind, mimeType) {
        if (kind === "folder") {
            return "Folder"
        }

        var typeLabel = mimeType === undefined || mimeType === null ? "" : String(mimeType)
        return typeLabel !== "" ? typeLabel : "File"
    }

    function formatItemSize(kind, size, itemCount) {
        if (kind === "folder") {
            var itemCountValue = itemCount === undefined || itemCount === null ? "" : String(itemCount)
            return itemCountValue !== "" ? itemCountValue + " items" : "—"
        }

        return root.formatSize(size)
    }

    function formatUpdatedAtText(updatedAt) {
        if (updatedAt === undefined || updatedAt === null || updatedAt === "") {
            return "—"
        }

        var formattedValue = Qt.formatDateTime(updatedAt, "yyyy-MM-dd hh:mm")
        return formattedValue !== "" ? formattedValue : String(updatedAt)
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
        pendingMutationAction = "delete"

        if (root.selectedItemKind !== "file") {
            mutationInFlight = false
            deleteErrorMessage = "Folder deletion is not supported in this build."
            return
        }

        mutationInFlight = true
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

    function navigateToFolder(folderId) {
        currentFolderId = folderId ? String(folderId) : "0"
        refreshCurrentFolder()
    }

    function openFolder(folderId) {
        root.navigateToFolder(folderId)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.pagePadding
        spacing: root.panelSpacing

        Rectangle {
            Layout.fillWidth: true
            color: root.panelBackgroundColor
            radius: root.panelRadius
            border.color: root.panelBorderColor
            implicitHeight: toolbarCardLayout.implicitHeight + 24

            ColumnLayout {
                id: toolbarCardLayout
                anchors.fill: parent
                anchors.margins: root.panelInset
                spacing: root.compactSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.compactSpacing

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
                    text: root.uploadErrorMessage
                    color: root.panelErrorTextColor
                    visible: text !== ""
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: root.downloadErrorMessage
                    color: root.panelErrorTextColor
                    visible: text !== ""
                    wrapMode: Text.WordWrap
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            color: root.panelBackgroundColor
            radius: root.panelRadius
            border.color: root.panelBorderColor
            implicitHeight: driveStatusRow.implicitHeight + 32

            RowLayout {
                id: driveStatusRow
                anchors.fill: parent
                anchors.margins: root.pagePadding
                spacing: root.panelSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        text: "DRIVE"
                        color: root.panelMutedTextColor
                        font.pixelSize: 11
                        font.bold: true
                    }

                    Label {
                        text: root.driveTitle
                        color: root.panelStrongTextColor
                        font.pixelSize: 24
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        text: root.driveStatusSummary()
                        color: root.panelMutedTextColor
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    spacing: root.compactSpacing

                    Rectangle {
                        Layout.alignment: Qt.AlignRight
                        implicitWidth: stateChipLabel.implicitWidth + 20
                        implicitHeight: stateChipLabel.implicitHeight + 10
                        radius: implicitHeight / 2
                        color: root.panelAccentFillColor

                        Label {
                            id: stateChipLabel
                            anchors.centerIn: parent
                            text: root.pageStateLabel()
                            color: root.panelAccentTextColor
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: root.folderScopeLabel()
                        color: root.panelMutedTextColor
                        font.pixelSize: 12
                    }
                }
            }
        }

        PageStateView {
            id: stateView
            Layout.fillWidth: true
            Layout.fillHeight: true
            pageState: shellController.pageState

            emptyText: "This folder is empty"
            errorText: "Failed to load folder contents"

            onRetryClicked: root.refreshCurrentFolder()

            RowLayout {
                anchors.fill: parent
                spacing: root.panelSpacing

                Rectangle {
                    Layout.preferredWidth: 248
                    Layout.fillHeight: true
                    color: root.panelBackgroundColor
                    radius: root.panelRadius
                    border.color: root.panelBorderColor
                    clip: true

                    FolderTreePanel {
                        anchors.fill: parent
                        model: driveManager.treeModel
                        currentFolderId: root.currentFolderId
                        onFolderClicked: function(folderId) { root.navigateToFolder(folderId) }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.panelBackgroundColor
                    radius: root.panelRadius
                    border.color: root.panelBorderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: root.contentInset
                        spacing: 12

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            BreadcrumbBar {
                                id: breadcrumbBar
                                objectName: "breadcrumbBar"
                                Layout.fillWidth: true
                                onPathClicked: function(folderId) { root.navigateToFolder(folderId) }
                            }

                            Label {
                                text: root.selectedItemId !== ""
                                      ? "Selected: " + root.selectedItemName
                                      : (root.currentFolderItemCount === 1 ? "1 item" : root.currentFolderItemCount + " items")
                                color: root.panelMutedTextColor
                                font.pixelSize: 12
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: root.panelMutedFillColor
                            radius: root.innerPanelRadius
                            border.color: root.panelBorderColor

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: root.compactSpacing

                                Rectangle {
                                    objectName: "fileTableHeaderSurface"
                                    Layout.fillWidth: true
                                    color: root.pageBackgroundColor
                                    radius: root.innerPanelRadius
                                    border.color: root.panelBorderColor
                                    implicitHeight: fileTableHeaderRow.implicitHeight + 16
                                    clip: true

                                    RowLayout {
                                        id: fileTableHeaderRow
                                        objectName: "fileTableHeaderRow"
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 12
                                        spacing: root.tableColumnSpacing

                                        Label {
                                            objectName: "fileTableHeaderName"
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: "Name"
                                            color: root.tableHeaderTextColor
                                            font.pixelSize: 12
                                            font.bold: true
                                            elide: Text.ElideRight
                                            wrapMode: Text.NoWrap
                                            maximumLineCount: 1
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        Label {
                                            objectName: "fileTableHeaderType"
                                            Layout.preferredWidth: root.fileTypeColumnWidth
                                            Layout.minimumWidth: 0
                                            text: "Type"
                                            color: root.tableHeaderTextColor
                                            font.pixelSize: 12
                                            font.bold: true
                                            elide: Text.ElideRight
                                            wrapMode: Text.NoWrap
                                            maximumLineCount: 1
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        Label {
                                            objectName: "fileTableHeaderSize"
                                            Layout.preferredWidth: root.fileSizeColumnWidth
                                            Layout.minimumWidth: 0
                                            text: "Size"
                                            color: root.tableHeaderTextColor
                                            font.pixelSize: 12
                                            font.bold: true
                                            elide: Text.ElideRight
                                            wrapMode: Text.NoWrap
                                            maximumLineCount: 1
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        Label {
                                            objectName: "fileTableHeaderUpdated"
                                            Layout.preferredWidth: root.fileUpdatedColumnWidth
                                            Layout.minimumWidth: 0
                                            text: "Updated"
                                            color: root.tableHeaderTextColor
                                            font.pixelSize: 12
                                            font.bold: true
                                            elide: Text.ElideRight
                                            wrapMode: Text.NoWrap
                                            maximumLineCount: 1
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        Item {
                                            Layout.preferredWidth: root.fileActionColumnWidth
                                        }
                                    }
                                }

                                ListView {
                                    id: fileListView
                                    objectName: "fileListView"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    model: driveManager.listModel
                                    clip: true
                                    spacing: 1

                                    delegate: ItemDelegate {
                                        id: fileRowDelegate
                                        objectName: "fileRowDelegate_" + String(model.id)
                                        width: ListView.view.width
                                        implicitHeight: root.fileRowHeight
                                        highlighted: root.selectedItemId === String(model.id)
                                        hoverEnabled: true

                                        onClicked: root.selectItem(model.id, model.kind, model.name)

                                        background: Rectangle {
                                            radius: root.innerPanelRadius
                                            color: fileRowDelegate.highlighted
                                                   ? root.panelAccentFillColor
                                                   : (fileRowDelegate.hovered ? root.panelBackgroundColor : "transparent")
                                            border.color: fileRowDelegate.highlighted ? root.panelBorderColor : "transparent"
                                        }

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 12
                                            spacing: root.tableColumnSpacing

                                            Item {
                                                Layout.fillWidth: true
                                                Layout.minimumWidth: 0
                                                implicitHeight: fileNameRow.implicitHeight

                                                RowLayout {
                                                    id: fileNameRow
                                                    anchors.fill: parent
                                                    spacing: 10

                                                    Label {
                                                        text: model.kind === "folder" ? "📁" : "📄"
                                                        font.pixelSize: 18
                                                    }

                                                    Label {
                                                        id: fileNameLabel
                                                        objectName: "fileNameLabel_" + String(model.id)
                                                        Layout.fillWidth: true
                                                        Layout.minimumWidth: 0
                                                        text: model.name
                                                        font.pixelSize: 14
                                                        font.weight: fileRowDelegate.highlighted ? Font.DemiBold : Font.Medium
                                                        color: root.tableBodyPrimaryTextColor
                                                        elide: Text.ElideRight
                                                        wrapMode: Text.NoWrap
                                                        maximumLineCount: 1
                                                        verticalAlignment: Text.AlignVCenter
                                                        ToolTip.visible: truncated && fileRowDelegate.hovered
                                                        ToolTip.delay: 350
                                                        ToolTip.timeout: 5000
                                                        ToolTip.text: text
                                                    }
                                                }
                                            }

                                            Label {
                                                objectName: "fileTypeLabel_" + String(model.id)
                                                Layout.preferredWidth: root.fileTypeColumnWidth
                                                Layout.minimumWidth: 0
                                                text: root.formatItemType(model.kind, model.mimeType)
                                                font.pixelSize: 13
                                                color: root.tableBodySecondaryTextColor
                                                elide: Text.ElideRight
                                                wrapMode: Text.NoWrap
                                                maximumLineCount: 1
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            Label {
                                                objectName: "fileSizeLabel_" + String(model.id)
                                                Layout.preferredWidth: root.fileSizeColumnWidth
                                                Layout.minimumWidth: 0
                                                text: root.formatItemSize(model.kind, model.size, model.itemCount)
                                                font.pixelSize: 13
                                                color: root.tableBodySecondaryTextColor
                                                elide: Text.ElideRight
                                                wrapMode: Text.NoWrap
                                                maximumLineCount: 1
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            Label {
                                                objectName: "fileUpdatedLabel_" + String(model.id)
                                                Layout.preferredWidth: root.fileUpdatedColumnWidth
                                                Layout.minimumWidth: 0
                                                text: root.formatUpdatedAtText(model.updatedAt)
                                                font.pixelSize: 13
                                                color: root.tableBodyTertiaryTextColor
                                                elide: Text.ElideRight
                                                wrapMode: Text.NoWrap
                                                maximumLineCount: 1
                                                verticalAlignment: Text.AlignVCenter
                                            }

                                            Button {
                                                Layout.preferredWidth: root.fileActionColumnWidth
                                                text: "Open"
                                                flat: true
                                                visible: model.kind === "folder"
                                                onClicked: root.navigateToFolder(model.id)
                                            }
                                        }
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
                color: root.panelSecondaryTextColor
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
                color: root.panelErrorTextColor
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
                spacing: root.panelSpacing

            Label {
                Layout.fillWidth: true
                text: "Enter a new name for the selected item."
                color: root.panelSecondaryTextColor
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
                color: root.panelErrorTextColor
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
                spacing: root.panelSpacing

            Label {
                Layout.fillWidth: true
                text: "Delete \"" + root.selectedItemName + "\"? This moves the selected item to trash."
                color: root.panelSecondaryTextColor
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: root.deleteErrorMessage
                color: root.panelErrorTextColor
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
