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
        verify(source.indexOf("onFolderClicked: function(folderId) { root.openFolder(folderId) }") !== -1,
               "Folder tree clicks open folders through page flow")
        verify(source.indexOf("onPathClicked: function(folderId) { root.openFolder(folderId) }") !== -1,
               "Breadcrumb clicks open folders through page flow")
        verify(source.indexOf("function openFolder(folderId)") !== -1,
               "Has openFolder helper")
        verify(source.indexOf("refreshCurrentFolder()") !== -1,
               "Folder open reuses refresh flow")
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
        verify(source.indexOf("driveManager.getFileDetail(") === -1,
               "Row click no longer triggers file detail loading")
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
}
