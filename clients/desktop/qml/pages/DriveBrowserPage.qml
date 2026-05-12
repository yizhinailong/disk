import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform
import "../components"
import "../components/drive"
import "../components/FormatUtils.js" as FormatUtils

Page {
    id: root

    WorkspaceTheme { id: workspaceTheme }

    readonly property int pagePadding: workspaceTheme.pagePadding
    readonly property int compactSpacing: workspaceTheme.compactSpacing
    readonly property int panelSpacing: workspaceTheme.panelSpacing
    readonly property int panelInset: workspaceTheme.panelInset
    readonly property int contentInset: workspaceTheme.contentInset
    readonly property int panelRadius: workspaceTheme.panelRadius
    readonly property int innerPanelRadius: workspaceTheme.innerPanelRadius
    readonly property int fileRowHeight: workspaceTheme.fileRowHeight
    readonly property int tableColumnSpacing: workspaceTheme.tableColumnSpacing
    readonly property color pageBackgroundColor: workspaceTheme.pageBackgroundColor
    readonly property color panelBackgroundColor: workspaceTheme.panelBackgroundColor
    readonly property color panelBorderColor: workspaceTheme.panelBorderColor
    readonly property color panelMutedFillColor: workspaceTheme.panelMutedFillColor
    readonly property color panelMutedTextColor: workspaceTheme.mutedTextColor
    readonly property color panelSecondaryTextColor: workspaceTheme.secondaryTextColor
    readonly property color panelTertiaryTextColor: workspaceTheme.tertiaryTextColor
    readonly property color panelStrongTextColor: workspaceTheme.strongTextColor
    readonly property color panelAccentFillColor: workspaceTheme.accentFillColor
    readonly property color panelAccentTextColor: workspaceTheme.accentTextColor
    readonly property color panelErrorTextColor: workspaceTheme.errorTextColor
    readonly property color panelSuccessTextColor: workspaceTheme.successTextColor
    readonly property color tableHeaderTextColor: workspaceTheme.tableHeaderTextColor
    readonly property color tableBodyPrimaryTextColor: workspaceTheme.tableBodyPrimaryTextColor
    readonly property color tableBodySecondaryTextColor: workspaceTheme.tableBodySecondaryTextColor
    readonly property color tableBodyTertiaryTextColor: workspaceTheme.tableBodyTertiaryTextColor
    readonly property int fileTypeColumnWidth: workspaceTheme.fileTypeColumnWidth
    readonly property int fileSizeColumnWidth: workspaceTheme.fileSizeColumnWidth
    readonly property int fileUpdatedColumnWidth: workspaceTheme.fileUpdatedColumnWidth
    readonly property int fileActionColumnWidth: workspaceTheme.fileActionColumnWidth
    readonly property int currentFolderItemCount: driveManager.listModel ? driveManager.listModel.rowCount() : 0
    readonly property int currentShareItemCount: shareManager.listModel ? shareManager.listModel.rowCount() : 0
    readonly property int currentTrashItemCount: trashManager.listModel ? trashManager.listModel.rowCount() : 0
    readonly property string driveTitle: root.breadcrumbPath.length > 0
                                         && root.breadcrumbPath[root.breadcrumbPath.length - 1].name
                                         ? String(root.breadcrumbPath[root.breadcrumbPath.length - 1].name)
                                         : "My Drive"
    readonly property bool isRootFolder: root.currentFolderId === "0"
    readonly property string resolvedParentFolderId: {
        var breadcrumbPath = root.breadcrumbPath
        if (!breadcrumbPath || breadcrumbPath.length < 2) {
            return ""
        }

        var parentEntry = breadcrumbPath[breadcrumbPath.length - 2]
        if (parentEntry === undefined || parentEntry === null
                || parentEntry.id === undefined || parentEntry.id === null) {
            return ""
        }

        return String(parentEntry.id)
    }
    readonly property bool canNavigateUp: !root.isRootFolder && root.resolvedParentFolderId !== ""

    background: Rectangle {
        color: root.pageBackgroundColor
    }

    property string currentFolderId: "0"
    property string selectedItemId: ""
    property string selectedItemKind: ""
    property string selectedItemName: ""
    property var selectedShareIds: []
    property var selectedTrashIds: []
    property string currentShareId: ""
    property bool mutationInFlight: false
    property string pendingMutationAction: ""
    property bool shareMutationInFlight: false
    property string pendingShareMutationAction: ""
    property string createFolderErrorMessage: ""
    property string renameErrorMessage: ""
    property string deleteErrorMessage: ""
    property string createShareErrorMessage: ""
    property string editShareErrorMessage: ""
    property string uploadErrorMessage: ""
    property string downloadErrorMessage: ""
    property string pendingOwnerDownloadFileId: ""
    property string pendingOwnerDownloadFilename: ""
    property bool folderNavigatorExpanded: false
    property string currentViewMode: "myfiles"
    property var breadcrumbPath: []
    readonly property bool isMyFilesMode: root.currentViewMode === "myfiles"
    readonly property bool isSharedMode: root.currentViewMode === "shared"
    readonly property bool isTrashMode: root.currentViewMode === "trash"
    property var selectedItemIds: []
    property string searchQuery: ""
    property string currentSort: "name_asc"
    property string currentViewLayout: "list"
    property string toastMessage: ""
    property bool toastVisible: false
    readonly property bool isSearchActive: root.searchQuery !== ""
    readonly property bool hasMultiSelection: root.selectedItemIds.length > 1

    function viewModeLabel() {
        switch (root.currentViewMode) {
        case "shared": return "SHARES"
        case "trash": return "TRASH"
        default: return "DRIVE"
        }
    }

    function viewModeTitleText() {
        if (root.isMyFilesMode) {
            return root.driveTitle
        }
        switch (root.currentViewMode) {
        case "shared": return "Shares"
        case "trash": return "Trash"
        default: return "My Drive"
        }
    }

    function viewModeStatusText() {
        if (root.isMyFilesMode) {
            return root.driveStatusSummary()
        }
        if (root.isSharedMode) {
            return root.sharedStatusSummary()
        }
        switch (root.currentViewMode) {
        case "trash": return root.trashStatusSummary()
        default: return ""
        }
    }

    function folderScopeLabel() {
        return root.isRootFolder ? "Root folder" : "Nested folder"
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

    function sharedStatusSummary() {
        if (shellController.pageState === "loading") {
            return "Refreshing your shared links and access controls."
        }
        if (shellController.pageState === "error") {
            return "We could not load your shares. Retry to restore the shared view."
        }
        if (shellController.pageState === "batchResult") {
            return shareManager.batchResultModel.successCount + " of "
                   + shareManager.batchResultModel.totalCount + " share updates succeeded."
        }
        if (shellController.pageState === "empty") {
            return "You have not created any shares yet."
        }
        if (root.selectedShareIds.length > 0) {
            return root.selectedShareIds.length === 1
                   ? "1 share selected"
                   : root.selectedShareIds.length + " shares selected"
        }

        return root.currentShareItemCount === 1
               ? "1 share available"
               : root.currentShareItemCount + " shares available"
    }

    function shareBatchResultTitle() {
        return "Batch Cancel Results"
    }

    function trashStatusSummary() {
        if (shellController.pageState === "loading") {
            return "Refreshing deleted items and trash actions."
        }
        if (shellController.pageState === "error") {
            return "We could not load the trash view. Retry to restore deleted items."
        }
        if (shellController.pageState === "batchResult") {
            var actionLabel = trashManager.batchResultModel.operation === "trash_restore"
                              ? "restore"
                              : "delete"
            return trashManager.batchResultModel.successCount + " of "
                   + trashManager.batchResultModel.totalCount + " trash " + actionLabel + " actions succeeded."
        }
        if (shellController.pageState === "empty") {
            return "Trash is empty."
        }
        if (root.selectedTrashIds.length > 0) {
            return root.selectedTrashIds.length === 1
                   ? "1 trash item selected"
                   : root.selectedTrashIds.length + " trash items selected"
        }

        return root.currentTrashItemCount === 1
               ? "1 item in trash"
               : root.currentTrashItemCount + " items in trash"
    }

    function trashBatchResultTitle() {
        return trashManager.batchResultModel.operation === "trash_restore"
               ? "Restore Results"
               : "Delete Results"
    }

    function shareBatchResultSummaryColor(failureCount) {
        return failureCount > 0 ? root.panelErrorTextColor : root.panelMutedTextColor
    }

    function formatSharePermission(permission) {
        return permission === "view" ? "View only" : "View and download"
    }

    function formatShareStatus(status) {
        switch (String(status || "active")) {
        case "expired":
            return "Expired"
        case "cancelled":
            return "Cancelled"
        default:
            return "Active"
        }
    }

    function shareStatusColor(status) {
        switch (String(status || "active")) {
        case "expired":
            return root.panelErrorTextColor
        case "cancelled":
            return root.panelTertiaryTextColor
        default:
            return root.panelSuccessTextColor
        }
    }

    function formatShareDateTime(value, fallbackText) {
        if (value === undefined || value === null || value === "") {
            return fallbackText
        }

        var formattedValue = Qt.formatDateTime(value, "yyyy-MM-dd hh:mm")
        return formattedValue !== "" ? formattedValue : String(value)
    }

    function isShareSelected(shareId) {
        return root.selectedShareIds.indexOf(String(shareId || "")) >= 0
    }

    function clearSelection() {
        selectedItemId = ""
        selectedItemKind = ""
        selectedItemName = ""
        root.selectedItemIds = []
    }

    function clearSharedSelection() {
        root.selectedShareIds = []
    }

    function clearTrashSelection() {
        root.selectedTrashIds = []
    }

    function selectItem(itemId, itemKind, itemName) {
        selectedItemId = String(itemId)
        selectedItemKind = String(itemKind || "")
        selectedItemName = String(itemName || "")
        root.selectedItemIds = [String(itemId)]
    }

    function isItemSelected(itemId) {
        return root.selectedItemIds.indexOf(String(itemId || "")) >= 0
    }

    function toggleItemSelection(itemId, itemKind, itemName) {
        var id = String(itemId || "")
        var copy = root.selectedItemIds.slice()
        var index = copy.indexOf(id)
        if (index >= 0) {
            copy.splice(index, 1)
            root.selectedItemIds = copy
            if (copy.length > 0) {
                root.selectedItemId = copy[copy.length - 1]
            } else {
                root.selectedItemId = ""
                root.selectedItemKind = ""
                root.selectedItemName = ""
            }
        } else {
            copy.push(id)
            root.selectedItemIds = copy
            root.selectedItemId = id
            root.selectedItemKind = String(itemKind || "")
            root.selectedItemName = String(itemName || "")
        }
    }

    function submitSearch() {
        if (root.searchQuery === "") {
            root.refreshCurrentFolder()
            return
        }
        root.refreshCurrentFolder()
    }

    function clearSearch() {
        root.searchQuery = ""
        root.refreshCurrentFolder()
    }

    function applySort(sortKey) {
        root.currentSort = String(sortKey || "name_asc")
        root.refreshCurrentFolder()
    }

    function toggleViewLayout() {
        root.currentViewLayout = root.currentViewLayout === "list" ? "grid" : "list"
    }

    function showToast(message) {
        root.toastMessage = String(message || "")
        root.toastVisible = true
        toastDismissTimer.restart()
    }

    function hideToast() {
        root.toastVisible = false
        root.toastMessage = ""
    }

    function toggleShareSelection(shareId) {
        var shareIdValue = String(shareId || "")
        var copy = root.selectedShareIds.slice()
        var index = copy.indexOf(shareIdValue)
        if (index >= 0) {
            copy.splice(index, 1)
        } else {
            copy.push(shareIdValue)
        }
        root.selectedShareIds = copy
    }

    function isTrashSelected(trashId) {
        return root.selectedTrashIds.indexOf(String(trashId || "")) >= 0
    }

    function toggleTrashSelection(trashId) {
        var trashIdValue = String(trashId || "")
        var copy = root.selectedTrashIds.slice()
        var index = copy.indexOf(trashIdValue)
        if (index >= 0) {
            copy.splice(index, 1)
        } else {
            copy.push(trashIdValue)
        }
        root.selectedTrashIds = copy
    }

    function clearMutationErrors() {
        createFolderErrorMessage = ""
        renameErrorMessage = ""
        deleteErrorMessage = ""
    }

    function clearShareMutationErrors() {
        createShareErrorMessage = ""
        editShareErrorMessage = ""
    }

    function resetMutationState() {
        mutationInFlight = false
        pendingMutationAction = ""
        clearMutationErrors()
    }

    function resetShareMutationState() {
        shareMutationInFlight = false
        pendingShareMutationAction = ""
        clearShareMutationErrors()
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
        return FormatUtils.formatSize(bytes)
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
            toolbarCard.uploadButton.forceActiveFocus()
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
            toolbarCard.uploadButton.forceActiveFocus()
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
            toolbarCard.downloadButton.forceActiveFocus()
            return false
        }

        var localPath = root.normalizeDownloadPath(targetPath)
        if (localPath === "") {
            downloadErrorMessage = "Please choose one download destination."
            toolbarCard.downloadButton.forceActiveFocus()
            return false
        }

        downloadErrorMessage = ""

        var downloadCountBefore = transferManager.downloadModel.rowCount()
        transferManager.StartDownload(ownerFileId, localPath, "owner")
        if (transferManager.downloadModel.rowCount() === downloadCountBefore) {
            downloadErrorMessage = "Failed to create the download task."
            toolbarCard.downloadButton.forceActiveFocus()
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
        var finishedAction = pendingMutationAction
        pendingMutationAction = ""
        clearMutationErrors()
        newFolderDialog.close()
        renameDialog.close()
        deleteDialog.close()
        if (finishedAction === "create") {
            root.showToast("Folder created successfully")
        } else if (finishedAction === "rename") {
            root.showToast("Item renamed successfully")
        } else if (finishedAction === "delete") {
            root.showToast("Item deleted successfully")
        }
        root.refreshCurrentFolder()
    }

    function openCreateShareDialog() {
        root.resetShareMutationState()
        createSharePasswordField.text = ""
        createShareExpireSpin.value = 7
        createShareDialog.open()
    }

    function openEditShareDialog(shareId, permission) {
        root.resetShareMutationState()
        root.currentShareId = String(shareId || "")
        editSharePermissionCombo.currentIndex = permission === "view" ? 1 : 0
        editSharePasswordField.text = ""
        editShareDialog.open()
    }

    function submitCreateShare() {
        root.clearShareMutationErrors()
        root.shareMutationInFlight = true
        root.pendingShareMutationAction = "create"
        shareManager.createShare(
            createShareDialog.selectedFileIds,
            createSharePermissionCombo.currentText,
            createSharePasswordField.text,
            createShareExpireSpin.value
        )
    }

    function submitUpdateShare() {
        if (root.currentShareId === "") {
            return
        }

        root.clearShareMutationErrors()
        root.shareMutationInFlight = true
        root.pendingShareMutationAction = "edit"
        shareManager.updateShare(
            root.currentShareId,
            editSharePermissionCombo.currentText,
            editSharePasswordField.text
        )
    }

    function submitCancelSelectedShares() {
        if (root.selectedShareIds.length === 0) {
            return
        }

        root.clearShareMutationErrors()
        root.shareMutationInFlight = true
        root.pendingShareMutationAction = "cancel"
        shareManager.cancelShares(root.selectedShareIds)
        root.clearSharedSelection()
    }

    function submitCancelShare(shareId) {
        var shareIdValue = String(shareId || "")
        if (shareIdValue === "") {
            return
        }

        root.clearShareMutationErrors()
        root.shareMutationInFlight = true
        root.pendingShareMutationAction = "cancel"
        shareManager.cancelShares([shareIdValue])
        root.clearSharedSelection()
    }

    function applyShareMutationError(message) {
        var errorMessage = message || "Request failed. Please try again."
        root.shareMutationInFlight = false

        if (root.pendingShareMutationAction === "create") {
            root.createShareErrorMessage = errorMessage
            createSharePasswordField.forceActiveFocus()
            return
        }
        if (root.pendingShareMutationAction === "edit") {
            root.editShareErrorMessage = errorMessage
            editSharePasswordField.forceActiveFocus()
            return
        }

        root.pendingShareMutationAction = ""
        shellController.setPageState("error")
    }

    function finishShareMutationSuccess() {
        root.shareMutationInFlight = false
        var finishedAction = root.pendingShareMutationAction
        root.pendingShareMutationAction = ""
        root.clearShareMutationErrors()
        if (finishedAction === "create") {
            createShareDialog.close()
        } else if (finishedAction === "edit") {
            editShareDialog.close()
        }

        if (shellController.pageState !== "batchResult") {
            root.refreshSharedList()
        }
    }

    function refreshCurrentView() {
        if (root.isMyFilesMode) {
            root.refreshCurrentFolder()
            return
        }
        if (root.isSharedMode) {
            root.refreshSharedList()
            return
        }
        if (root.isTrashMode) {
            root.refreshTrashList()
        }
    }

    function refreshCurrentFolder() {
        clearSelection()
        shellController.setPageState("loading")
        driveManager.loadFolderTree()
        driveManager.loadBreadcrumb(currentFolderId)
        if (root.isSearchActive) {
            driveManager.searchFiles(root.searchQuery)
        } else {
            driveManager.listFiles(currentFolderId, 1, 50, root.currentSort)
        }
    }

    function refreshSharedList() {
        root.clearSelection()
        root.clearSharedSelection()
        root.clearTrashSelection()
        shellController.setPageState("loading")
        shareManager.listShares()
    }

    function refreshTrashList() {
        root.clearSelection()
        root.clearSharedSelection()
        root.clearTrashSelection()
        shellController.setPageState("loading")
        trashManager.listTrash()
    }

    function navigateToFolder(folderId) {
        var nextFolderId = folderId ? String(folderId) : "0"
        if (nextFolderId === "0") {
            folderNavigatorExpanded = false
        }
        currentFolderId = nextFolderId
        root.searchQuery = ""
        refreshCurrentFolder()
    }

    function activateViewMode(mode) {
        var nextMode = String(mode || "myfiles")
        if (root.currentViewMode === nextMode) {
            return
        }
        root.currentViewMode = nextMode
        root.clearSelection()
        root.clearSharedSelection()
        root.clearTrashSelection()
        root.folderNavigatorExpanded = false
        root.searchQuery = ""
        if (nextMode === "myfiles") {
            root.currentFolderId = "0"
            root.refreshCurrentFolder()
            return
        }
        if (nextMode === "shared") {
            root.refreshSharedList()
            return
        }
        if (nextMode === "trash") {
            root.refreshTrashList()
            return
        }
    }

    function submitRestoreSelectedTrash() {
        if (root.selectedTrashIds.length === 0) {
            return
        }
        trashManager.restoreItems(root.selectedTrashIds)
        root.clearTrashSelection()
    }

    function submitDeleteSelectedTrash() {
        if (root.selectedTrashIds.length === 0) {
            return
        }
        trashManager.deleteItems(root.selectedTrashIds)
        root.clearTrashSelection()
    }

    function submitRestoreTrash(trashId) {
        var trashIdValue = String(trashId || "")
        if (trashIdValue === "") {
            return
        }
        trashManager.restoreItems([trashIdValue])
    }

    function submitDeleteTrash(trashId) {
        var trashIdValue = String(trashId || "")
        if (trashIdValue === "") {
            return
        }
        trashManager.deleteItems([trashIdValue])
    }

    function submitClearTrash() {
        root.clearTrashSelection()
        trashManager.clearAll()
    }

    function openFolder(folderId) {
        root.navigateToFolder(folderId)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.pagePadding
        spacing: root.panelSpacing

        DriveToolbarCard {
            id: toolbarCard
            Layout.fillWidth: true
            page: root
        }

        DriveStatusCard {
            Layout.fillWidth: true
            page: root
        }

        DriveMyFilesView {
            id: myFilesView
            Layout.fillWidth: true
            Layout.fillHeight: true
            page: root
            breadcrumbPath: root.breadcrumbPath
        }

        DriveSharedView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            page: root
        }

        DriveTrashView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            page: root
        }

    }

    FolderTreePanel {
        visible: false
        width: 0
        height: 0
        model: driveManager.treeModel
        currentFolderId: root.currentFolderId
        onFolderClicked: function(folderId) { root.navigateToFolder(folderId) }
    }

    Popup {
        id: folderNavigatorOverlay
        objectName: "folderNavigatorPanel"
        x: root.pagePadding
        y: root.pagePadding
        width: 248
        height: Math.max(0, root.height - (root.pagePadding * 2))
        visible: root.folderNavigatorExpanded && root.isMyFilesMode
        modal: false
        padding: 0
        closePolicy: Popup.NoAutoClose

        background: Rectangle {
            color: root.panelBackgroundColor
            radius: root.panelRadius
            border.color: root.panelBorderColor
        }

        FolderTreePanel {
            anchors.fill: parent
            model: driveManager.treeModel
            currentFolderId: root.currentFolderId
            onFolderClicked: function(folderId) { root.navigateToFolder(folderId) }
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

    Dialog {
        id: createShareDialog
        modal: true
        width: 360
        title: "Create Share"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        property var selectedFileIds: []

        onClosed: {
            if (!root.shareMutationInFlight) {
                root.resetShareMutationState()
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: root.panelSpacing

            Label {
                Layout.fillWidth: true
                text: createShareDialog.selectedFileIds.length > 0
                      ? (createShareDialog.selectedFileIds.length === 1
                             ? "1 file id is queued for this share."
                             : createShareDialog.selectedFileIds.length + " file ids are queued for this share.")
                      : "No file ids are currently queued for this share."
                color: root.panelSecondaryTextColor
                wrapMode: Text.WordWrap
            }

            Label {
                text: "Permission:"
                color: root.panelStrongTextColor
            }

            ComboBox {
                id: createSharePermissionCombo
                Layout.fillWidth: true
                enabled: !root.shareMutationInFlight
                model: ["download", "view"]
            }

            Label {
                text: "Password (optional, 4-8 chars):"
                color: root.panelStrongTextColor
            }

            TextField {
                id: createSharePasswordField
                Layout.fillWidth: true
                enabled: !root.shareMutationInFlight
                placeholderText: "No password"
                echoMode: TextInput.Password
                maximumLength: 8
                onAccepted: root.submitCreateShare()
            }

            Label {
                text: "Expire (days, 0 = permanent):"
                color: root.panelStrongTextColor
            }

            SpinBox {
                id: createShareExpireSpin
                Layout.fillWidth: true
                enabled: !root.shareMutationInFlight
                from: 0
                to: 365
                value: 7
            }

            Label {
                Layout.fillWidth: true
                text: root.createShareErrorMessage
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
                    enabled: !root.shareMutationInFlight
                    onClicked: createShareDialog.close()
                }

                Button {
                    text: root.pendingShareMutationAction === "create" && root.shareMutationInFlight
                          ? "Creating..." : "Create"
                    highlighted: true
                    enabled: !root.shareMutationInFlight
                    onClicked: root.submitCreateShare()
                }
            }
        }
    }

    Dialog {
        id: editShareDialog
        modal: true
        width: 360
        title: "Edit Share"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        onClosed: {
            if (!root.shareMutationInFlight) {
                root.resetShareMutationState()
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: root.panelSpacing

            Label {
                Layout.fillWidth: true
                text: "Update the permission or password for the selected share."
                color: root.panelSecondaryTextColor
                wrapMode: Text.WordWrap
            }

            Label {
                text: "Permission:"
                color: root.panelStrongTextColor
            }

            ComboBox {
                id: editSharePermissionCombo
                Layout.fillWidth: true
                enabled: !root.shareMutationInFlight
                model: ["download", "view"]
            }

            Label {
                text: "New Password (empty to remove):"
                color: root.panelStrongTextColor
            }

            TextField {
                id: editSharePasswordField
                Layout.fillWidth: true
                enabled: !root.shareMutationInFlight
                placeholderText: "Leave empty to keep current"
                echoMode: TextInput.Password
                maximumLength: 8
                onAccepted: root.submitUpdateShare()
            }

            Label {
                Layout.fillWidth: true
                text: root.editShareErrorMessage
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
                    enabled: !root.shareMutationInFlight
                    onClicked: editShareDialog.close()
                }

                Button {
                    text: root.pendingShareMutationAction === "edit" && root.shareMutationInFlight
                          ? "Saving..." : "Save"
                    highlighted: true
                    enabled: !root.shareMutationInFlight
                    onClicked: root.submitUpdateShare()
                }
            }
        }
    }

    Connections {
        target: trashManager

        function onApiError(message, code) {
            if (root.isTrashMode) {
                shellController.setPageState("error")
            }
        }

        function onOperationSuccess(message) {
            if (root.isTrashMode && shellController.pageState !== "batchResult") {
                root.refreshTrashList()
            }
        }

        function onPaginationLoaded(page, totalPages, total) {
            if (root.isTrashMode) {
                shellController.setPageState(total > 0 ? "content" : "empty")
            }
        }

        function onBatchResultReady() {
            root.clearTrashSelection()
            if (root.isTrashMode) {
                shellController.setPageState("batchResult")
            }
        }

        function onClearAllCompleted(deletedCount, freedSpace) {
            root.clearTrashSelection()
        }
    }

    Connections {
        target: shareManager

        function onApiError(message, code) {
            if (root.pendingShareMutationAction !== "") {
                root.applyShareMutationError(message)
                return
            }
            if (root.isSharedMode) {
                shellController.setPageState("error")
            }
        }

        function onOperationSuccess(message) {
            if (root.pendingShareMutationAction !== "") {
                root.finishShareMutationSuccess()
            }
        }

        function onPaginationLoaded(page, totalPages, total) {
            if (root.isSharedMode) {
                shellController.setPageState(total > 0 ? "content" : "empty")
            }
        }

        function onBatchResultReady() {
            root.shareMutationInFlight = false
            root.pendingShareMutationAction = ""
            root.clearShareMutationErrors()
            if (root.isSharedMode) {
                shellController.setPageState("batchResult")
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
            if (root.isMyFilesMode) {
                root.breadcrumbPath = breadcrumb
            }
        }

        function onPaginationLoaded(page, totalPages, total) {
            if (root.isMyFilesMode) {
                shellController.setPageState(total > 0 ? "content" : "empty")
            }
        }

        function onListLoadFailed(message, code) {
            if (root.isMyFilesMode) {
                shellController.setPageState("error")
            }
        }
    }
    
    Rectangle {
        id: toastOverlay
        objectName: "driveToast"
        visible: root.toastVisible && root.toastMessage !== ""
        color: "#323232"
        radius: 8
        implicitWidth: toastLabel.implicitWidth + 32
        implicitHeight: toastLabel.implicitHeight + 16
        z: 100
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter

        Label {
            id: toastLabel
            anchors.centerIn: parent
            text: root.toastMessage
            color: "#ffffff"
            font.pixelSize: 13
        }
    }

    Timer {
        id: toastDismissTimer
        interval: 3000
        onTriggered: root.hideToast()
    }

    Component.onCompleted: {
        root.refreshCurrentView()
    }
}
