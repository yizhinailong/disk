import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopDriveBrowser"
    id: testDriveBrowser

    function readDriveBrowserSource() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/DriveBrowserPage.qml"), false)
        xhr.send()
        verify(xhr.responseText.length > 0, "DriveBrowserPage.qml was read")
        return xhr.responseText
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
        var source = readDriveBrowserSource()

        verify(source.indexOf("PageStateView") !== -1, "Uses PageStateView for state management")
        verify(source.indexOf("pageState: shellController.pageState") !== -1,
               "Binds to shellController.pageState")
    }

    function test_drive_browser_exposes_only_p0_actions() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('text: "Refresh"') !== -1, "Exposes Refresh action")
        verify(source.indexOf('text: "New Folder"') !== -1, "Exposes New Folder action")
        verify(source.indexOf('text: "Upload"') !== -1, "Exposes Upload action")
        verify(source.indexOf('text: "Rename"') !== -1, "Exposes Rename action")
        verify(source.indexOf('text: "Delete"') !== -1, "Exposes Delete action")
        verify(source.indexOf('text: "Download"') !== -1, "Exposes Download action")
        verify(source.indexOf('enabled: root.selectedItemId !== ""') !== -1,
               "Selection-gates rename and delete")
        verify(source.indexOf('enabled: root.selectedItemKind === "file"') !== -1,
               "Selection-gates download to files")

        verify(source.indexOf('Search files...') === -1, "Search field is removed")
        verify(source.indexOf('searchFiles(') === -1, "Search behavior is removed")
        verify(source.indexOf('text: "Move"') === -1, "Move action is absent")
        verify(source.indexOf('text: "Copy"') === -1, "Copy action is absent")
        verify(source.indexOf('text: "Share"') === -1, "Share action is absent")
        verify(source.indexOf('text: "Batch"') === -1, "Batch action is absent")
    }

    function test_drive_browser_has_single_file_upload_entry_and_helper() {
        var source = readDriveBrowserSource()

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
        verify(source.indexOf("onClicked: root.openUploadFileChooser()") !== -1,
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
        verify(source.indexOf('uploadErrorMessage = "Please choose one local file."') !== -1,
               "Empty upload input gets deterministic feedback")
        verify(source.indexOf('uploadErrorMessage = "Please choose an existing local file."') !== -1,
               "Invalid upload path gets deterministic feedback")
        verify(source.indexOf('property string uploadErrorMessage: ""') !== -1,
               "Upload error feedback is kept in page state")
    }

    function test_drive_browser_has_file_only_owner_download_entry_and_helper() {
        var source = readDriveBrowserSource()

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
        verify(source.indexOf("onClicked: root.openOwnerDownloadFileChooser(root.selectedItemId, root.selectedItemName)") !== -1,
               "Download toolbar action opens the owner save-file chooser")
        verify(source.indexOf("onAccepted: root.startOwnerDownloadToPath(root.pendingOwnerDownloadFileId, root.pendingOwnerDownloadFilename, file)") !== -1,
               "Chooser acceptance routes through the owner download helper")
        verify(source.indexOf("onRejected: root.startOwnerDownloadToPath(root.pendingOwnerDownloadFileId, root.pendingOwnerDownloadFilename, \"\")") !== -1,
               "Chooser rejection routes through the same owner download helper")
        verify(source.indexOf('downloadErrorMessage = "Please select one file to download."') !== -1,
               "Invalid selection gets deterministic owner download feedback")
        verify(source.indexOf('downloadErrorMessage = "Please choose one download destination."') !== -1,
               "Empty download destination gets deterministic feedback")
        verify(source.indexOf("transferManager.StartDownload(ownerFileId, localPath, \"owner\")") !== -1,
               "Valid owner download requests call TransferManager.StartDownload with the owner domain")
        verify(source.indexOf("transferManager.StartVisitorDownload(") === -1,
               "Does not add visitor/share-token download flow")
        verify(source.indexOf("Platform.FolderDialog {") === -1,
               "Does not introduce folder download")
    }

    function test_drive_browser_has_folder_tree_and_breadcrumb_navigation() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("FolderTreePanel") !== -1, "Has FolderTreePanel")
        verify(source.indexOf("model: driveManager.treeModel") !== -1,
               "FolderTreePanel uses driveManager.treeModel")
        verify(source.indexOf("BreadcrumbBar") !== -1, "Has BreadcrumbBar")
        verify(source.indexOf("onFolderClicked: function(folderId) { root.navigateToFolder(folderId) }") !== -1,
               "Folder tree clicks navigate through page flow")
        verify(source.indexOf("onPathClicked: function(folderId) { root.navigateToFolder(folderId) }") !== -1,
               "Breadcrumb clicks navigate through page flow")
        verify(source.indexOf("function navigateToFolder(folderId)") !== -1,
               "Has navigateToFolder helper")
        verify(source.indexOf("refreshCurrentFolder()") !== -1,
               "Folder navigation reuses refresh flow")
    }

    function test_drive_browser_row_click_is_selection_only() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("model: driveManager.listModel") !== -1,
               "ListView uses driveManager.listModel")
        verify(source.indexOf("onClicked: root.selectItem(model.id, model.kind, model.name)") !== -1,
               "Row click only updates selection")
        verify(source.indexOf('text: "Open"') !== -1,
               "Folder rows expose dedicated Open control")
        verify(source.indexOf('visible: model.kind === "folder"') !== -1,
               "Open control is only shown for folder rows")
        verify(source.indexOf("root.navigateToFolder(model.id)") !== -1,
               "Folder Open button navigates through the shared flow")
        verify(source.indexOf("driveManager.getFileDetail(") === -1,
               "Row click no longer triggers file detail loading")
    }

    function test_drive_browser_file_list_uses_static_table_header_and_role_fallbacks() {
        var source = readDriveBrowserSource()
        var nameHeaderIndex = source.indexOf('text: "Name"')
        var typeHeaderIndex = source.indexOf('text: "Type"')
        var sizeHeaderIndex = source.indexOf('text: "Size"')
        var updatedHeaderIndex = source.indexOf('text: "Updated"')

        verify(nameHeaderIndex !== -1, "Has static Name header")
        verify(typeHeaderIndex !== -1, "Has static Type header")
        verify(sizeHeaderIndex !== -1, "Has static Size header")
        verify(updatedHeaderIndex !== -1, "Has static Updated header")
        verify(nameHeaderIndex < typeHeaderIndex && typeHeaderIndex < sizeHeaderIndex && sizeHeaderIndex < updatedHeaderIndex,
               "Header columns stay ordered as Name, Type, Size, Updated")
        verify(source.indexOf("function formatItemType(kind, mimeType)") !== -1,
               "Uses a dedicated helper for the Type column")
        verify(source.indexOf('return typeLabel !== "" ? typeLabel : "File"') !== -1,
               "Files fall back to File when mimeType is missing")
        verify(source.indexOf("function formatItemSize(kind, size, itemCount)") !== -1,
               "Uses a dedicated helper for the Size column")
        verify(source.indexOf('return itemCountValue !== "" ? itemCountValue + " items" : "—"') !== -1,
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
        verify(source.indexOf('spacing: root.tableColumnSpacing') !== -1,
               "Header and rows share one explicit column-spacing contract")
        verify(source.indexOf('wrapMode: Text.NoWrap') !== -1,
               "Header labels and row metadata stay single-line for alignment")
        verify(source.indexOf("TableView") === -1,
               "Keeps ListView instead of introducing TableView")
    }

    function test_drive_browser_file_list_long_names_use_deterministic_elision_and_disclosure() {
        var source = readDriveBrowserSource()

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
        verify(source.indexOf("root.refreshCurrentFolder()") !== -1,
               "Calls refreshCurrentFolder on init")
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
        verify(source.indexOf('error: "Name must be 1-255 characters"') !== -1,
               "Validates backend length contract")
        verify(source.indexOf("Name cannot be \".\" or \"..\"") !== -1,
               "Rejects reserved names")
        verify(source.indexOf("Name cannot start with \".\"") !== -1,
               "Rejects hidden-dot names")
        verify(source.indexOf("Name must use ASCII printable characters only") !== -1,
               "Rejects non-ASCII printable names")
        verify(source.indexOf("var forbiddenChars = ") !== -1,
               "Defines the forbidden character set")
        verify(source.indexOf("Name cannot contain any of /") !== -1,
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
        verify(source.indexOf("driveManager.renameItem(root.selectedItemId, validationResult.value)") !== -1,
               "Valid rename flow calls DriveManager.renameItem")
        verify(source.indexOf("driveManager.deleteItems([root.selectedItemId])") !== -1,
               "Delete flow calls DriveManager.deleteItems for one selected item")
        verify(source.indexOf('if (root.selectedItemKind !== "file")') !== -1,
               "Delete submit path checks item kind before using the file delete contract")
        verify(source.indexOf('deleteErrorMessage = "Folder deletion is not supported in this build."') !== -1,
               "Folder delete attempts surface deterministic local feedback")
        verify(source.indexOf('deleteErrorMessage = "Folder deletion is not supported in this build."')
               < source.indexOf("driveManager.deleteItems([root.selectedItemId])"),
               "Folder delete feedback is assigned before any file delete request call")

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
    }

    function test_drive_browser_navigateToFolder_sets_currentFolderId_and_refreshes() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("function navigateToFolder(folderId)") !== -1,
               "Has navigateToFolder helper")

        var navStart = source.indexOf("function navigateToFolder(folderId)")
        var navBody = source.substring(navStart, navStart + 280)

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
        verify(refreshBody.indexOf("driveManager.listFiles(currentFolderId)") !== -1,
               "refreshCurrentFolder loads the file list for currentFolderId")
        verify(refreshBody.indexOf("driveManager.loadBreadcrumb(currentFolderId)") !== -1,
               "refreshCurrentFolder loads the breadcrumb for currentFolderId")
    }

    function test_drive_browser_tree_breadcrumb_and_folderRow_share_navigateToFolder_flow() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("onFolderClicked: function(folderId) { root.navigateToFolder(folderId) }") !== -1,
               "FolderTreePanel routes clicks through navigateToFolder")
        verify(source.indexOf("onPathClicked: function(folderId) { root.navigateToFolder(folderId) }") !== -1,
               "BreadcrumbBar routes clicks through navigateToFolder")
        verify(source.indexOf("root.navigateToFolder(model.id)") !== -1,
               "Folder Open button routes through navigateToFolder")

        var firstNav = source.indexOf("root.navigateToFolder(")
        var secondNav = source.indexOf("root.navigateToFolder(", firstNav + 1)
        var thirdNav = source.indexOf("root.navigateToFolder(", secondNav + 1)
        verify(firstNav !== -1, "First navigateToFolder call exists")
        verify(secondNav !== -1, "Second navigateToFolder call exists")
        verify(thirdNav !== -1, "Third navigateToFolder call exists (folder row Open button)")
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
        var navBody = source.substring(navStart, navStart + 260)

        verify(navBody.indexOf("currentFolderId = nextFolderId") !== -1,
                "navigateToFolder assigns currentFolderId before refresh")
        verify(navBody.indexOf("refreshCurrentFolder") !== -1,
                "navigateToFolder triggers refresh after setting currentFolderId")
    }

    function test_drive_browser_finishMutationSuccess_restores_current_path() {
        var source = readDriveBrowserSource()

        var finishStart = source.indexOf("function finishMutationSuccess()")
        verify(finishStart !== -1, "Has finishMutationSuccess handler")

        var finishBody = source.substring(finishStart, finishStart + 400)

        verify(finishBody.indexOf("pendingMutationAction =") !== -1,
               "finishMutationSuccess clears pendingMutationAction")
        verify(finishBody.indexOf("refreshCurrentFolder") !== -1,
               "finishMutationSuccess calls refreshCurrentFolder to reload tree, list, and breadcrumb")
    }

    function test_drive_browser_onOperationSuccess_only_fires_for_mutations() {
        var source = readDriveBrowserSource()

        var successHandlerStart = source.indexOf("function onOperationSuccess(message)")
        verify(successHandlerStart !== -1, "Has onOperationSuccess handler")

        var successBody = source.substring(successHandlerStart, successHandlerStart + 300)

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
        var source = readDriveBrowserSource()

        var breadcrumbStart = source.indexOf("BreadcrumbBar {")
        verify(breadcrumbStart !== -1, "Has BreadcrumbBar instance")
        var breadcrumbEnd = source.indexOf("}", breadcrumbStart + 1)
        var breadcrumbBlock = source.substring(breadcrumbStart, breadcrumbEnd)

        verify(breadcrumbBlock.indexOf("root.navigateToFolder") !== -1,
               "BreadcrumbBar click routes through navigateToFolder")
    }

    function test_drive_browser_folder_open_button_routes_through_navigateToFolder() {
        var source = readDriveBrowserSource()

        var openBtnIndex = source.indexOf('text: "Open"')
        verify(openBtnIndex !== -1, "Has Open button")

        var openBtnBlock = source.substring(openBtnIndex, openBtnIndex + 400)
        verify(openBtnBlock.indexOf("root.navigateToFolder(model.id)") !== -1,
               "Open button navigates through navigateToFolder")
    }

    // ── Homepage interaction contract (Task 2) ────────────────────────

    function test_drive_browser_has_homepage_up_button() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('objectName: "homepageUpButton"') !== -1,
               "Exposes a stable homepageUpButton hook for runtime checks")
    }

    function test_drive_browser_homepage_up_button_enabled_depends_on_canNavigateUp() {
        var source = readDriveBrowserSource()

        var upBtnIndex = source.indexOf('objectName: "homepageUpButton"')
        verify(upBtnIndex !== -1, "Has homepageUpButton")

        var upBtnBlock = source.substring(upBtnIndex, upBtnIndex + 600)

        verify(upBtnBlock.indexOf("root.canNavigateUp") !== -1,
               "homepageUpButton enabled state is bound to canNavigateUp")
    }

    function test_drive_browser_defines_canNavigateUp_from_breadcrumb() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("canNavigateUp") !== -1,
               "Defines canNavigateUp derived state")

        verify(source.indexOf("resolvedParentFolderId") !== -1,
               "Defines resolvedParentFolderId derived from breadcrumb parent")

        verify(source.indexOf("breadcrumbBar.path") !== -1,
               "canNavigateUp resolves parent from breadcrumbBar.path")
    }

    function test_drive_browser_homepage_up_button_navigates_to_resolved_parent() {
        var source = readDriveBrowserSource()

        var upBtnIndex = source.indexOf('objectName: "homepageUpButton"')
        verify(upBtnIndex !== -1, "Has homepageUpButton")

        var upBtnBlock = source.substring(upBtnIndex, upBtnIndex + 600)

        verify(upBtnBlock.indexOf("root.navigateToFolder(root.resolvedParentFolderId)") !== -1,
               "homepageUpButton click navigates to resolvedParentFolderId")
    }

    function test_drive_browser_has_folder_navigator_toggle_button() {
        var source = readDriveBrowserSource()

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
        var source = readDriveBrowserSource()

        var toggleIndex = source.indexOf('objectName: "folderNavigatorToggleButton"')
        verify(toggleIndex !== -1, "Has folderNavigatorToggleButton")

        var toggleBlock = source.substring(toggleIndex, toggleIndex + 600)

        verify(toggleBlock.indexOf("!root.folderNavigatorExpanded") !== -1,
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
}
