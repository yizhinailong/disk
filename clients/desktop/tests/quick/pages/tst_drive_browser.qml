import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopDriveBrowser"
    id: testDriveBrowser

    function readQmlSource(relPath) {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/" + relPath), false)
        xhr.send()
        verify(xhr.responseText.length > 0, relPath + " was read")
        return xhr.responseText
    }

    function readDriveBrowserSource() {
        return readQmlSource("pages/DriveBrowserPage.qml")
    }

    function readDriveCompositeSource() {
        return [
            readQmlSource("pages/DriveBrowserPage.qml"),
            readQmlSource("components/drive/DriveToolbarCard.qml"),
            readQmlSource("components/drive/DriveStatusCard.qml"),
            readQmlSource("components/drive/DriveMyFilesView.qml"),
            readQmlSource("components/drive/DriveSharedView.qml"),
            readQmlSource("components/drive/DriveTrashView.qml")
        ].join("\n")
    }

    function test_drive_browser_page_has_foundation_selection_state() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('property string currentFolderId: "0"') !== -1,
               "Has currentFolderId property initialized to root")
        verify(source.indexOf('property string selectedItemId: ""') !== -1,
               "Has explicit selectedItemId state")
        verify(source.indexOf('property string selectedItemKind: ""') !== -1,
               "Has explicit selectedItemKind state")
        verify(source.indexOf('property string selectedItemName: ""') !== -1,
               "Has explicit selectedItemName state")
        verify(source.indexOf('function clearSelection()') !== -1,
               "Has helper to reset single selection")
        verify(source.indexOf('function selectItem(itemId, itemKind, itemName)') !== -1,
               "Has helper to update single selection")
    }

    function test_drive_browser_uses_page_state_view() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("PageStateView") !== -1, "Uses PageStateView for state management")
        verify(source.indexOf("pageState: shellController.pageState") !== -1,
               "Binds to shellController.pageState")
    }

    function test_drive_browser_exposes_only_p0_actions() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('text: "刷新"') !== -1, "Exposes Refresh action")
        verify(source.indexOf('text: "新建文件夹"') !== -1, "Exposes New Folder action")
        verify(source.indexOf('text: "上传"') !== -1, "Exposes Upload action")
        verify(source.indexOf('text: "重命名"') !== -1, "Exposes Rename action")
        verify(source.indexOf('text: "删除"') !== -1, "Exposes Delete action")
        verify(source.indexOf('text: "下载"') !== -1, "Exposes Download action")
        verify(source.indexOf('enabled: page.selectedItemId !== ""') !== -1
               || source.indexOf('enabled: root.selectedItemId !== ""') !== -1,
               "Selection-gates rename and delete")
        verify(source.indexOf('enabled: page.selectedItemKind === "file"') !== -1
               || source.indexOf('enabled: root.selectedItemKind === "file"') !== -1,
               "Selection-gates download to files")

        verify(source.indexOf('placeholderText: "搜索文件..."') !== -1
               || source.indexOf('搜索文件...') !== -1,
               "Search field is present in My Files view")
        verify(source.indexOf('searchFiles(') !== -1, "Search behavior is present")
        verify(source.indexOf('objectName: "moveButton"') !== -1, "Move action is present")
        verify(source.indexOf('text: "移动"') !== -1, "Move action is localized")
        verify(source.indexOf('objectName: "copyButton"') !== -1, "Copy action is present")
        verify(source.indexOf('text: "Share"') === -1, "Share action is absent")
        verify(source.indexOf('text: "Batch"') === -1, "Batch action is absent")
    }

    function test_drive_browser_has_single_file_upload_entry_and_helper() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("function openUploadFileChooser()") !== -1,
               "Has page helper to open the native single-file chooser")
        verify(source.indexOf("function normalizeUploadPath(path)") !== -1,
               "Normalizes file URLs into one local path string")
        verify(source.indexOf("function startUploadFromPath(path)") !== -1,
               "Has testable page-level upload helper")
        verify(source.indexOf("import Qt.labs.platform as Platform") !== -1,
               "Uses the native platform dialog module")
        verify(source.indexOf("Platform.FileDialog {") !== -1,
               "Uses a native file chooser")
        verify(source.indexOf("id: uploadFileDialogLoader") !== -1,
               "Lazily loads the native upload file chooser")
        verify(source.indexOf("id: uploadFileDialogComponent") !== -1,
               "Defines the chooser as a dedicated component")
        verify(source.indexOf("fileMode: Platform.FileDialog.OpenFile") !== -1,
               "Restricts the chooser to one file")
        verify(source.indexOf("selectedFiles") === -1,
               "Does not enable multi-file upload selection")
        verify(source.indexOf("Platform.FolderDialog {") === -1,
               "Does not introduce folder upload")
        verify(source.indexOf("id: uploadDialog") === -1,
               "Removes the manual upload path dialog")
        verify(source.indexOf("id: uploadPathField") === -1,
               "Removes the manual upload path text field")
        verify(source.indexOf('placeholderText: "Absolute local file path"') === -1,
               "Does not request a free-text local path")
        verify(source.indexOf("onClicked: page.openUploadFileChooser()") !== -1
               || source.indexOf("onClicked: root.openUploadFileChooser()") !== -1,
               "Upload toolbar action opens the native chooser")
        verify(source.indexOf("uploadFileDialogLoader.active = true") !== -1,
               "Chooser is created only when the upload action is used")
        verify(source.indexOf("uploadFileDialogLoader.item.open()") !== -1,
               "Upload action opens the native chooser instance")
        verify(source.indexOf("onAccepted: root.startUploadFromPath(file)") !== -1,
               "Chooser acceptance hands the selected path to the page helper")
        verify(source.indexOf("onRejected: root.startUploadFromPath(\"\")") !== -1,
               "Chooser rejection routes empty selection through the same helper")
        verify(source.indexOf("transferManager.StartUpload(localPath, parentFolderId)") !== -1,
               "Valid upload paths call TransferManager.StartUpload")
        verify(source.indexOf("var uploadCountBefore = transferManager.uploadModel.rowCount()") !== -1,
               "Helper checks the tracked upload count before enqueuing")
        verify(source.indexOf("if (transferManager.uploadModel.rowCount() === uploadCountBefore)") !== -1,
               "Invalid upload paths do not create a tracked upload task")
        verify(source.indexOf('uploadErrorMessage = "请选择一个本地文件"') !== -1,
               "Empty upload input gets deterministic feedback")
        verify(source.indexOf('uploadErrorMessage = "请选择一个存在的本地文件"') !== -1,
               "Invalid upload path gets deterministic feedback")
        verify(source.indexOf('property string uploadErrorMessage: ""') !== -1,
               "Upload error feedback is kept in page state")
    }

    function test_drive_browser_has_file_only_owner_download_entry_and_helper() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('property string downloadErrorMessage: ""') !== -1,
               "Tracks download chooser validation feedback in page state")
        verify(source.indexOf('property string pendingOwnerDownloadFileId: ""') !== -1,
               "Stores the pending owner download file id for the chooser callback")
        verify(source.indexOf('property string pendingOwnerDownloadFilename: ""') !== -1,
               "Stores the pending owner download filename for the chooser callback")
        verify(source.indexOf("function openOwnerDownloadFileChooser(fileId, filename)") !== -1,
               "Has a helper to open the save-file chooser for owner downloads")
        verify(source.indexOf("function normalizeDownloadPath(path)") !== -1,
               "Normalizes save-file chooser URLs into local paths")
        verify(source.indexOf("function startOwnerDownloadToPath(fileId, filename, targetPath)") !== -1,
               "Has a testable owner download helper")
        verify(source.indexOf("id: downloadFileDialogLoader") !== -1,
               "Lazily loads the owner download save-file chooser")
        verify(source.indexOf("id: downloadFileDialogComponent") !== -1,
               "Defines the owner download chooser as a dedicated component")
        verify(source.indexOf("fileMode: Platform.FileDialog.SaveFile") !== -1,
               "Uses a save-file chooser for owner downloads")
        verify(source.indexOf("onClicked: page.openOwnerDownloadFileChooser(page.selectedItemId, page.selectedItemName)") !== -1
               || source.indexOf("onClicked: root.openOwnerDownloadFileChooser(root.selectedItemId, root.selectedItemName)") !== -1,
               "Download toolbar action opens the owner save-file chooser")
        verify(source.indexOf("onAccepted: root.startOwnerDownloadToPath(root.pendingOwnerDownloadFileId, root.pendingOwnerDownloadFilename, file)") !== -1,
               "Chooser acceptance routes through the owner download helper")
        verify(source.indexOf("onRejected: root.startOwnerDownloadToPath(root.pendingOwnerDownloadFileId, root.pendingOwnerDownloadFilename, \"\")") !== -1,
               "Chooser rejection routes through the same owner download helper")
        verify(source.indexOf('downloadErrorMessage = "请选择一个文件下载"') !== -1,
               "Invalid selection gets deterministic owner download feedback")
        verify(source.indexOf('downloadErrorMessage = "请选择下载保存位置"') !== -1,
               "Empty download destination gets deterministic feedback")
        verify(source.indexOf("transferManager.StartDownload(ownerFileId, localPath, \"owner\")") !== -1,
               "Valid owner download requests call TransferManager.StartDownload with the owner domain")
        verify(source.indexOf("transferManager.StartVisitorDownload(") === -1,
               "Does not add visitor/share-token download flow")
        verify(source.indexOf("Platform.FolderDialog {") === -1,
               "Does not introduce folder download")
    }

    function test_drive_browser_has_folder_tree_and_breadcrumb_navigation() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("FolderTreePanel") !== -1, "Has FolderTreePanel")
        verify(source.indexOf("model: driveManager.treeModel") !== -1,
               "FolderTreePanel uses driveManager.treeModel")
        verify(source.indexOf("BreadcrumbBar") !== -1, "Has BreadcrumbBar")
        verify(source.indexOf("onFolderClicked: function(folderId) { root.navigateToFolder(folderId) }") !== -1,
               "Folder tree clicks navigate through page flow")
        verify(source.indexOf("onPathClicked: function(folderId) { page.navigateToFolder(folderId) }") !== -1
               || source.indexOf("onPathClicked: function(folderId) { root.navigateToFolder(folderId) }") !== -1,
               "Breadcrumb clicks navigate through page flow")
        verify(source.indexOf("function navigateToFolder(folderId)") !== -1,
               "Has navigateToFolder helper")
        verify(source.indexOf("refreshCurrentFolder()") !== -1,
               "Folder navigation reuses refresh flow")
    }

    function test_drive_browser_row_click_is_selection_only() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("model: driveManager.listModel") !== -1,
               "ListView uses driveManager.listModel")
        verify(source.indexOf("onClicked: page.selectItem(model.id, model.kind, model.name)") !== -1
               || source.indexOf("onClicked: root.selectItem(model.id, model.kind, model.name)") !== -1,
               "Row click only updates selection")
        verify(source.indexOf('text: "打开"') !== -1,
               "Folder rows expose dedicated Open control")
        verify(source.indexOf('visible: model.kind === "folder"') !== -1,
               "Open control is only shown for folder rows")
        verify(source.indexOf("page.navigateToFolder(model.id)") !== -1
               || source.indexOf("root.navigateToFolder(model.id)") !== -1,
               "Folder Open button navigates through the shared flow")
        verify(source.indexOf("driveManager.getFileDetail(") === -1,
               "Row click no longer triggers file detail loading")
    }

    function test_drive_browser_file_list_uses_static_table_header_and_role_fallbacks() {
        var source = readDriveCompositeSource()
        var nameHeaderIndex = source.indexOf('text: "名称"')
        var typeHeaderIndex = source.indexOf('text: "类型"')
        var sizeHeaderIndex = source.indexOf('text: "大小"')
        var updatedHeaderIndex = source.indexOf('text: "更新日期"')

        verify(nameHeaderIndex !== -1, "Has static Name header")
        verify(typeHeaderIndex !== -1, "Has static Type header")
        verify(sizeHeaderIndex !== -1, "Has static Size header")
        verify(updatedHeaderIndex !== -1, "Has static Updated header")
        verify(nameHeaderIndex < typeHeaderIndex && typeHeaderIndex < sizeHeaderIndex && sizeHeaderIndex < updatedHeaderIndex,
               "Header columns stay ordered as Name, Type, Size, Updated")
        verify(source.indexOf("function formatItemType(kind, mimeType)") !== -1,
               "Uses a dedicated helper for the Type column")
        verify(source.indexOf('return typeLabel !== "" ? typeLabel : "文件"') !== -1,
               "Files fall back to File when mimeType is missing")
        verify(source.indexOf("function formatItemSize(kind, size, itemCount)") !== -1,
               "Uses a dedicated helper for the Size column")
        verify(source.indexOf('return itemCountValue !== "" ? itemCountValue + " 项" : "—"') !== -1,
               "Folders render item counts or an em dash when unavailable")
        verify(source.indexOf("return root.formatSize(size)") !== -1,
               "Files render size via the existing formatSize helper")
        verify(source.indexOf("function formatUpdatedAtText(updatedAt)") !== -1,
               "Uses a dedicated helper for the Updated column")
        verify(source.indexOf('if (updatedAt === undefined || updatedAt === null || updatedAt === "")') !== -1,
               "Missing updatedAt values fall back to an em dash")
        verify(source.indexOf('objectName: "fileTableHeaderRow"') !== -1,
               "Exposes a stable header-row hook for runtime checks")
        verify(source.indexOf('objectName: "fileTableHeaderName"') !== -1,
               "Name header keeps a stable hook")
        verify(source.indexOf('objectName: "fileTableHeaderType"') !== -1,
               "Type header keeps a stable hook")
        verify(source.indexOf('objectName: "fileTableHeaderSize"') !== -1,
               "Size header keeps a stable hook")
        verify(source.indexOf('objectName: "fileTableHeaderUpdated"') !== -1,
               "Updated header keeps a stable hook")
        verify(source.indexOf('spacing: page.tableColumnSpacing') !== -1
               || source.indexOf('spacing: root.tableColumnSpacing') !== -1,
               "Header and rows share one explicit column-spacing contract")
        verify(source.indexOf('wrapMode: Text.NoWrap') !== -1,
               "Header labels and row metadata stay single-line for alignment")
        verify(source.indexOf("TableView") === -1,
               "Keeps ListView instead of introducing TableView")
    }

    function test_drive_browser_file_list_long_names_use_deterministic_elision_and_disclosure() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "fileListView"') !== -1,
               "Exposes a stable file list hook")
        verify(source.indexOf('objectName: "fileRowDelegate_" + String(model.id)') !== -1,
               "Each file row exposes a deterministic delegate hook")
        verify(source.indexOf('objectName: "fileNameLabel_" + String(model.id)') !== -1,
               "Each file name label exposes a deterministic hook")
        verify(source.indexOf('Layout.minimumWidth: 0') !== -1,
               "The flexible name column explicitly allows shrinking without collisions")
        verify(source.indexOf('elide: Text.ElideRight') !== -1,
               "Long file names use right-side elision")
        verify(source.indexOf('wrapMode: Text.NoWrap') !== -1,
               "Long file names stay on one line")
        verify(source.indexOf('maximumLineCount: 1') !== -1,
               "Long file names are constrained to one line")
        verify(source.indexOf('ToolTip.visible: truncated && fileRowDelegate.hovered') !== -1,
               "Hover disclosure appears only when the file name is actually truncated")
        verify(source.indexOf('ToolTip.text: text') !== -1,
               "The disclosure reveals the full file name text")
    }

    function test_drive_browser_refreshes_on_complete_and_handles_pagination() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("Component.onCompleted") !== -1,
               "Refreshes on component completion")
        verify(source.indexOf("root.refreshCurrentView()") !== -1,
               "Calls refreshCurrentView on init")
        verify(source.indexOf("onPaginationLoaded") !== -1,
               "Handles paginationLoaded signal")
        verify(source.indexOf("onListLoadFailed") !== -1,
               "Handles listLoadFailed signal")
    }

    function test_drive_browser_uses_one_shared_name_validator_for_create_and_rename() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("function validateDriveItemName(name)") !== -1,
               "Has one shared drive item name validator")
        verify(source.indexOf("root.validateDriveItemName(newFolderNameField.text)") !== -1,
               "Create flow uses shared validator")
        verify(source.indexOf("root.validateDriveItemName(renameNameField.text)") !== -1,
               "Rename flow uses shared validator")
        verify(source.indexOf('error: "名称必须为 1-255 个字符"') !== -1,
               "Validates backend length contract")
        verify(source.indexOf("名称不能为 \".\" 或 \"..\"") !== -1,
               "Rejects reserved names")
        verify(source.indexOf("名称不能以 \".\" 开头") !== -1,
               "Rejects hidden-dot names")
        verify(source.indexOf("名称必须是合法 UTF-8 且不能包含控制字符") !== -1,
               "Rejects control characters while allowing UTF-8 names")
        verify(source.indexOf("code > 0x7e") === -1,
               "Does not reject non-ASCII UTF-8 names")
        verify(source.indexOf("var forbiddenChars = ") !== -1,
               "Defines the forbidden character set")
        verify(source.indexOf("名称不能包含以下字符：") !== -1,
               "Rejects forbidden filesystem characters")
        verify(source.indexOf("function validateNewFolderName(") === -1,
               "Does not introduce a create-only validator")
        verify(source.indexOf("function validateRenameName(") === -1,
               "Does not introduce a rename-only validator")
    }

    function test_drive_browser_wires_single_item_mutation_dialogs_and_refresh_flow() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("Dialog {") !== -1, "Uses explicit dialogs for mutations")
        verify(source.indexOf("id: newFolderDialog") !== -1,
               "Has new folder dialog")
        verify(source.indexOf("id: renameDialog") !== -1,
               "Has rename dialog")
        verify(source.indexOf("id: deleteDialog") !== -1,
               "Has delete dialog")

        verify(source.indexOf("driveManager.createFolder(root.currentFolderId, validationResult.value)") !== -1,
               "Valid create flow calls DriveManager.createFolder")
        verify(source.indexOf("driveManager.renameDriveItem(root.selectedItemId, root.selectedItemKind, validationResult.value)") !== -1,
               "Valid rename flow calls DriveManager.renameDriveItem")
        verify(source.indexOf("function selectedDeleteIds()") !== -1,
               "Delete flow splits selected files and folders")
        verify(source.indexOf("function openMoveDialog()") !== -1,
               "Has move dialog opener")
        verify(source.indexOf("function submitMoveItems()") !== -1,
               "Has move submit helper")
        verify(source.indexOf("driveManager.moveItems(ids.fileIds, ids.folderIds, root.targetMoveFolderId)") !== -1,
               "Move submit calls moveItems with split ids and target folder")
        verify(source.indexOf('pendingMutationAction = "move"') !== -1,
               "Move submit records the move mutation action")
        verify(source.indexOf('property string moveErrorMessage: ""') !== -1,
               "Tracks move API errors in page state")
        verify(source.indexOf('property string targetMoveFolderId: "0"') !== -1,
               "Tracks selected move target folder")
        verify(source.indexOf('id: moveDialog') !== -1,
               "Has move dialog")
        verify(source.indexOf('objectName: "moveDialog"') !== -1,
               "Move dialog exposes a stable object name")
        verify(source.indexOf("driveManager.deleteDriveItems(ids.fileIds, ids.folderIds)") !== -1,
               "Delete submit calls deleteDriveItems with split ids")
        verify(source.indexOf("driveManager.deleteItems([root.selectedItemId])") === -1,
               "Delete submit no longer sends every selected id as a file")
        verify(source.indexOf('kind === "folder"') !== -1,
               "Folder selections route to folder ids")
        verify(source.indexOf("folderIds.push(id)") !== -1,
               "Folder ids are collected separately")
        verify(source.indexOf("fileIds.push(id)") !== -1,
               "File ids are collected separately")
        verify(source.indexOf("该文件夹及其内容将移至回收站") !== -1,
               "Folder delete dialog mentions folder contents")
        verify(source.indexOf('pendingMutationAction = "delete"') !== -1,
               "Delete submit records the delete mutation action")
        verify(source.indexOf("mutationInFlight = true") !== -1,
               "Delete submit enters in-flight state before calling the manager")
        verify(source.indexOf("mutationTimeoutTimer.start()") === -1,
               "Delete submit no longer starts a parallel QML timeout timer")

        verify(source.indexOf("pendingMutationAction") !== -1,
               "Tracks the active mutation flow")
        verify(source.indexOf("function applyMutationError(message)") !== -1,
               "Has deterministic mutation error helper")
        verify(source.indexOf('property string createFolderErrorMessage: ""') !== -1,
               "Tracks create validation and API errors in page state")
        verify(source.indexOf('property string renameErrorMessage: ""') !== -1,
               "Tracks rename validation and API errors in page state")
        verify(source.indexOf('property string deleteErrorMessage: ""') !== -1,
               "Tracks delete API errors in page state")
        verify(source.indexOf("moveDialog.close()") !== -1,
               "Closes move dialog on success")
        verify(source.indexOf("root.applyMutationError(message)") !== -1,
               "Routes API failures into visible dialog state")
        verify(source.indexOf("shellController.navigateTo") === -1,
               "Mutation failures do not navigate away from the drive page")
        verify(source.indexOf("function finishMutationSuccess()") !== -1,
               "Has shared mutation success handler")
        verify(source.indexOf("newFolderDialog.close()") !== -1,
               "Closes create dialog on success")
        verify(source.indexOf("renameDialog.close()") !== -1,
               "Closes rename dialog on success")
        verify(source.indexOf("deleteDialog.close()") !== -1,
               "Closes delete dialog on success")
        verify(source.indexOf("root.refreshCurrentFolder()") !== -1,
               "Refreshes current folder after successful mutation")
        verify(source.indexOf("mutationTimeoutTimer") === -1,
               "DriveBrowserPage no longer defines a separate mutation timeout timer")
    }

    function test_drive_browser_navigateToFolder_sets_currentFolderId_and_refreshes() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("function navigateToFolder(folderId)") !== -1,
               "Has navigateToFolder helper")

        var navStart = source.indexOf("function navigateToFolder(folderId)")
        var navBody = source.substring(navStart, navStart + 320)

        verify(navBody.indexOf("currentFolderId = nextFolderId") !== -1,
                 "navigateToFolder assigns currentFolderId")
        verify(navBody.indexOf("refreshCurrentFolder") !== -1,
                "navigateToFolder calls refreshCurrentFolder")
        verify(navBody.indexOf('var nextFolderId = folderId ? String(folderId) : "0"') !== -1,
                "navigateToFolder normalizes folderId to string")
        verify(navBody.indexOf('"0"') !== -1,
                "navigateToFolder falls back to root when folderId is falsy")
    }

    function test_drive_browser_refreshCurrentFolder_loads_tree_list_and_breadcrumb() {
        var source = readDriveBrowserSource()

        var refreshStart = source.indexOf("function refreshCurrentFolder()")
        verify(refreshStart !== -1, "Has refreshCurrentFolder helper")

        var refreshBody = source.substring(refreshStart, refreshStart + 400)

        verify(refreshBody.indexOf("clearSelection()") !== -1,
               "refreshCurrentFolder clears selection first")
        verify(refreshBody.indexOf('setPageState("loading")') !== -1,
               "refreshCurrentFolder sets loading state")
        verify(refreshBody.indexOf("driveManager.loadFolderTree()") !== -1,
                "refreshCurrentFolder loads the folder tree")
        verify(refreshBody.indexOf("driveManager.listFiles(currentFolderId") !== -1
               || refreshBody.indexOf("driveManager.searchFiles(root.searchQuery)") !== -1,
                "refreshCurrentFolder loads the file list for currentFolderId")
        verify(refreshBody.indexOf("driveManager.loadBreadcrumb(currentFolderId)") !== -1,
                "refreshCurrentFolder loads the breadcrumb for currentFolderId")
    }

    function test_drive_browser_tree_breadcrumb_and_folderRow_share_navigateToFolder_flow() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("onFolderClicked: function(folderId) { root.navigateToFolder(folderId) }") !== -1,
               "FolderTreePanel routes clicks through navigateToFolder")
        verify(source.indexOf("onPathClicked: function(folderId) { page.navigateToFolder(folderId) }") !== -1
               || source.indexOf("onPathClicked: function(folderId) { root.navigateToFolder(folderId) }") !== -1,
               "BreadcrumbBar routes clicks through navigateToFolder")
        verify(source.indexOf("page.navigateToFolder(model.id)") !== -1
               || source.indexOf("root.navigateToFolder(model.id)") !== -1,
               "Folder Open button routes through navigateToFolder")

        var navigateCallCount = source.split("navigateToFolder(").length - 1
        verify(navigateCallCount >= 3,
               "At least three navigateToFolder call sites exist across tree, breadcrumb, and folder-row open flows")
    }

    function test_drive_browser_currentFolderId_initializes_to_root() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('property string currentFolderId: "0"') !== -1,
               "currentFolderId initializes to root \"0\"")
    }

    function test_drive_browser_clearSelection_resets_all_selection_state() {
        var source = readDriveBrowserSource()

        var clearStart = source.indexOf("function clearSelection()")
        verify(clearStart !== -1, "Has clearSelection helper")

        var clearBody = source.substring(clearStart, clearStart + 200)

        verify(clearBody.indexOf('selectedItemId = ""') !== -1,
               "clearSelection resets selectedItemId")
        verify(clearBody.indexOf('selectedItemKind = ""') !== -1,
               "clearSelection resets selectedItemKind")
        verify(clearBody.indexOf('selectedItemName = ""') !== -1,
               "clearSelection resets selectedItemName")
    }

    function test_drive_browser_selectItem_sets_all_selection_state() {
        var source = readDriveBrowserSource()

        var selectStart = source.indexOf("function selectItem(itemId, itemKind, itemName)")
        verify(selectStart !== -1, "Has selectItem helper")

        var selectBody = source.substring(selectStart, selectStart + 300)

        verify(selectBody.indexOf("selectedItemId = String(itemId)") !== -1,
               "selectItem sets selectedItemId from itemId")
        verify(selectBody.indexOf("selectedItemKind = String(itemKind") !== -1,
               "selectItem sets selectedItemKind from itemKind")
        verify(selectBody.indexOf("selectedItemName = String(itemName") !== -1,
               "selectItem sets selectedItemName from itemName")
    }

    function test_drive_browser_navigateToFolder_preserves_currentFolderId_binding() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("currentFolderId: root.currentFolderId") !== -1,
               "FolderTreePanel binds currentFolderId to page state")
        verify(source.indexOf("function navigateToFolder(folderId)") !== -1,
               "Has navigateToFolder helper")

        var navStart = source.indexOf("function navigateToFolder(folderId)")
        var navBody = source.substring(navStart, navStart + 320)

        verify(navBody.indexOf("currentFolderId = nextFolderId") !== -1,
                "navigateToFolder assigns currentFolderId before refresh")
        verify(navBody.indexOf("refreshCurrentFolder") !== -1,
                "navigateToFolder calls refreshCurrentFolder")
    }

    function test_drive_browser_finishMutationSuccess_restores_current_path() {
        var source = readDriveBrowserSource()

        var finishStart = source.indexOf("function finishMutationSuccess()")
        verify(finishStart !== -1, "Has finishMutationSuccess handler")

        var finishBody = source.substring(finishStart, finishStart + 700)

        verify(finishBody.indexOf("pendingMutationAction =") !== -1,
               "finishMutationSuccess clears pendingMutationAction")
        verify(finishBody.indexOf("refreshCurrentFolder") !== -1,
               "finishMutationSuccess calls refreshCurrentFolder to reload tree, list, and breadcrumb")
    }

    function test_drive_browser_onOperationSuccess_only_fires_for_mutations() {
        var source = readDriveBrowserSource()

        var driveConnectionsStart = source.indexOf("target: driveManager")
        verify(driveConnectionsStart !== -1, "Has driveManager Connections block")
        var successHandlerStart = source.indexOf("function onOperationSuccess(message)", driveConnectionsStart)
        verify(successHandlerStart !== -1, "Drive connections expose an onOperationSuccess handler")

        var successBody = source.substring(successHandlerStart, successHandlerStart + 320)

        verify(successBody.indexOf("pendingMutationAction") !== -1,
                "onOperationSuccess checks pendingMutationAction before acting")
        verify(successBody.indexOf("finishMutationSuccess") !== -1,
               "onOperationSuccess delegates to finishMutationSuccess for mutations")
    }

    function test_drive_browser_refreshCurrentFolder_does_not_set_mutation_state() {
        var source = readDriveBrowserSource()

        var refreshStart = source.indexOf("function refreshCurrentFolder()")
        verify(refreshStart !== -1, "Has refreshCurrentFolder")

        var refreshBody = source.substring(refreshStart, refreshStart + 400)

        verify(refreshBody.indexOf("mutationInFlight") === -1,
               "refreshCurrentFolder does not touch mutationInFlight")
        verify(refreshBody.indexOf("pendingMutationAction") === -1,
               "refreshCurrentFolder does not touch pendingMutationAction")
        verify(refreshBody.indexOf("loadFolderTree") !== -1,
               "refreshCurrentFolder loads the folder tree")
        verify(refreshBody.indexOf("loadBreadcrumb") !== -1,
               "refreshCurrentFolder loads the breadcrumb for the current folder")
    }

    function test_drive_browser_tree_click_navigation_routes_through_navigateToFolder() {
        var source = readDriveBrowserSource()

        var treePanelStart = source.indexOf("FolderTreePanel {")
        verify(treePanelStart !== -1, "Has FolderTreePanel instance")
        var treePanelEnd = source.indexOf("}", treePanelStart + 1)
        var treePanelBlock = source.substring(treePanelStart, treePanelEnd)

        verify(treePanelBlock.indexOf("root.navigateToFolder") !== -1,
               "FolderTreePanel click routes through navigateToFolder")
        verify(treePanelBlock.indexOf("currentFolderId: root.currentFolderId") !== -1,
               "FolderTreePanel receives currentFolderId from page state")
    }

    function test_drive_browser_breadcrumb_click_navigation_routes_through_navigateToFolder() {
        var source = readDriveCompositeSource()

        var breadcrumbStart = source.indexOf("BreadcrumbBar {")
        verify(breadcrumbStart !== -1, "Has BreadcrumbBar instance")
        var breadcrumbEnd = source.indexOf("}", breadcrumbStart + 1)
        var breadcrumbBlock = source.substring(breadcrumbStart, breadcrumbEnd)

        verify(breadcrumbBlock.indexOf("page.navigateToFolder") !== -1
               || breadcrumbBlock.indexOf("root.navigateToFolder") !== -1,
               "BreadcrumbBar click routes through navigateToFolder")
    }

    function test_drive_browser_folder_open_button_routes_through_navigateToFolder() {
        var source = readDriveCompositeSource()

        var openBtnIndex = source.indexOf('text: "打开"')
        verify(openBtnIndex !== -1, "Has Open button")

        var openBtnBlock = source.substring(openBtnIndex, openBtnIndex + 400)
        verify(openBtnBlock.indexOf("page.navigateToFolder(model.id)") !== -1
               || openBtnBlock.indexOf("root.navigateToFolder(model.id)") !== -1,
               "Open button navigates through navigateToFolder")
    }

    // ── Homepage interaction contract (Task 2) ────────────────────────

    function test_drive_browser_has_homepage_up_button() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "homepageUpButton"') !== -1,
               "Exposes a stable homepageUpButton hook for runtime checks")
    }

    function test_drive_browser_homepage_up_button_enabled_depends_on_canNavigateUp() {
        var source = readDriveCompositeSource()

        var upBtnIndex = source.indexOf('objectName: "homepageUpButton"')
        verify(upBtnIndex !== -1, "Has homepageUpButton")

        var upBtnBlock = source.substring(upBtnIndex, upBtnIndex + 600)

        verify(upBtnBlock.indexOf("page.canNavigateUp") !== -1
               || upBtnBlock.indexOf("root.canNavigateUp") !== -1,
               "homepageUpButton enabled state is bound to canNavigateUp")
    }

    function test_drive_browser_defines_canNavigateUp_from_breadcrumb() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("canNavigateUp") !== -1,
               "Defines canNavigateUp derived state")

        verify(source.indexOf("resolvedParentFolderId") !== -1,
               "Defines resolvedParentFolderId derived from breadcrumb parent")

        verify(source.indexOf("breadcrumbPath") !== -1,
               "canNavigateUp resolves parent from page breadcrumbPath")
    }

    function test_drive_browser_homepage_up_button_navigates_to_resolved_parent() {
        var source = readDriveCompositeSource()

        var upBtnIndex = source.indexOf('objectName: "homepageUpButton"')
        verify(upBtnIndex !== -1, "Has homepageUpButton")

        var upBtnBlock = source.substring(upBtnIndex, upBtnIndex + 600)

        verify(upBtnBlock.indexOf("page.navigateToFolder(page.resolvedParentFolderId)") !== -1
               || upBtnBlock.indexOf("root.navigateToFolder(root.resolvedParentFolderId)") !== -1,
               "homepageUpButton click navigates to resolvedParentFolderId")
    }

    function test_drive_browser_has_folder_navigator_toggle_button() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "folderNavigatorToggleButton"') !== -1,
               "Exposes a stable folderNavigatorToggleButton hook for runtime checks")
    }

    function test_drive_browser_folder_navigator_hidden_by_default() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('objectName: "folderNavigatorPanel"') !== -1,
               "Exposes a stable folderNavigatorPanel hook")

        var panelIndex = source.indexOf('objectName: "folderNavigatorPanel"')
        var panelBlock = source.substring(panelIndex, panelIndex + 600)

        verify(panelBlock.indexOf("root.folderNavigatorExpanded") !== -1,
               "folderNavigatorPanel visibility is bound to folderNavigatorExpanded property")

        var propIndex = source.indexOf("property bool folderNavigatorExpanded: false")
        verify(propIndex !== -1,
               "folderNavigatorExpanded property initializes to false (hidden by default)")
    }

    function test_drive_browser_folder_navigator_toggle_flips_visibility() {
        var source = readDriveCompositeSource()

        var toggleIndex = source.indexOf('objectName: "folderNavigatorToggleButton"')
        verify(toggleIndex !== -1, "Has folderNavigatorToggleButton")

        var toggleBlock = source.substring(toggleIndex, toggleIndex + 600)

        verify(toggleBlock.indexOf("!page.folderNavigatorExpanded") !== -1
               || toggleBlock.indexOf("!root.folderNavigatorExpanded") !== -1,
               "folderNavigatorToggleButton toggles folderNavigatorExpanded")
    }

    function test_drive_browser_folder_navigator_resets_on_root_navigation() {
        var source = readDriveBrowserSource()

        var navStart = source.indexOf("function navigateToFolder(folderId)")
        verify(navStart !== -1, "Has navigateToFolder helper")

        var navBody = source.substring(navStart, navStart + 400)

        verify(navBody.indexOf('nextFolderId === "0"') !== -1,
               "navigateToFolder only hides the folder navigator when navigation resolves to root")
        verify(navBody.indexOf("folderNavigatorExpanded = false") !== -1,
               "navigateToFolder still hides the folder navigator for root navigation")
    }

    function test_drive_browser_folder_navigator_panel_has_folder_tree() {
        var source = readDriveBrowserSource()

        var panelIndex = source.indexOf('objectName: "folderNavigatorPanel"')
        verify(panelIndex !== -1, "Has folderNavigatorPanel")

        var panelBlock = source.substring(panelIndex, panelIndex + 800)

        verify(panelBlock.indexOf("FolderTreePanel") !== -1,
               "folderNavigatorPanel contains a FolderTreePanel")
        verify(panelBlock.indexOf("model: driveManager.treeModel") !== -1,
               "Navigator folder tree uses driveManager.treeModel")
        verify(panelBlock.indexOf("currentFolderId: root.currentFolderId") !== -1,
               "Navigator folder tree tracks page currentFolderId")
    }

    // ── Task 5: PAGE-DRIVE host mode contract ──────────────────────────────

    function test_drive_browser_has_mode_check_properties() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("readonly property bool isMyFilesMode: root.currentViewMode === \"myfiles\"") !== -1,
               "Has isMyFilesMode mode-check property")
        verify(source.indexOf("readonly property bool isSharedMode: root.currentViewMode === \"shared\"") !== -1,
               "Has isSharedMode mode-check property")
        verify(source.indexOf("readonly property bool isTrashMode: root.currentViewMode === \"trash\"") !== -1,
               "Has isTrashMode mode-check property")
    }

    function test_drive_browser_has_mode_aware_title_functions() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("function viewModeLabel()") !== -1,
               "Has viewModeLabel function for section label")
        verify(source.indexOf("function viewModeTitleText()") !== -1,
               "Has viewModeTitleText function for mode-specific title")
        verify(source.indexOf("function viewModeStatusText()") !== -1,
               "Has viewModeStatusText function for mode-specific status")
    }

    function test_drive_browser_viewModeLabel_returns_per_mode_labels() {
        var source = readDriveBrowserSource()

        var labelStart = source.indexOf("function viewModeLabel()")
        verify(labelStart !== -1, "Has viewModeLabel")
        var labelBody = source.substring(labelStart, labelStart + 400)

        verify(labelBody.indexOf('case "shared": return "分享"') !== -1,
               "shared mode returns SHARES label")
        verify(labelBody.indexOf('case "trash": return "回收站"') !== -1,
               "trash mode returns TRASH label")
        verify(labelBody.indexOf('default: return "网盘"') !== -1,
               "myfiles mode returns DRIVE label")
    }

    function test_drive_browser_viewModeTitleText_returns_per_mode_titles() {
        var source = readDriveBrowserSource()

        var titleStart = source.indexOf("function viewModeTitleText()")
        verify(titleStart !== -1, "Has viewModeTitleText")
        var titleBody = source.substring(titleStart, titleStart + 350)

        verify(titleBody.indexOf("root.isMyFilesMode") !== -1,
               "My Files mode delegates to driveTitle")
        verify(titleBody.indexOf('case "shared": return "分享"') !== -1,
               "shared mode returns Shares title")
        verify(titleBody.indexOf('case "trash": return "回收站"') !== -1,
               "trash mode returns Trash title")
    }

    function test_drive_browser_myfiles_toolbar_buttons_gated_by_mode() {
        var source = readDriveCompositeSource()

        // Up button: visible gate near objectName
        var upBtnIdx = source.indexOf('objectName: "homepageUpButton"')
        verify(upBtnIdx !== -1, "Has homepageUpButton")
        var upBlock = source.substring(upBtnIdx, upBtnIdx + 200)
        verify(upBlock.indexOf("visible: page.isMyFilesMode") !== -1
               || upBlock.indexOf("visible: root.isMyFilesMode") !== -1,
               "homepageUpButton is gated by isMyFilesMode")

        // Folders toggle: visible gate near objectName
        var toggleIdx = source.indexOf('objectName: "folderNavigatorToggleButton"')
        verify(toggleIdx !== -1, "Has folderNavigatorToggleButton")
        var toggleBlock = source.substring(toggleIdx, toggleIdx + 200)
        verify(toggleBlock.indexOf("visible: page.isMyFilesMode") !== -1
               || toggleBlock.indexOf("visible: root.isMyFilesMode") !== -1,
               "folderNavigatorToggleButton is gated by isMyFilesMode")

        // New Folder: visible gate in the button block
        var newFolderIdx = source.indexOf('text: "新建文件夹"')
        verify(newFolderIdx !== -1, "Has New Folder button")
        var newFolderBlock = source.substring(newFolderIdx, newFolderIdx + 120)
        verify(newFolderBlock.indexOf("visible: page.isMyFilesMode") !== -1
               || newFolderBlock.indexOf("visible: root.isMyFilesMode") !== -1,
               "New Folder button is gated by isMyFilesMode")

        // Upload: visible gate in the button block
        var uploadIdx = source.indexOf('id: uploadButton')
        verify(uploadIdx !== -1, "Has upload button")
        var uploadBlock = source.substring(uploadIdx, uploadIdx + 120)
        verify(uploadBlock.indexOf("visible: page.isMyFilesMode") !== -1
               || uploadBlock.indexOf("visible: root.isMyFilesMode") !== -1,
               "Upload button is gated by isMyFilesMode")
    }

    function test_drive_browser_mutation_toolbar_buttons_gated_by_mode() {
        var source = readDriveCompositeSource()

        var renameIdx = source.indexOf('text: "重命名"')
        verify(renameIdx !== -1, "Has Rename button")
        var renameBlock = source.substring(renameIdx, renameIdx + 150)
        verify(renameBlock.indexOf("visible: page.isMyFilesMode") !== -1
               || renameBlock.indexOf("visible: root.isMyFilesMode") !== -1,
               "Rename button is gated by isMyFilesMode")

        var deleteIdx = source.indexOf('text: "删除"')
        verify(deleteIdx !== -1, "Has Delete button")
        var deleteBlock = source.substring(deleteIdx, deleteIdx + 150)
        verify(deleteBlock.indexOf("visible: page.isMyFilesMode") !== -1
               || deleteBlock.indexOf("visible: root.isMyFilesMode") !== -1,
               "Delete button is gated by isMyFilesMode")

        var downloadIdx = source.indexOf('text: "下载"')
        verify(downloadIdx !== -1, "Has Download button")
        var downloadBlock = source.substring(downloadIdx, downloadIdx + 150)
        verify(downloadBlock.indexOf("visible: page.isMyFilesMode") !== -1
               || downloadBlock.indexOf("visible: root.isMyFilesMode") !== -1,
               "Download button is gated by isMyFilesMode")
    }

    function test_drive_browser_refresh_button_is_not_myfiles_only() {
        var source = readDriveCompositeSource()

        var refreshIdx = source.indexOf('text: "刷新"')
        verify(refreshIdx !== -1, "Has Refresh button")
        var refreshBlock = source.substring(refreshIdx, refreshIdx + 180)
        verify(refreshBlock.indexOf("visible: page.isMyFilesMode") === -1
               && refreshBlock.indexOf("visible: root.isMyFilesMode") === -1,
               "Refresh button is not gated to My Files only")
    }

    function test_drive_browser_status_card_uses_mode_aware_functions() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("text: page.viewModeLabel()") !== -1
               || source.indexOf("text: root.viewModeLabel()") !== -1,
               "Status card section label uses viewModeLabel()")
        verify(source.indexOf("text: page.viewModeTitleText()") !== -1
               || source.indexOf("text: root.viewModeTitleText()") !== -1,
               "Status card title uses viewModeTitleText()")
        verify(source.indexOf("text: page.viewModeStatusText()") !== -1
               || source.indexOf("text: root.viewModeStatusText()") !== -1,
               "Status card status uses viewModeStatusText()")
    }

    function test_drive_browser_state_chip_and_scope_gated_by_myfiles() {
        var source = readDriveCompositeSource()

        var chipIdx = source.indexOf("implicitWidth: stateChipLabel.implicitWidth")
        verify(chipIdx !== -1, "Has state chip")
        var chipBlock = source.substring(chipIdx - 200, chipIdx + 100)
        verify(chipBlock.indexOf("visible: page.isMyFilesMode") !== -1
               || chipBlock.indexOf("visible: root.isMyFilesMode") !== -1,
               "State chip is gated by isMyFilesMode")

        verify(source.indexOf("visible: page.isMyFilesMode\n                text: page.folderScopeLabel()") !== -1
               || source.indexOf("visible: page.isMyFilesMode") < source.indexOf("text: page.folderScopeLabel()") + 200
               || source.indexOf("visible: root.isMyFilesMode") < source.indexOf("text: root.folderScopeLabel()") + 200,
               "Folder scope label is gated by isMyFilesMode")
    }

    function test_drive_browser_pageStateView_gated_by_myfiles() {
        var source = readDriveCompositeSource()

        var psvIdx = source.indexOf("PageStateView {")
        verify(psvIdx !== -1, "Has PageStateView")

        var psvBlock = source.substring(psvIdx, psvIdx + 200)
        verify(psvBlock.indexOf("visible: page.isMyFilesMode") !== -1
               || psvBlock.indexOf("visible: root.isMyFilesMode") !== -1,
               "PageStateView is gated by isMyFilesMode")
    }

    function test_drive_browser_shared_mode_uses_share_manager_for_content_and_batch_results() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("function refreshSharedList()") !== -1,
               "Has dedicated shared-mode refresh helper")
        verify(source.indexOf("shareManager.listShares()") !== -1,
               "Shared refresh uses shareManager.listShares()")
        verify(source.indexOf("model: shareManager.listModel") !== -1,
               "Shared content binds to shareManager.listModel")
        verify(source.indexOf("model: shareManager.batchResultModel") !== -1,
               "Shared batch-result content binds to shareManager.batchResultModel")
        verify(source.indexOf('objectName: "sharedCreateButton"') !== -1,
               "Toolbar exposes a create-share action")
        var buttonBlock = source.substring(source.indexOf('"sharedCreateButton"'), source.indexOf('"sharedCreateButton"') + 500)
        verify(buttonBlock.indexOf("page.isMyFilesMode") !== -1,
               "Create share button visibility is gated by My Files mode")
        verify(buttonBlock.indexOf("page.isSharedMode") === -1,
               "Create share button is not visible in Shared mode")
        verify(source.indexOf('objectName: "sharedCancelSelectedButton"') !== -1,
               "Shared toolbar exposes a cancel-selected action")
        verify(source.indexOf('objectName: "sharedBatchResultListView"') !== -1,
               "Shared batch-result list exposes a stable runtime hook")
        verify(source.indexOf("page.submitCancelShare(model.shareId)") !== -1
               || source.indexOf("root.submitCancelShare(model.shareId)") !== -1,
               "Shared row actions route cancellation through shareManager-backed flow")
    }

    function test_drive_browser_trash_mode_uses_trash_manager_for_content_and_batch_results() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("function refreshTrashList()") !== -1,
               "Has dedicated trash-mode refresh helper")
        verify(source.indexOf("trashManager.listTrash()") !== -1,
               "Trash refresh uses trashManager.listTrash()")
        verify(source.indexOf("model: trashManager.listModel") !== -1,
               "Trash content binds to trashManager.listModel")
        verify(source.indexOf("model: trashManager.batchResultModel") !== -1,
               "Trash batch-result content binds to trashManager.batchResultModel")
        verify(source.indexOf('objectName: "trashRestoreSelectedButton"') !== -1,
               "Trash toolbar exposes a restore-selected action")
        verify(source.indexOf('objectName: "trashDeleteSelectedButton"') !== -1,
               "Trash toolbar exposes a delete-selected action")
        verify(source.indexOf('objectName: "trashClearAllButton"') !== -1,
               "Trash toolbar exposes a clear-all action")
        verify(source.indexOf('objectName: "trashBatchResultListView"') !== -1,
               "Trash batch-result list exposes a stable runtime hook")
        verify(source.indexOf("page.submitRestoreTrash(model.trashId)") !== -1
               || source.indexOf("root.submitRestoreTrash(model.trashId)") !== -1,
               "Trash row actions route restore through trashManager-backed flow")
        verify(source.indexOf("page.submitDeleteTrash(model.trashId)") !== -1
               || source.indexOf("root.submitDeleteTrash(model.trashId)") !== -1,
               "Trash row actions route delete through trashManager-backed flow")
    }

    function test_drive_browser_refreshCurrentView_and_activateViewMode_route_shared_and_trash_modes_to_respective_managers() {
        var source = readDriveBrowserSource()

        var refreshStart = source.indexOf("function refreshCurrentView()")
        verify(refreshStart !== -1, "Has refreshCurrentView helper")
        var refreshBody = source.substring(refreshStart, refreshStart + 360)
        verify(refreshBody.indexOf("root.isSharedMode") !== -1,
               "refreshCurrentView checks shared mode")
        verify(refreshBody.indexOf("root.refreshSharedList()") !== -1,
               "refreshCurrentView routes shared refresh through refreshSharedList")
        verify(refreshBody.indexOf("root.isTrashMode") !== -1,
               "refreshCurrentView checks trash mode")
        verify(refreshBody.indexOf("root.refreshTrashList()") !== -1,
               "refreshCurrentView routes trash refresh through refreshTrashList")

        var activateStart = source.indexOf("function activateViewMode(mode)")
        verify(activateStart !== -1, "Has activateViewMode")
        var activateBody = source.substring(activateStart, activateStart + 800)
        verify(activateBody.indexOf('if (nextMode === "shared")') !== -1,
               "activateViewMode has a dedicated shared-mode branch")
        verify(activateBody.indexOf("root.refreshSharedList()") !== -1,
               "activateViewMode triggers shareManager-backed refresh for shared mode")
        verify(activateBody.indexOf('if (nextMode === "trash")') !== -1,
               "activateViewMode has a dedicated trash-mode branch")
        verify(activateBody.indexOf("root.refreshTrashList()") !== -1,
               "activateViewMode triggers trashManager-backed refresh for trash mode")
    }

    function test_drive_browser_folder_navigator_gated_by_myfiles() {
        var source = readDriveBrowserSource()

        var panelIdx = source.indexOf('objectName: "folderNavigatorPanel"')
        verify(panelIdx !== -1, "Has folder navigator panel")
        var panelBlock = source.substring(panelIdx, panelIdx + 300)
        verify(panelBlock.indexOf("root.isMyFilesMode") !== -1,
               "Folder navigator panel visibility is gated by isMyFilesMode")
    }

    function test_drive_browser_activateViewMode_clears_folder_navigator() {
        var source = readDriveBrowserSource()

        var activateStart = source.indexOf("function activateViewMode(mode)")
        verify(activateStart !== -1, "Has activateViewMode")
        var activateBody = source.substring(activateStart, activateStart + 400)

        verify(activateBody.indexOf("folderNavigatorExpanded = false") !== -1,
               "activateViewMode collapses folder navigator on mode switch")
    }

    function test_drive_browser_upload_download_errors_gated_by_myfiles() {
        var source = readDriveCompositeSource()

        var uploadErrIdx = source.indexOf("text: page.uploadErrorMessage")
        if (uploadErrIdx === -1) {
            uploadErrIdx = source.indexOf("text: root.uploadErrorMessage")
        }
        verify(uploadErrIdx !== -1, "Has upload error label")
        var uploadErrBlock = source.substring(uploadErrIdx, uploadErrIdx + 150)
        verify(uploadErrBlock.indexOf("page.isMyFilesMode") !== -1
               || uploadErrBlock.indexOf("root.isMyFilesMode") !== -1,
               "Upload error label visibility is gated by isMyFilesMode")

        var downloadErrIdx = source.indexOf("text: page.downloadErrorMessage")
        if (downloadErrIdx === -1) {
            downloadErrIdx = source.indexOf("text: root.downloadErrorMessage")
        }
        verify(downloadErrIdx !== -1, "Has download error label")
        var downloadErrBlock = source.substring(downloadErrIdx, downloadErrIdx + 150)
verify(downloadErrBlock.indexOf("page.isMyFilesMode") !== -1
               || downloadErrBlock.indexOf("root.isMyFilesMode") !== -1,
               "Download error label visibility is gated by isMyFilesMode")
    }

    // ── Bug 5: FolderTreePanel close button handler ──────────────────────────

    function test_drive_browser_folder_tree_panel_close_handler() {
        var source = readDriveBrowserSource()

        // Verify FolderTreePanel instance wires the closeRequested signal
        var treePanelIdx = source.indexOf("FolderTreePanel {")
        verify(treePanelIdx !== -1, "Has FolderTreePanel instance")
        var treePanelEnd = source.indexOf("}", treePanelIdx + 1)
        var treePanelBlock = source.substring(treePanelIdx, treePanelEnd)

        verify(treePanelBlock.indexOf("onCloseRequested") !== -1,
               "FolderTreePanel wires closeRequested signal to collapse folder navigator")
        verify(treePanelBlock.indexOf("folderNavigatorExpanded = false") !== -1,
               "closeRequested handler collapses folderNavigatorExpanded to hide the panel")
    }
}
