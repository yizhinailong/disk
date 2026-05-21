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
                                         : "我的网盘"
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
    property string createdShareLink: ""
    property string createdShareId: ""
    property string createDialogState: "form"
    property string createFolderErrorMessage: ""
    property string renameErrorMessage: ""
    property string deleteErrorMessage: ""
    property string moveErrorMessage: ""
    property string copyErrorMessage: ""
    property string targetMoveFolderId: "0"
    property string targetMoveFolderName: "我的网盘"
    property string targetCopyFolderId: "0"
    property string targetCopyFolderName: "我的网盘"
    property string createShareErrorMessage: ""
    property string editShareErrorMessage: ""
    property string uploadErrorMessage: ""
    property string downloadErrorMessage: ""
    property string visitorEntryError: ""
    property string visitorShareInputText: ""
    property string pendingOwnerDownloadFileId: ""
    property string pendingOwnerDownloadFilename: ""
    property var pendingOwnerBatchDownloadFiles: []
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
    property bool keepMyFilesContentWhileLoading: false
    property bool keepSharedContentWhileLoading: false
    property bool keepTrashContentWhileLoading: false
    property bool preserveMyFilesContentOnNextRefresh: false
    property bool preserveSharedContentOnNextRefresh: false
    property bool preserveTrashContentOnNextRefresh: false
    readonly property bool isSearchActive: root.searchQuery !== ""
    readonly property bool hasMultiSelection: root.selectedItemIds.length > 1

    function viewModeLabel() {
        switch (root.currentViewMode) {
        case "shared": return "分享"
        case "trash": return "回收站"
        default: return "网盘"
        }
    }

    function viewModeTitleText() {
        if (root.isMyFilesMode) {
            return root.driveTitle
        }
        switch (root.currentViewMode) {
        case "shared": return "分享"
        case "trash": return "回收站"
        default: return "我的网盘"
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
        return root.isRootFolder ? "根文件夹" : "子文件夹"
    }

    function pageStateLabel() {
        switch (shellController.pageState) {
        case "loading":
            return "加载中"
        case "empty":
            return "空"
        case "error":
            return "需要重试"
        default:
            return "就绪"
        }
    }

    function driveStatusSummary() {
        if (shellController.pageState === "loading") {
            return "正在刷新文件夹内容和网盘导航"
        }
        if (shellController.pageState === "error") {
            return "无法加载此文件夹。请重试以恢复网盘视图。"
        }
        if (shellController.pageState === "empty") {
            return "此文件夹为空。使用工具栏添加新内容。"
        }
        if (selectedItemId !== "") {
            return '已选择："' + selectedItemName + '"'
        }

        return currentFolderItemCount === 1
               ? "此文件夹中有 1 项"
               : "此文件夹中有 " + currentFolderItemCount + " 项"
    }

    function sharedStatusSummary() {
        if (shellController.pageState === "loading") {
            return "正在刷新您的分享链接和访问控制"
        }
        if (shellController.pageState === "error") {
            return "无法加载分享。请重试以恢复分享视图。"
        }
        if (shellController.pageState === "batchResult") {
            return "成功更新 " + shareManager.batchResultModel.successCount + " / "
                   + shareManager.batchResultModel.totalCount + " 个分享"
        }
        if (shellController.pageState === "empty") {
            return "您还没有创建任何分享"
        }
        if (root.selectedShareIds.length > 0) {
            return root.selectedShareIds.length === 1
                   ? "已选择 1 个分享"
                   : "已选择 " + root.selectedShareIds.length + " 个分享"
        }

        return root.currentShareItemCount === 1
               ? "1 个可用分享"
               : root.currentShareItemCount + " 个可用分享"
    }

    function shareBatchResultTitle() {
        return "批量取消结果"
    }

    function trashStatusSummary() {
        if (shellController.pageState === "loading") {
            return "正在刷新已删除项目和回收站操作"
        }
        if (shellController.pageState === "error") {
            return "无法加载回收站视图。请重试以恢复删除的项目。"
        }
        if (shellController.pageState === "batchResult") {
            var actionLabel = trashManager.batchResultModel.operation === "trash_restore"
                              ? "恢复"
                              : "删除"
            return "成功" + actionLabel + " " + trashManager.batchResultModel.successCount + " / "
                   + trashManager.batchResultModel.totalCount + " 个回收站项目"
        }
        if (shellController.pageState === "empty") {
            return "回收站为空"
        }
        if (root.selectedTrashIds.length > 0) {
            return root.selectedTrashIds.length === 1
                   ? "已选择 1 个回收站项目"
                   : "已选择 " + root.selectedTrashIds.length + " 个回收站项目"
        }

        return root.currentTrashItemCount === 1
               ? "回收站中有 1 个项目"
               : "回收站中有 " + root.currentTrashItemCount + " 个项目"
    }

    function trashBatchResultTitle() {
        return trashManager.batchResultModel.operation === "trash_restore"
               ? "恢复结果"
               : "删除结果"
    }

    function shareBatchResultSummaryColor(failureCount) {
        return failureCount > 0 ? root.panelErrorTextColor : root.panelMutedTextColor
    }

    function formatSharePermission(permission) {
        return permission === "view" ? "仅查看" : "查看和下载"
    }

    function formatShareStatus(status) {
        switch (String(status || "active")) {
        case "expired":
            return "已过期"
        case "cancelled":
            return "已取消"
        default:
            return "有效"
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

    function hasCachedMyFilesContent() {
        return driveManager.listModel && driveManager.listModel.rowCount() > 0
    }

    function hasCachedSharedContent() {
        return shareManager.listModel && shareManager.listModel.rowCount() > 0
    }

    function hasCachedTrashContent() {
        return trashManager.listModel && trashManager.listModel.rowCount() > 0
    }

    function preserveMyFilesContentForNextRefresh() {
        root.preserveMyFilesContentOnNextRefresh = true
    }

    function preserveSharedContentForNextRefresh() {
        root.preserveSharedContentOnNextRefresh = true
    }

    function preserveTrashContentForNextRefresh() {
        root.preserveTrashContentOnNextRefresh = true
    }

    function submitSearch() {
        root.preserveMyFilesContentForNextRefresh()
        root.refreshCurrentFolder()
    }

    function clearSearch() {
        root.searchQuery = ""
        root.preserveMyFilesContentForNextRefresh()
        root.refreshCurrentFolder()
    }

    function applySort(sortKey) {
        root.currentSort = String(sortKey || "name_asc")
        root.preserveMyFilesContentForNextRefresh()
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

    function selectedDownloadFiles() {
        var files = []
        if (!driveManager.listModel) {
            return files
        }

        for (var index = 0; index < root.selectedItemIds.length; ++index) {
            var item = driveManager.listModel.GetItemById(String(root.selectedItemIds[index] || ""))
            if (item && String(item.kind || "") === "file") {
                files.push({
                    id: String(item.id || ""),
                    name: String(item.name || "")
                })
            }
        }

        return files
    }

    function selectedDownloadFileCount() {
        return root.selectedDownloadFiles().length
    }

    function selectedDeleteIds() {
        var fileIds = []
        var folderIds = []
        for (var index = 0; index < root.selectedItemIds.length; ++index) {
            var id = String(root.selectedItemIds[index] || "")
            if (id === "") {
                continue
            }

            var kind = ""
            if (driveManager.listModel) {
                var item = driveManager.listModel.GetItemById(id)
                if (item) {
                    kind = String(item.kind || "")
                }
            }
            if (kind === "" && id === root.selectedItemId) {
                kind = String(root.selectedItemKind || "")
            }

            if (kind === "folder") {
                folderIds.push(id)
            } else {
                fileIds.push(id)
            }
        }
        return {
            fileIds: fileIds,
            folderIds: folderIds
        }
    }

    function deleteDialogMessage() {
        if (root.selectedItemKind === "folder") {
            return "确定删除\"" + root.selectedItemName + "\"？该文件夹及其内容将移至回收站。"
        }
        return "确定删除\"" + root.selectedItemName + "\"？所选项目将移至回收站。"
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
        moveErrorMessage = ""
        copyErrorMessage = ""
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
                error: "名称必须为 1-255 个字符"
            }
        }
        if (value === "." || value === "..") {
            return {
                valid: false,
                value: value,
                error: '名称不能为 "." 或 ".."'
            }
        }
        if (value.charAt(0) === ".") {
            return {
                valid: false,
                value: value,
                error: '名称不能以 "." 开头'
            }
        }

        var forbiddenChars = "/\\\\:*?\"<>|"
        for (var index = 0; index < value.length; ++index) {
            var code = value.charCodeAt(index)
            var character = value.charAt(index)
            if (code < 0x20 || (code >= 0x7f && code <= 0x9f)) {
                return {
                    valid: false,
                    value: value,
                    error: "名称必须是合法 UTF-8 且不能包含控制字符"
                }
            }
            if (forbiddenChars.indexOf(character) >= 0) {
                return {
                    valid: false,
                    value: value,
                    error: '名称不能包含以下字符：/ \\ : * ? " < > |'
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
            return "文件夹"
        }

        var typeLabel = mimeType === undefined || mimeType === null ? "" : String(mimeType)
        return typeLabel !== "" ? typeLabel : "文件"
    }

    function formatItemSize(kind, size, itemCount) {
        if (kind === "folder") {
            var itemCountValue = itemCount === undefined || itemCount === null ? "" : String(itemCount)
            return itemCountValue !== "" ? itemCountValue + " 项" : "—"
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
            uploadErrorMessage = "请选择一个本地文件"
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
            uploadErrorMessage = "请选择一个存在的本地文件"
            toolbarCard.uploadButton.forceActiveFocus()
            return false
        }

        return true
    }

    function openOwnerDownloadFileChooser(fileId, filename) {
        var selectedFiles = root.selectedDownloadFiles()
        if (root.selectedItemIds.length > 1) {
            if (selectedFiles.length === 0) {
                downloadErrorMessage = "请选择文件下载"
                toolbarCard.downloadButton.forceActiveFocus()
                return
            }
            root.openOwnerBatchDownloadFolderChooser(selectedFiles)
            return
        }

        if (selectedFiles.length === 1) {
            fileId = selectedFiles[0].id
            filename = selectedFiles[0].name
        }

        pendingOwnerDownloadFileId = String(fileId || "")
        pendingOwnerDownloadFilename = String(filename || "")
        downloadErrorMessage = ""
        downloadFileDialogLoader.active = true
        if (downloadFileDialogLoader.item) {
            downloadFileDialogLoader.item.open()
        }
    }

    function openOwnerBatchDownloadFolderChooser(files) {
        pendingOwnerBatchDownloadFiles = files || []
        pendingOwnerDownloadFileId = ""
        pendingOwnerDownloadFilename = ""
        downloadErrorMessage = ""
        downloadFolderDialogLoader.active = true
        if (downloadFolderDialogLoader.item) {
            downloadFolderDialogLoader.item.open()
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

    function downloadBasename(filename) {
        var value = String(filename || "")
        value = value.replace(/\\/g, "/")
        var slashIndex = value.lastIndexOf("/")
        if (slashIndex >= 0) {
            value = value.slice(slashIndex + 1)
        }
        return value === "" ? "download" : value
    }

    function uniqueDownloadFilename(filename, usedNames) {
        var baseName = root.downloadBasename(filename)
        var lowerBaseName = baseName.toLowerCase()
        if (!usedNames[lowerBaseName]) {
            usedNames[lowerBaseName] = true
            return baseName
        }

        var dotIndex = baseName.lastIndexOf(".")
        var stem = dotIndex > 0 ? baseName.slice(0, dotIndex) : baseName
        var extension = dotIndex > 0 ? baseName.slice(dotIndex) : ""
        var suffix = 1
        while (true) {
            var candidate = stem + " (" + suffix + ")" + extension
            var lowerCandidate = candidate.toLowerCase()
            if (!usedNames[lowerCandidate]) {
                usedNames[lowerCandidate] = true
                return candidate
            }
            ++suffix
        }
    }

    function joinDownloadPath(directoryPath, filename) {
        var directory = root.normalizeDownloadPath(directoryPath)
        var name = root.downloadBasename(filename)
        if (directory === "" || name === "") {
            return ""
        }

        var lastCharacter = directory.charAt(directory.length - 1)
        if (lastCharacter === "/" || lastCharacter === "\\") {
            return directory + name
        }

        return directory + "/" + name
    }

    function startOwnerDownloadToPath(fileId, filename, targetPath) {
        var ownerFileId = Number(fileId)
        if (!isFinite(ownerFileId) || ownerFileId <= 0) {
            downloadErrorMessage = "请选择一个文件下载"
            toolbarCard.downloadButton.forceActiveFocus()
            return false
        }

        var localPath = root.normalizeDownloadPath(targetPath)
        if (localPath === "") {
            downloadErrorMessage = "请选择下载保存位置"
            toolbarCard.downloadButton.forceActiveFocus()
            return false
        }

        downloadErrorMessage = ""

        var downloadCountBefore = transferManager.downloadModel.rowCount()
        transferManager.StartDownload(ownerFileId, localPath, "owner")
        if (transferManager.downloadModel.rowCount() === downloadCountBefore) {
            downloadErrorMessage = "创建下载任务失败"
            toolbarCard.downloadButton.forceActiveFocus()
            return false
        }

        return true
    }

    function startOwnerBatchDownloadToDirectory(directoryPath) {
        var localDirectory = root.normalizeDownloadPath(directoryPath)
        if (localDirectory === "") {
            downloadErrorMessage = "请选择下载保存目录"
            toolbarCard.downloadButton.forceActiveFocus()
            return false
        }

        var files = root.pendingOwnerBatchDownloadFiles || []
        if (files.length === 0) {
            downloadErrorMessage = "请选择文件下载"
            toolbarCard.downloadButton.forceActiveFocus()
            return false
        }

        downloadErrorMessage = ""

        var createdCount = 0
        var usedNames = ({})
        for (var index = 0; index < files.length; ++index) {
            var file = files[index]
            var ownerFileId = Number(file.id)
            var targetPath = root.joinDownloadPath(
                localDirectory,
                root.uniqueDownloadFilename(file.name, usedNames)
            )
            if (!isFinite(ownerFileId) || ownerFileId <= 0 || targetPath === "") {
                continue
            }

            var downloadCountBefore = transferManager.downloadModel.rowCount()
            transferManager.StartDownload(ownerFileId, targetPath, "owner")
            if (transferManager.downloadModel.rowCount() > downloadCountBefore) {
                ++createdCount
            }
        }

        if (createdCount === 0) {
            downloadErrorMessage = "创建下载任务失败"
            toolbarCard.downloadButton.forceActiveFocus()
            return false
        }

        root.showToast("已创建 " + createdCount + " 个下载任务")
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

    function openMoveDialog() {
        if (selectedItemIds.length === 0 || !root.isMyFilesMode) {
            return
        }

        resetMutationState()
        targetMoveFolderId = "0"
        targetMoveFolderName = "我的网盘"
        driveManager.loadFolderTree()
        moveDialog.open()
    }

    function openCopyDialog() {
        if (selectedItemIds.length === 0 || !root.isMyFilesMode) {
            return
        }

        resetMutationState()
        targetCopyFolderId = "0"
        targetCopyFolderName = "我的网盘"
        driveManager.loadFolderTree()
        copyDialog.open()
    }

    function openFileDetailDialog(fileId) {
        var fileIdValue = String(fileId || "")
        if (fileIdValue === "") {
            return
        }
        driveManager.getFileDetail(fileIdValue)
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
        driveManager.renameDriveItem(root.selectedItemId, root.selectedItemKind, validationResult.value)
    }

    function submitMoveItems() {
        if (selectedItemIds.length === 0) {
            return
        }

        var ids = root.selectedDeleteIds()
        if (ids.fileIds.length === 0 && ids.folderIds.length === 0) {
            moveErrorMessage = "请选择要移动的项目"
            return
        }

        clearMutationErrors()
        pendingMutationAction = "move"
        mutationInFlight = true
        try {
            driveManager.moveItems(ids.fileIds, ids.folderIds, root.targetMoveFolderId)
        } catch (error) {
            mutationInFlight = false
            moveErrorMessage = "移动请求发送失败：" + error
        }
    }

    function submitCopyItems() {
        if (selectedItemIds.length === 0) {
            return
        }

        var ids = root.selectedDeleteIds()
        if (ids.fileIds.length === 0 && ids.folderIds.length === 0) {
            copyErrorMessage = "请选择要复制的项目"
            return
        }

        clearMutationErrors()
        pendingMutationAction = "copy"
        mutationInFlight = true
        try {
            driveManager.copyDriveItems(ids.fileIds, ids.folderIds, root.targetCopyFolderId)
        } catch (error) {
            mutationInFlight = false
            copyErrorMessage = "复制请求发送失败：" + error
        }
    }

    function submitDeleteItem() {
        if (selectedItemId === "") {
            return
        }

        var ids = root.selectedDeleteIds()
        if (ids.fileIds.length === 0 && ids.folderIds.length === 0) {
            deleteErrorMessage = "请选择要删除的项目"
            return
        }

        clearMutationErrors()
        pendingMutationAction = "delete"

        mutationInFlight = true
        try {
            console.log("[DriveBrowserPage] deleteDriveItems", JSON.stringify(ids.fileIds), JSON.stringify(ids.folderIds))
            driveManager.deleteDriveItems(ids.fileIds, ids.folderIds)
        } catch (error) {
            mutationInFlight = false
            deleteErrorMessage = "删除请求发送失败：" + error
        }
    }

    function applyMutationError(message) {
        var errorMessage = message || "请求失败，请重试"
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
            return
        }
        if (pendingMutationAction === "move") {
            moveErrorMessage = errorMessage
            return
        }
        if (pendingMutationAction === "copy") {
            copyErrorMessage = errorMessage
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
        moveDialog.close()
        copyDialog.close()
        if (finishedAction === "create") {
            root.showToast("文件夹创建成功")
        } else if (finishedAction === "rename") {
            root.showToast("项目重命名成功")
        } else if (finishedAction === "delete") {
            root.showToast("项目删除成功")
        } else if (finishedAction === "move") {
            root.showToast("项目移动成功")
        } else if (finishedAction === "copy") {
            root.showToast("项目复制成功")
        }
        root.preserveMyFilesContentForNextRefresh()
        root.refreshCurrentFolder()
    }

    function openCreateShareDialog() {
        root.resetShareMutationState()
        createSharePasswordField.text = ""
        createShareExpireSpin.value = 7
        if (root.isMyFilesMode) {
            var ids = root.selectedDeleteIds()
            createShareDialog.selectedFileIds = ids.fileIds
            createShareDialog.selectedFolderIds = ids.folderIds
        }
        createShareDialog.open()
    }

    function openEditShareDialog(shareId, permission) {
        root.resetShareMutationState()
        root.currentShareId = String(shareId || "")
        editSharePermissionCombo.currentIndex = permission === "view" ? 1 : 0
        editSharePasswordField.text = ""
        editShareDialog.open()
    }

    function openShareDetailDialog(shareId) {
        var shareIdValue = String(shareId || "")
        if (shareIdValue === "") {
            return
        }
        shareManager.getShareDetail(shareIdValue)
    }

    function submitCreateShare() {
        root.clearShareMutationErrors()
        root.shareMutationInFlight = true
        root.pendingShareMutationAction = "create"
        shareManager.createShare(
            createShareDialog.selectedFileIds,
            createShareDialog.selectedFolderIds,
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
        var errorMessage = message || "请求失败，请重试"
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
        if (finishedAction === "create" && root.createDialogState === "success") {
            // Dialog stays open in success state; user closes via "完成" button
        } else if (finishedAction === "create") {
            createShareDialog.close()
        } else if (finishedAction === "edit") {
            editShareDialog.close()
        }

        if (shellController.pageState !== "batchResult") {
            root.preserveSharedContentForNextRefresh()
            root.refreshSharedList()
        }
    }

    function refreshCurrentView() {
        if (root.isMyFilesMode) {
            root.preserveMyFilesContentForNextRefresh()
            root.refreshCurrentFolder()
            return
        }
        if (root.isSharedMode) {
            root.preserveSharedContentForNextRefresh()
            root.refreshSharedList()
            return
        }
        if (root.isTrashMode) {
            root.preserveTrashContentForNextRefresh()
            root.refreshTrashList()
        }
    }

    function openVisitorShare() {
        var shareId = shareManager.parseShareInput(root.visitorShareInputText.trim())
        if (shareId.length > 0) {
            root.visitorEntryError = ""
            shellController.navigateToVisitor(shareId)
        } else {
            root.visitorEntryError = "无效的分享码或链接"
        }
    }

    function refreshCurrentFolder() {
        root.keepMyFilesContentWhileLoading = root.preserveMyFilesContentOnNextRefresh && root.hasCachedMyFilesContent()
        root.preserveMyFilesContentOnNextRefresh = false
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
        root.keepSharedContentWhileLoading = root.preserveSharedContentOnNextRefresh && root.hasCachedSharedContent()
        root.preserveSharedContentOnNextRefresh = false
        root.clearSelection()
        root.clearSharedSelection()
        root.clearTrashSelection()
        shellController.setPageState("loading")
        shareManager.listShares(1, 20, "active")
    }

    function refreshTrashList() {
        root.keepTrashContentWhileLoading = root.preserveTrashContentOnNextRefresh && root.hasCachedTrashContent()
        root.preserveTrashContentOnNextRefresh = false
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

    function reenterViewMode(mode) {
        var nextMode = String(mode || "myfiles")
        if (root.currentViewMode !== nextMode) {
            root.activateViewMode(nextMode)
            return
        }
        if (nextMode === "myfiles") {
            root.preserveMyFilesContentForNextRefresh()
            root.refreshCurrentFolder()
            return
        }
        if (nextMode === "shared") {
            root.preserveSharedContentForNextRefresh()
            root.refreshSharedList()
            return
        }
        if (nextMode === "trash") {
            root.preserveTrashContentForNextRefresh()
            root.refreshTrashList()
        }
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
            root.preserveMyFilesContentForNextRefresh()
            root.refreshCurrentFolder()
            return
        }
        if (nextMode === "shared") {
            root.preserveSharedContentForNextRefresh()
            root.refreshSharedList()
            return
        }
        if (nextMode === "trash") {
            root.preserveTrashContentForNextRefresh()
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

    RowLayout {
        anchors.fill: parent
        anchors.margins: root.pagePadding
        spacing: root.panelSpacing

        Rectangle {
            objectName: "folderNavigatorPanel"
            visible: root.folderNavigatorExpanded && root.isMyFilesMode
            Layout.preferredWidth: 248
            Layout.fillHeight: true
            color: root.panelBackgroundColor
            radius: root.panelRadius
            border.color: root.panelBorderColor

            FolderTreePanel {
                anchors.fill: parent
                model: driveManager.treeModel
                currentFolderId: root.currentFolderId
                onFolderClicked: function(folderId) { root.navigateToFolder(folderId) }
                onCloseRequested: root.folderNavigatorExpanded = false
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
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

    Loader {
        id: downloadFolderDialogLoader
        active: false
        sourceComponent: downloadFolderDialogComponent
    }

    Component {
        id: uploadFileDialogComponent

        Platform.FileDialog {
            title: "选择要上传的文件"
            fileMode: Platform.FileDialog.OpenFile
            nameFilters: ["All files (*)"]

            onAccepted: root.startUploadFromPath(currentFile)
            onRejected: root.startUploadFromPath("")
        }
    }

    Component {
        id: downloadFileDialogComponent

        Platform.FileDialog {
            title: root.pendingOwnerDownloadFilename === ""
                   ? "选择下载保存位置"
                   : "保存 " + root.pendingOwnerDownloadFilename
            fileMode: Platform.FileDialog.SaveFile
            currentFile: root.pendingOwnerDownloadFilename
            nameFilters: ["All files (*)"]

            onAccepted: root.startOwnerDownloadToPath(root.pendingOwnerDownloadFileId, root.pendingOwnerDownloadFilename, file)
            onRejected: root.startOwnerDownloadToPath(root.pendingOwnerDownloadFileId, root.pendingOwnerDownloadFilename, "")
        }
    }

    Component {
        id: downloadFolderDialogComponent

        Platform.FolderDialog {
            title: "选择下载保存目录"

            onAccepted: root.startOwnerBatchDownloadToDirectory(folder)
            onRejected: root.startOwnerBatchDownloadToDirectory("")
        }
    }

    Dialog {
        id: newFolderDialog
        modal: true
        width: 360
        title: "新建文件夹"
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
                text: "请输入新文件夹的名称"
                color: root.panelSecondaryTextColor
                wrapMode: Text.WordWrap
            }

            TextField {
                id: newFolderNameField
                Layout.fillWidth: true
                maximumLength: 255
                enabled: !root.mutationInFlight
                placeholderText: "文件夹名称"
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
                    text: "取消"
                    enabled: !root.mutationInFlight
                    onClicked: newFolderDialog.close()
                }

                Button {
                    text: root.pendingMutationAction === "create" && root.mutationInFlight
                          ? "创建中..." : "创建"
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
        title: "重命名"
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
                text: "请输入所选项目的新名称"
                color: root.panelSecondaryTextColor
                wrapMode: Text.WordWrap
            }

            TextField {
                id: renameNameField
                Layout.fillWidth: true
                maximumLength: 255
                enabled: !root.mutationInFlight
                placeholderText: "项目名称"
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
                    text: "取消"
                    enabled: !root.mutationInFlight
                    onClicked: renameDialog.close()
                }

                Button {
                    text: root.pendingMutationAction === "rename" && root.mutationInFlight
                          ? "保存中..." : "保存"
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
        title: "删除"
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
                text: root.deleteDialogMessage()
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
                    text: "取消"
                    enabled: !root.mutationInFlight
                    onClicked: deleteDialog.close()
                }

                Button {
                    text: root.pendingMutationAction === "delete" && root.mutationInFlight
                          ? "删除中..." : "删除"
                    highlighted: true
                    enabled: !root.mutationInFlight
                    onClicked: root.submitDeleteItem()
                }
            }
        }
    }

    Dialog {
        id: moveDialog
        objectName: "moveDialog"
        modal: true
        width: 420
        height: 520
        title: "移动到"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        onClosed: {
            if (!root.mutationInFlight) {
                root.resetMutationState()
            }
        }

        ColumnLayout {
            width: parent.width
            height: parent.height
            spacing: root.panelSpacing

            Label {
                Layout.fillWidth: true
                text: "选择目标文件夹。当前目标：" + root.targetMoveFolderName
                color: root.panelSecondaryTextColor
                wrapMode: Text.WordWrap
            }

            Button {
                Layout.fillWidth: true
                text: "我的网盘（根目录）"
                enabled: !root.mutationInFlight
                onClicked: {
                    root.targetMoveFolderId = "0"
                    root.targetMoveFolderName = "我的网盘"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.panelMutedFillColor
                radius: root.innerPanelRadius
                border.color: root.panelBorderColor

                FolderTreePanel {
                    anchors.fill: parent
                    model: driveManager.treeModel
                    currentFolderId: root.targetMoveFolderId
                    onFolderClicked: function(folderId) {
                        root.targetMoveFolderId = folderId
                        root.targetMoveFolderName = "文件夹 " + folderId
                    }
                    onCloseRequested: moveDialog.close()
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.moveErrorMessage
                color: root.panelErrorTextColor
                visible: text !== ""
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    enabled: !root.mutationInFlight
                    onClicked: moveDialog.close()
                }

                Button {
                    text: root.pendingMutationAction === "move" && root.mutationInFlight
                          ? "移动中..." : "移动"
                    highlighted: true
                    enabled: !root.mutationInFlight
                    onClicked: root.submitMoveItems()
                }
            }
        }
    }

    Dialog {
        id: copyDialog
        objectName: "copyDialog"
        modal: true
        width: 420
        height: 520
        title: "复制到"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        onClosed: {
            if (!root.mutationInFlight) {
                root.resetMutationState()
            }
        }

        ColumnLayout {
            width: parent.width
            height: parent.height
            spacing: root.panelSpacing

            Label {
                Layout.fillWidth: true
                text: "选择复制目标文件夹。当前目标：" + root.targetCopyFolderName
                color: root.panelSecondaryTextColor
                wrapMode: Text.WordWrap
            }

            Button {
                Layout.fillWidth: true
                text: "我的网盘（根目录）"
                enabled: !root.mutationInFlight
                onClicked: {
                    root.targetCopyFolderId = "0"
                    root.targetCopyFolderName = "我的网盘"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.panelMutedFillColor
                radius: root.innerPanelRadius
                border.color: root.panelBorderColor

                FolderTreePanel {
                    anchors.fill: parent
                    model: driveManager.treeModel
                    currentFolderId: root.targetCopyFolderId
                    onFolderClicked: function(folderId) {
                        root.targetCopyFolderId = folderId
                        root.targetCopyFolderName = "文件夹 " + folderId
                    }
                    onCloseRequested: copyDialog.close()
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.copyErrorMessage
                color: root.panelErrorTextColor
                visible: text !== ""
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消"
                    enabled: !root.mutationInFlight
                    onClicked: copyDialog.close()
                }

                Button {
                    text: root.pendingMutationAction === "copy" && root.mutationInFlight
                          ? "复制中..." : "复制"
                    highlighted: true
                    enabled: !root.mutationInFlight
                    onClicked: root.submitCopyItems()
                }
            }
        }
    }

    Dialog {
        id: createShareDialog
        objectName: "createShareDialog"
        modal: true
        width: 360
        title: root.createDialogState === "success" ? "分享创建成功" : "创建分享"
        standardButtons: Dialog.NoButton
        closePolicy: Popup.NoAutoClose

        property var selectedFileIds: []
        property var selectedFolderIds: []

        onClosed: {
            root.createdShareLink = ""
            root.createdShareId = ""
            root.createDialogState = "form"
            if (!root.shareMutationInFlight) {
                root.resetShareMutationState()
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: root.panelSpacing

            ColumnLayout {
                id: shareFormContainer
                objectName: "shareFormContainer"
                Layout.fillWidth: true
                spacing: root.panelSpacing
                visible: root.createDialogState === "form"

                Label {
                    Layout.fillWidth: true
                    text: {
                        var fileCount = createShareDialog.selectedFileIds.length
                        var folderCount = createShareDialog.selectedFolderIds.length
                        var totalCount = fileCount + folderCount
                        if (totalCount === 0) {
                            return "当前没有项目加入分享队列"
                        }
                        return totalCount === 1
                               ? "1 个项目已加入分享队列"
                               : totalCount + " 个项目已加入分享队列"
                    }
                    color: root.panelSecondaryTextColor
                    wrapMode: Text.WordWrap
                }

                Label {
                    text: "权限："
                    color: root.panelStrongTextColor
                }

                ComboBox {
                    id: createSharePermissionCombo
                    Layout.fillWidth: true
                    enabled: !root.shareMutationInFlight
                    model: ["download", "view"]
                }

                Label {
                    text: "密码（可选，4-8个字符）："
                    color: root.panelStrongTextColor
                }

                TextField {
                    id: createSharePasswordField
                    Layout.fillWidth: true
                    enabled: !root.shareMutationInFlight
                    placeholderText: "无密码"
                    echoMode: TextInput.Password
                    maximumLength: 8
                    onAccepted: root.submitCreateShare()
                }

                Label {
                    text: "过期天数（0=永久）："
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
                        text: "取消"
                        enabled: !root.shareMutationInFlight
                        onClicked: createShareDialog.close()
                    }

                    Button {
                        text: root.pendingShareMutationAction === "create" && root.shareMutationInFlight
                              ? "创建中..." : "创建"
                        highlighted: true
                        enabled: !root.shareMutationInFlight
                        onClicked: root.submitCreateShare()
                    }
                }
            }

            ColumnLayout {
                id: shareSuccessContainer
                objectName: "shareSuccessContainer"
                Layout.fillWidth: true
                spacing: root.panelSpacing
                visible: root.createDialogState === "success"

                Label {
                    Layout.fillWidth: true
                    text: "分享已成功创建"
                    color: root.panelSuccessTextColor
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Label {
                    text: "分享码："
                    color: root.panelStrongTextColor
                }

                TextEdit {
                    id: shareSuccessCodeLabel
                    objectName: "shareSuccessCodeLabel"
                    Layout.fillWidth: true
                    text: root.createdShareId
                    color: root.tableBodyPrimaryTextColor
                    font.pixelSize: 16
                    font.bold: true
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                }

                Label {
                    text: "分享链接："
                    color: root.panelStrongTextColor
                }

                TextEdit {
                    id: shareSuccessLinkLabel
                    objectName: "shareSuccessLinkLabel"
                    Layout.fillWidth: true
                    text: root.createdShareLink
                    color: root.panelAccentTextColor
                    font.pixelSize: 14
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        id: shareSuccessCopyCodeButton
                        objectName: "shareSuccessCopyCodeButton"
                        text: codeCopyFeedback ? "✓ 已复制" : "复制分享码"
                        onClicked: {
                            if (root.createdShareId !== "") {
                                Clipboard.setText(root.createdShareId)
                                codeCopyFeedback = true
                                codeCopyTimer.start()
                            }
                        }

                        property bool codeCopyFeedback: false

                        Timer {
                            id: codeCopyTimer
                            interval: 2000
                            onTriggered: shareSuccessCopyCodeButton.codeCopyFeedback = false
                        }
                    }

                    Button {
                        id: shareSuccessCopyButton
                        objectName: "shareSuccessCopyButton"
                        text: successCopyFeedback ? "✓ 已复制" : "复制链接"
                        onClicked: {
                            if (root.createdShareLink !== "") {
                                Clipboard.setText(root.createdShareLink)
                                successCopyFeedback = true
                                successCopyTimer.start()
                            }
                        }

                        property bool successCopyFeedback: false

                        Timer {
                            id: successCopyTimer
                            interval: 2000
                            onTriggered: shareSuccessCopyButton.successCopyFeedback = false
                        }
                    }

                    Button {
                        id: shareSuccessDoneButton
                        objectName: "shareSuccessDoneButton"
                        text: "完成"
                        highlighted: true
                        onClicked: {
                            root.createdShareLink = ""
                            root.createdShareId = ""
                            root.createDialogState = "form"
                            createShareDialog.close()
                            if (shellController.pageState !== "batchResult") {
                                root.preserveSharedContentForNextRefresh()
        root.refreshSharedList()
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: editShareDialog
        modal: true
        width: 360
        title: "编辑分享"
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
                text: "请更新所选分享的权限或密码"
                color: root.panelSecondaryTextColor
                wrapMode: Text.WordWrap
            }

            Label {
                text: "权限："
                color: root.panelStrongTextColor
            }

            ComboBox {
                id: editSharePermissionCombo
                Layout.fillWidth: true
                enabled: !root.shareMutationInFlight
                model: ["download", "view"]
            }

            Label {
                text: "新密码（留空移除）："
                color: root.panelStrongTextColor
            }

            TextField {
                id: editSharePasswordField
                Layout.fillWidth: true
                enabled: !root.shareMutationInFlight
                placeholderText: "留空保持当前密码"
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
                    text: "取消"
                    enabled: !root.shareMutationInFlight
                    onClicked: editShareDialog.close()
                }

                Button {
                    text: root.pendingShareMutationAction === "edit" && root.shareMutationInFlight
                          ? "保存中..." : "保存"
                    highlighted: true
                    enabled: !root.shareMutationInFlight
                    onClicked: root.submitUpdateShare()
                }
            }
        }
    }

    FileDetailDialog {
        id: fileDetailDialog
        detail: ({})
        formatSize: root.formatSize
        formatType: root.formatItemType
        formatDateTime: root.formatUpdatedAtText
    }

    OwnerShareDetailDialog {
        id: ownerShareDetailDialog
        detail: ({})
        formatSize: root.formatSize
        formatPermission: root.formatSharePermission
        formatStatus: root.formatShareStatus
        formatDateTime: root.formatShareDateTime
    }

    Connections {
        target: trashManager

        function onApiError(message, code) {
            root.keepTrashContentWhileLoading = false
            if (root.isTrashMode) {
                shellController.setPageState("error")
            }
        }

        function onOperationSuccess(message) {
            if (root.isTrashMode && shellController.pageState !== "batchResult") {
                root.preserveTrashContentForNextRefresh()
        root.refreshTrashList()
            }
        }

        function onPaginationLoaded(page, totalPages, total) {
            root.keepTrashContentWhileLoading = false
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
            root.keepSharedContentWhileLoading = false
            if (root.isSharedMode) {
                shellController.setPageState("error")
            }
        }

        function onShareCreated(shareId, shareLink) {
            root.createdShareLink = String(shareLink || "")
            root.createdShareId = String(shareId || "")
            root.createDialogState = "success"
        }

        function onOperationSuccess(message) {
            if (root.pendingShareMutationAction !== "") {
                root.finishShareMutationSuccess()
            }
        }

        function onPaginationLoaded(page, totalPages, total) {
            root.keepSharedContentWhileLoading = false
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

        function onShareDetailLoaded(detail) {
            ownerShareDetailDialog.detail = detail
            ownerShareDetailDialog.open()
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
            if (root.pendingMutationAction === "create" || root.pendingMutationAction === "rename" || root.pendingMutationAction === "delete" || root.pendingMutationAction === "move" || root.pendingMutationAction === "copy") {
                root.finishMutationSuccess()
            }
        }

        function onBreadcrumbLoaded(breadcrumb) {
            if (root.isMyFilesMode) {
                root.breadcrumbPath = breadcrumb
            }
        }

        function onPaginationLoaded(page, totalPages, total) {
            root.keepMyFilesContentWhileLoading = false
            if (root.isMyFilesMode) {
                shellController.setPageState(total > 0 ? "content" : "empty")
            }
        }

        function onListLoadFailed(message, code) {
            root.keepMyFilesContentWhileLoading = false
            if (root.isMyFilesMode) {
                shellController.setPageState("error")
            }
        }

        function onFileDetailLoaded(detail) {
            fileDetailDialog.detail = detail
            fileDetailDialog.open()
        }
    }

    Connections {
        target: transferManager

        function onTaskError(task_id, message) {
            root.showToast(message)
        }

        function onUploadCompleted(task_id, filename, parent_id) {
            root.showToast(filename === "" ? "文件上传成功" : "文件上传成功：" + filename)
            if (root.isMyFilesMode && String(parent_id) === root.currentFolderId) {
                root.preserveMyFilesContentForNextRefresh()
                root.refreshCurrentFolder()
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
