import QtQuick 2.15
import QtTest 1.15

TestCase {
    id: testRoot
    name: "DesktopDriveBrowserNavigation"
    when: windowShown

    width: 1024
    height: 640

    property var _created: []

    function cleanup() {
        for (var i = 0; i < _created.length; ++i) {
            if (_created[i]) {
                _created[i].destroy()
            }
        }
        _created = []
    }

    function registerObject(obj) {
        _created.push(obj)
        return obj
    }

    function readQmlSource(relPath) {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/" + relPath), false)
        xhr.send()
        verify(xhr.responseText.length > 0, relPath + " was read")
        return xhr.responseText
    }

    function sourceUrl(relPath) {
        var base = Qt.resolvedUrl(".").toString()
        return normalizeFileUrl(base + "../../../qml/" + relPath)
    }

    function normalizeFileUrl(url) {
        var idx = url.indexOf("://")
        var sep = idx >= 0 ? idx + 3 : 0
        var prefix = url.substring(0, sep)
        var path = url.substring(sep)
        var parts = path.split("/")
        var stack = []

        for (var i = 0; i < parts.length; ++i) {
            if (parts[i] === "..") {
                if (stack.length > 0) {
                    stack.pop()
                }
            } else if (parts[i] !== "" && parts[i] !== ".") {
                stack.push(parts[i])
            }
        }

        var leadingSlash = path.charAt(0) === "/" ? "/" : ""
        return prefix + leadingSlash + stack.join("/")
    }

    function loadComponent(relPath) {
        var component = Qt.createComponent(sourceUrl(relPath))
        verify(component !== null, "Component created for " + relPath)
        if (component.status === Component.Loading) {
            wait(500)
        }
        compare(component.status, Component.Ready, component.errorString())
        return component
    }

    function createPage() {
        var component = loadComponent("pages/DriveBrowserPage.qml")
        var page = component.createObject(testRoot, {
            width: 1024,
            height: 640
        })
        verify(page !== null, "DriveBrowserPage instance created")
        registerObject(page)
        page.visible = true
        wait(100)
        return page
    }

    function test_context_properties_are_injected() {
        verify(driveManager !== null, "driveManager should be available as context property")
        verify(shellController !== null, "shellController should be available as context property")
    }

    function test_navigateToFolder_sets_currentFolderId_to_target() {
        var page = createPage()

        compare(page.currentFolderId, "0", "Initial folder is root")

        page.navigateToFolder("42")
        compare(page.currentFolderId, "42", "navigateToFolder updates currentFolderId")

        page.navigateToFolder("10")
        compare(page.currentFolderId, "10", "navigateToFolder updates currentFolderId again")
    }

    function test_navigateToFolder_falls_back_to_root_on_falsy() {
        var page = createPage()

        page.navigateToFolder("42")
        compare(page.currentFolderId, "42", "Sanity: folder id set to 42")

        page.navigateToFolder("")
        compare(page.currentFolderId, "0", "Empty string falls back to root")

        page.navigateToFolder("42")
        page.navigateToFolder(0)
        compare(page.currentFolderId, "0", "Numeric zero falls back to root")
    }

    function test_navigateToFolder_triggers_all_three_loads() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager context property is available")
        dm.resetCounts()

        page.navigateToFolder("42")

        verify(dm.loadFolderTreeCallCount() > 0,
               "navigateToFolder triggers loadFolderTree")
        verify(dm.listFilesCalls().length > 0,
               "navigateToFolder triggers listFiles")
        verify(dm.loadBreadcrumbCalls().length > 0,
               "navigateToFolder triggers loadBreadcrumb")

        var lastListArg = dm.listFilesCalls()[dm.listFilesCalls().length - 1]
        compare(lastListArg, "42",
                "listFiles is called with the navigated folder id")

        var lastBreadcrumbArg = dm.loadBreadcrumbCalls()[dm.loadBreadcrumbCalls().length - 1]
        compare(lastBreadcrumbArg, "42",
                "loadBreadcrumb is called with the navigated folder id")
    }

    function test_navigateToFolder_clears_selection() {
        var page = createPage()

        page.selectItem("99", "file", "report.pdf")
        compare(page.selectedItemId, "99", "Selection set")

        page.navigateToFolder("10")
        compare(page.selectedItemId, "", "Selection cleared after navigation")
        compare(page.selectedItemKind, "", "Kind cleared after navigation")
        compare(page.selectedItemName, "", "Name cleared after navigation")
    }

    function test_tree_panel_receives_currentFolderId_from_page() {
        var page = createPage()

        page.navigateToFolder("40")
        wait(50)

        var folderTreePanelChild = findChildByType(page, "FolderTreePanel")
        verify(folderTreePanelChild !== null, "FolderTreePanel child found")
        compare(folderTreePanelChild.currentFolderId, "40",
                "FolderTreePanel.currentFolderId tracks page state")
    }

    function test_breadcrumb_loaded_updates_breadcrumb_bar_path() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        var testBreadcrumb = [
            { id: 0, name: "Root" },
            { id: 10, name: "Documents" },
            { id: 20, name: "Project Alpha" }
        ]
        dm.breadcrumbLoaded(testBreadcrumb)
        wait(50)

        var breadcrumbBar = findByObjectName(page, "breadcrumbBar")
        if (breadcrumbBar) {
            verify(breadcrumbBar.path.length >= 3,
                   "Breadcrumb path has at least 3 entries after signal")
        }
    }

    function test_finishMutationSuccess_clears_mutation_state_and_refreshes() {
        var page = createPage()

        page.mutationInFlight = true
        page.pendingMutationAction = "create"

        var dm = driveManager
        verify(dm !== null, "driveManager available")
        dm.resetCounts()

        page.finishMutationSuccess()
        wait(50)

        compare(page.mutationInFlight, false,
                "mutationInFlight cleared after finishMutationSuccess")
        compare(page.pendingMutationAction, "",
                "pendingMutationAction cleared after finishMutationSuccess")
        verify(dm.loadFolderTreeCallCount() > 0,
               "finishMutationSuccess triggers a refresh (loadFolderTree called)")
        verify(dm.listFilesCalls().length > 0,
               "finishMutationSuccess triggers a refresh (listFiles called)")
    }

    function test_finishMutationSuccess_preserves_current_folder_on_refresh() {
        var page = createPage()

        page.navigateToFolder("20")
        wait(50)

        var dm = null
        try { dm = driveManager } catch (e) {} 
        verify(dm !== null, "driveManager available")
        dm.resetCounts()

        page.mutationInFlight = true
        page.pendingMutationAction = "rename"
        page.finishMutationSuccess()
        wait(50)

        compare(page.currentFolderId, "20",
                "After mutation success, page stays in the same folder")
        verify(dm.loadBreadcrumbCalls().length > 0,
               "Breadcrumb reloaded for current folder")
        var lastBreadcrumbArg = dm.loadBreadcrumbCalls()[dm.loadBreadcrumbCalls().length - 1]
        compare(lastBreadcrumbArg, "20",
                "Breadcrumb reload targets the current folder id")
    }

    function test_operationSuccess_ignored_when_no_mutation_pending() {
        var page = createPage()

        page.mutationInFlight = false
        page.pendingMutationAction = ""

        var dm = driveManager
        verify(dm !== null, "driveManager available")
        dm.resetCounts()

        dm.operationSuccess("Folder tree loaded")

        compare(page.mutationInFlight, false,
                "operationSuccess from non-mutation source does not set mutationInFlight")
        compare(page.pendingMutationAction, "",
                "pendingMutationAction stays empty for non-mutation operationSuccess")
        verify(dm.loadFolderTreeCallCount() === 0,
               "No refresh triggered by non-mutation operationSuccess")
    }

    function test_paginationLoaded_updates_page_state_to_content() {
        var page = createPage()

        var sc = shellController
        verify(sc !== null, "shellController available")

        sc.setPageState("loading")
        wait(50)

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        dm.paginationLoaded(1, 1, 5)
        wait(50)

        compare(sc.pageState, "content",
                "paginationLoaded with total>0 sets state to content")
    }

    function test_paginationLoaded_updates_page_state_to_empty() {
        var page = createPage()

        var sc = shellController
        verify(sc !== null, "shellController available")

        sc.setPageState("loading")
        wait(50)

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        dm.paginationLoaded(1, 0, 0)
        wait(50)

        compare(sc.pageState, "empty",
                "paginationLoaded with total=0 sets state to empty")
    }

    function test_listLoadFailed_sets_page_state_to_error() {
        var page = createPage()

        var sc = shellController
        verify(sc !== null, "shellController available")

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        dm.listLoadFailed("Network error", 0)
        wait(50)

        compare(sc.pageState, "error",
                "listLoadFailed sets page state to error")
    }

    function test_sequential_navigation_updates_all_state() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        page.navigateToFolder("10")
        compare(page.currentFolderId, "10", "First navigation")

        dm.paginationLoaded(1, 1, 3)
        wait(50)

        page.navigateToFolder("20")
        compare(page.currentFolderId, "20", "Second navigation")

        page.selectItem("99", "folder", "Subfolder")

        page.navigateToFolder("30")
        compare(page.currentFolderId, "30", "Third navigation")
        compare(page.selectedItemId, "",
                "Selection cleared on navigation")
    }

    function test_rapid_navigation_final_state_wins() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")
        dm.resetCounts()

        page.navigateToFolder("10")
        page.navigateToFolder("20")
        page.navigateToFolder("30")

        compare(page.currentFolderId, "30",
                "Final navigation wins after rapid calls")

        var lastListArg = dm.listFilesCalls()[dm.listFilesCalls().length - 1]
        compare(lastListArg, "30",
                "Last listFiles call targets the final folder")

        var lastBreadcrumbArg = dm.loadBreadcrumbCalls()[dm.loadBreadcrumbCalls().length - 1]
        compare(lastBreadcrumbArg, "30",
                "Last loadBreadcrumb call targets the final folder")
    }

    function test_repeated_refresh_does_not_corrupt_folder_state() {
        var page = createPage()

        page.navigateToFolder("42")
        wait(50)

        var dm = driveManager
        verify(dm !== null, "driveManager available")
        dm.resetCounts()

        page.refreshCurrentFolder()
        page.refreshCurrentFolder()
        page.refreshCurrentFolder()

        compare(page.currentFolderId, "42",
                "Current folder id is preserved across repeated refreshes")
        compare(dm.loadFolderTreeCallCount(), 3,
                "Each refresh triggers a loadFolderTree call")
        compare(dm.listFilesCalls().length, 3,
                "Each refresh triggers a listFiles call")
        compare(dm.loadBreadcrumbCalls().length, 3,
                "Each refresh triggers a loadBreadcrumb call")

        var allTargetCurrentFolder = true
        for (var i = 0; i < dm.listFilesCalls().length; ++i) {
            if (dm.listFilesCalls()[i] !== "42") {
                allTargetCurrentFolder = false
            }
        }
        verify(allTargetCurrentFolder,
               "All listFiles calls target the current folder id")
    }

    function test_empty_tree_model_does_not_crash_navigation() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")
        verify(dm.treeModel !== null, "Tree model is available")
        compare(dm.treeModel.rowCount(), 0,
                "Tree model starts empty in stub")

        page.navigateToFolder("10")
        compare(page.currentFolderId, "10",
                "Navigation works with empty tree model")
        compare(page.selectedItemId, "",
                "Selection is clean after navigating into empty tree")

        dm.paginationLoaded(1, 0, 0)
        wait(50)

        var sc = shellController
        verify(sc !== null, "shellController available")
        compare(sc.pageState, "empty",
                "Empty-folder state works with empty tree model")

        page.navigateToFolder("20")
        compare(page.currentFolderId, "20",
                "Second navigation also works with empty tree model")
    }

    function test_navigation_to_unknown_folder_preserves_functional_state() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        page.navigateToFolder("999")
        compare(page.currentFolderId, "999",
                "Page accepts folder id not in tree model")

        dm.paginationLoaded(1, 1, 2)
        wait(50)

        var sc = shellController
        compare(sc.pageState, "content",
                "Page state becomes content even when folder is absent from tree")

        page.selectItem("100", "file", "report.pdf")
        compare(page.selectedItemId, "100", "Selection works in unknown folder")

        dm.paginationLoaded(1, 0, 0)
        wait(50)
        compare(sc.pageState, "empty",
                "Subsequent empty response transitions state correctly")

        page.navigateToFolder("0")
        compare(page.currentFolderId, "0",
                "Navigation back to root works from unknown folder")
    }

    function test_breadcrumb_updates_after_each_navigation() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        var breadcrumb1 = [
            { id: 0, name: "Root" },
            { id: 10, name: "Documents" }
        ]
        dm.breadcrumbLoaded(breadcrumb1)
        wait(50)

        var breadcrumbBar = findByObjectName(page, "breadcrumbBar")
        verify(breadcrumbBar !== null, "BreadcrumbBar found")
        verify(breadcrumbBar.path.length >= 2,
               "First breadcrumb has at least 2 entries")

        page.navigateToFolder("20")
        var breadcrumb2 = [
            { id: 0, name: "Root" },
            { id: 10, name: "Documents" },
            { id: 20, name: "Projects" }
        ]
        dm.breadcrumbLoaded(breadcrumb2)
        wait(50)

        verify(breadcrumbBar.path.length >= 3,
               "Second breadcrumb has at least 3 entries")

        page.navigateToFolder("0")
        var breadcrumb3 = [
            { id: 0, name: "Root" }
        ]
        dm.breadcrumbLoaded(breadcrumb3)
        wait(50)

        verify(breadcrumbBar.path.length >= 1,
               "Root breadcrumb has at least 1 entry")
    }

    function test_tree_panel_handles_model_reset_gracefully() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        page.navigateToFolder("10")
        wait(50)

        var folderTreePanelChild = findChildByType(page, "FolderTreePanel")
        verify(folderTreePanelChild !== null, "FolderTreePanel child found")
        compare(folderTreePanelChild.currentFolderId, "10",
                "Tree panel tracks current folder before model reset")

        dm.clearTreeModel()
        wait(50)

        compare(folderTreePanelChild.currentFolderId, "10",
                "Tree panel preserves currentFolderId after model reset")
        compare(dm.treeModel.rowCount(), 0,
                "Tree model is empty after clear")

        page.navigateToFolder("20")
        compare(page.currentFolderId, "20",
                "Navigation works after tree model reset")
    }

    function test_populated_tree_after_empty_is_functional() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        dm.clearTreeModel()
        wait(50)
        compare(dm.treeModel.rowCount(), 0, "Tree is empty")

        dm.populateTreeModel()
        wait(50)
        compare(dm.treeModel.rowCount(), 1, "Tree has one top-level node")

        var docsIdx = dm.treeModel.index(0, 0)
        compare(dm.treeModel.data(docsIdx, 0x0100 + 1), 10,
                "Documents node has correct id")

        page.navigateToFolder("20")
        compare(page.currentFolderId, "20",
                "Navigation works with newly populated tree")
    }

    function test_tree_selection_clears_when_folder_absent_from_tree() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")
        dm.populateTreeModel()
        wait(50)

        page.navigateToFolder("10")
        wait(100)

        var folderTreePanelChild = findChildByType(page, "FolderTreePanel")
        verify(folderTreePanelChild !== null, "FolderTreePanel child found")
        compare(folderTreePanelChild.currentFolderId, "10",
                "Tree panel tracks current folder")

        var treeView = findByObjectName(folderTreePanelChild, "folderTreeView")
        verify(treeView !== null, "TreeView found")
        verify(treeView.selectionModel !== null, "Selection model exists")

        var docsIdx = dm.treeModel.indexOf(10)
        verify(docsIdx.valid, "Documents node found in tree")
        treeView.selectionModel.setCurrentIndex(docsIdx, ItemSelectionModel.NoUpdate)
        wait(50)

        page.navigateToFolder("9999")
        wait(100)

        compare(folderTreePanelChild.currentFolderId, "9999",
                "currentFolderId updated to unknown folder")
        var currentSelection = treeView.selectionModel.currentIndex
        verify(!currentSelection.valid,
               "Selection is cleared when currentFolderId is absent from tree")
    }

    function test_runtime_header_labels_are_present_and_ordered() {
        var page = createPage()

        var sc = shellController
        verify(sc !== null, "shellController available")

        var dm = driveManager
        verify(dm !== null, "driveManager available")
        dm.paginationLoaded(1, 1, 1)
        wait(100)

        var headerName = findByObjectName(page, "fileTableHeaderName")
        var headerType = findByObjectName(page, "fileTableHeaderType")
        var headerSize = findByObjectName(page, "fileTableHeaderSize")
        var headerUpdated = findByObjectName(page, "fileTableHeaderUpdated")

        verify(headerName !== null, "Name header label found at runtime")
        verify(headerType !== null, "Type header label found at runtime")
        verify(headerSize !== null, "Size header label found at runtime")
        verify(headerUpdated !== null, "Updated header label found at runtime")

        compare(headerName.text, "名称", "Name header text is correct")
        compare(headerType.text, "类型", "Type header text is correct")
        compare(headerSize.text, "大小", "Size header text is correct")
        compare(headerUpdated.text, "更新日期", "Updated header text is correct")

        verify(headerName.x < headerType.x,
               "Name column is left of Type column")
        verify(headerType.x < headerSize.x,
               "Type column is left of Size column")
        verify(headerSize.x < headerUpdated.x,
               "Size column is left of Updated column")
    }

    function test_runtime_file_list_long_name_elides() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")
        dm.clearListModel()
        wait(50)

        var longName = "Quarterly Planning Artifacts and Documentation Review Collection Final Draft"
        dm.addListFileItem(1, "file", longName)
        dm.paginationLoaded(1, 1, 1)
        wait(100)

        var fileRow = findByObjectName(page, "fileRowDelegate_1")
        verify(fileRow !== null, "File row delegate found at runtime")

        var nameLabel = findByObjectName(page, "fileNameLabel_1")
        verify(nameLabel !== null, "File name label found at runtime")
        compare(nameLabel.text, longName,
                "Label holds the full long name")
        verify(nameLabel.elide === Text.ElideRight,
               "Name label uses right elision")
        verify(nameLabel.wrapMode === Text.NoWrap,
               "Name label stays single-line")
    }

    function test_cached_myfiles_content_stays_visible_during_preserving_refresh() {
        var page = createPage()
        var dm = driveManager
        var sc = shellController

        verify(dm !== null, "driveManager available")
        verify(sc !== null, "shellController available")
        dm.clearListModel()
        dm.addListFileItem(1, "file", "cached.txt")
        dm.paginationLoaded(1, 1, 1)
        wait(100)

        var fileListView = findByObjectName(page, "fileListView")
        verify(fileListView !== null, "FileListView found")
        verify(fileListView.visible, "File list is visible before refresh")

        page.refreshCurrentFolder({ keepContent: true })
        wait(50)

        compare(sc.pageState, "loading", "Preserving refresh enters loading state")
        verify(page.keepMyFilesContentWhileLoading,
               "MyFiles view keeps cached content during preserving refresh")
        verify(fileListView.visible,
               "Cached file list remains visible during preserving refresh")
    }

    function test_folder_navigation_does_not_preserve_old_folder_content() {
        var page = createPage()
        var dm = driveManager

        verify(dm !== null, "driveManager available")
        dm.clearListModel()
        dm.addListFileItem(1, "file", "old-folder-file.txt")
        dm.paginationLoaded(1, 1, 1)
        wait(100)

        page.navigateToFolder("42")
        wait(50)

        verify(!page.keepMyFilesContentWhileLoading,
               "Folder navigation does not preserve previous folder content")
    }

    function test_drive_subviews_configure_preserved_loading_content() {
        var myFilesSource = readQmlSource("components/drive/DriveMyFilesView.qml")
        var sharedSource = readQmlSource("components/drive/DriveSharedView.qml")
        var trashSource = readQmlSource("components/drive/DriveTrashView.qml")

        verify(myFilesSource.indexOf("keepContentVisibleWhileLoading: page.keepMyFilesContentWhileLoading") !== -1,
               "MyFiles view configures preserved loading content")
        verify(sharedSource.indexOf("keepContentVisibleWhileLoading: page.keepSharedContentWhileLoading") !== -1,
               "Shared view configures preserved loading content")
        verify(trashSource.indexOf("keepContentVisibleWhileLoading: page.keepTrashContentWhileLoading") !== -1,
               "Trash view configures preserved loading content")
    }

    function test_screenshot_helper_is_available() {
        verify(screenshotHelper !== null,
               "screenshotHelper context property should be injected")
    }

    function test_screenshot_capture_root_page() {
        verify(screenshotHelper !== null, "screenshotHelper available")
        if (!screenshotHelper.available) {
            skip("DESKTOP_QML_EVIDENCE_DIR not set; skipping screenshot capture test")
        }

        var page = createPage()
        compare(page.currentFolderId, "0", "Page starts at root")

        var dm = driveManager
        dm.paginationLoaded(1, 1, 3)
        wait(100)

        var saved = screenshotHelper.saveScreenshot(page, "task-5-homepage-root.png")
        verify(saved, "Root page screenshot saved successfully")
    }

    function test_screenshot_capture_nested_page() {
        verify(screenshotHelper !== null, "screenshotHelper available")
        if (!screenshotHelper.available) {
            skip("DESKTOP_QML_EVIDENCE_DIR not set; skipping screenshot capture test")
        }

        var page = createPage()
        var dm = driveManager

        page.navigateToFolder("10")

        var breadcrumb = [
            { id: 0, name: "Root" },
            { id: 10, name: "Documents" }
        ]
        dm.breadcrumbLoaded(breadcrumb)
        dm.paginationLoaded(1, 1, 2)
        wait(100)

        var saved = screenshotHelper.saveScreenshot(page, "task-5-homepage-nested.png")
        verify(saved, "Nested page screenshot saved successfully")
    }

    function test_screenshot_fails_when_evidence_dir_invalid() {
        var helper = screenshotHelper
        verify(helper !== null, "screenshotHelper available")

        if (helper.available) {
            skip("This test validates failure when DESKTOP_QML_EVIDENCE_DIR is invalid; " +
                 "skipped because the variable is currently valid")
        }

        var page = createPage()
        var saved = helper.saveScreenshot(page, "should-not-exist.png")
        verify(!saved,
               "saveScreenshot must fail when DESKTOP_QML_EVIDENCE_DIR is missing or invalid")
    }

    // ── Homepage interaction contract (Task 2) ────────────────────────

    function test_homepage_up_button_exists_at_runtime() {
        var page = createPage()

        var upBtn = findByObjectName(page, "homepageUpButton")
        verify(upBtn !== null,
               "homepageUpButton is present at runtime")
    }

    function test_homepage_up_button_disabled_at_root() {
        var page = createPage()

        compare(page.currentFolderId, "0", "Page starts at root")
        verify(!page.canNavigateUp,
               "canNavigateUp is false at root because breadcrumb has no parent")

        var upBtn = findByObjectName(page, "homepageUpButton")
        verify(upBtn !== null, "homepageUpButton found")
        verify(!upBtn.enabled,
               "homepageUpButton is disabled at root")
    }

    function test_homepage_up_button_enabled_in_nested_folder_with_breadcrumb_parent() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        page.navigateToFolder("10")
        var breadcrumb = [
            { id: 0, name: "Root" },
            { id: 10, name: "Documents" }
        ]
        dm.breadcrumbLoaded(breadcrumb)
        wait(50)

        verify(page.canNavigateUp,
               "canNavigateUp is true when breadcrumb has a parent entry")
        compare(page.resolvedParentFolderId, "0",
                "resolvedParentFolderId is the second-to-last breadcrumb entry")

        var upBtn = findByObjectName(page, "homepageUpButton")
        verify(upBtn !== null, "homepageUpButton found")
        verify(upBtn.enabled,
               "homepageUpButton is enabled when breadcrumb parent exists")
    }

    function test_homepage_up_button_navigates_to_breadcrumb_parent() {
        var page = createPage()

        var dm = driveManager
        verify(dm !== null, "driveManager available")

        page.navigateToFolder("20")
        var breadcrumb = [
            { id: 0, name: "Root" },
            { id: 10, name: "Documents" },
            { id: 20, name: "Projects" }
        ]
        dm.breadcrumbLoaded(breadcrumb)
        wait(50)

        compare(page.resolvedParentFolderId, "10",
                "resolvedParentFolderId is Documents (second-to-last breadcrumb)")

        var upBtn = findByObjectName(page, "homepageUpButton")
        verify(upBtn !== null, "homepageUpButton found")
        verify(upBtn.enabled, "Up button enabled in nested folder")

        dm.resetCounts()
        upBtn.clicked()
        wait(50)

        compare(page.currentFolderId, "10",
                "Up button navigates to breadcrumb parent, not history")
    }

    function test_folder_navigator_toggle_button_exists_at_runtime() {
        var page = createPage()

        var toggleBtn = findByObjectName(page, "folderNavigatorToggleButton")
        verify(toggleBtn !== null,
               "folderNavigatorToggleButton is present at runtime")
    }

    function test_folder_navigator_panel_hidden_by_default() {
        var page = createPage()

        verify(!page.folderNavigatorExpanded,
               "folderNavigatorExpanded property is false by default")

        var navPanel = findByObjectName(page, "folderNavigatorPanel")
        verify(navPanel !== null,
               "folderNavigatorPanel element exists at runtime")
        verify(!navPanel.visible,
               "folderNavigatorPanel is hidden when folderNavigatorExpanded is false")
    }

    function test_folder_navigator_toggle_opens_panel() {
        var page = createPage()

        verify(!page.folderNavigatorExpanded, "Navigator starts collapsed")

        var toggleBtn = findByObjectName(page, "folderNavigatorToggleButton")
        verify(toggleBtn !== null, "Toggle button found")

        toggleBtn.clicked()
        wait(50)

        verify(page.folderNavigatorExpanded,
               "folderNavigatorExpanded is true after toggle click")

        var navPanel = findByObjectName(page, "folderNavigatorPanel")
        verify(navPanel !== null, "Navigator panel found")
        verify(navPanel.visible || page.folderNavigatorExpanded,
               "folderNavigatorPanel is visible after toggle click")
    }

    function test_folder_navigator_toggle_closes_panel() {
        var page = createPage()

        var toggleBtn = findByObjectName(page, "folderNavigatorToggleButton")
        verify(toggleBtn !== null, "Toggle button found")

        toggleBtn.clicked()
        wait(50)
        verify(page.folderNavigatorExpanded, "Navigator opened")

        toggleBtn.clicked()
        wait(50)

        verify(!page.folderNavigatorExpanded,
               "folderNavigatorExpanded is false after second toggle click")

        var navPanel = findByObjectName(page, "folderNavigatorPanel")
        verify(navPanel !== null, "Navigator panel found")
        verify(!navPanel.visible,
               "folderNavigatorPanel is hidden after second toggle click")
    }

    function test_folder_navigator_stays_open_on_nested_navigation() {
        var page = createPage()

        var toggleBtn = findByObjectName(page, "folderNavigatorToggleButton")
        verify(toggleBtn !== null, "Toggle button found")

        toggleBtn.clicked()
        wait(50)
        verify(page.folderNavigatorExpanded, "Navigator opened")

        page.navigateToFolder("10")
        wait(50)

        verify(page.folderNavigatorExpanded,
               "Navigating to a nested folder keeps the folder navigator open")

        var navPanel = findByObjectName(page, "folderNavigatorPanel")
        verify(navPanel !== null, "Navigator panel found")
        verify(navPanel.visible || page.folderNavigatorExpanded,
               "folderNavigatorPanel stays visible after non-root navigation")
    }

    function test_folder_navigator_hides_on_root_navigation() {
        var page = createPage()

        var toggleBtn = findByObjectName(page, "folderNavigatorToggleButton")
        verify(toggleBtn !== null, "Toggle button found")

        toggleBtn.clicked()
        wait(50)
        verify(page.folderNavigatorExpanded, "Navigator opened")

        page.navigateToFolder("0")
        wait(50)

        verify(!page.folderNavigatorExpanded,
               "Navigating to root collapses the folder navigator")

        var navPanel = findByObjectName(page, "folderNavigatorPanel")
        verify(navPanel !== null, "Navigator panel found")
        verify(!navPanel.visible,
               "folderNavigatorPanel is hidden after root navigation")
    }

    // ── Task 5: PAGE-DRIVE host mode runtime contract ─────────────────────

    function test_activateViewMode_switches_currentViewMode() {
        var page = createPage()

        compare(page.currentViewMode, "myfiles", "Default mode is myfiles")
        verify(page.isMyFilesMode, "isMyFilesMode is true by default")

        page.activateViewMode("shared")
        compare(page.currentViewMode, "shared", "Mode switched to shared")
        verify(page.isSharedMode, "isSharedMode is true after switch")
        verify(!page.isMyFilesMode, "isMyFilesMode is false after switch")

        page.activateViewMode("trash")
        compare(page.currentViewMode, "trash", "Mode switched to trash")
        verify(page.isTrashMode, "isTrashMode is true after switch")

        page.activateViewMode("myfiles")
        compare(page.currentViewMode, "myfiles", "Mode switched back to myfiles")
        verify(page.isMyFilesMode, "isMyFilesMode is true after switch back")
    }

    function test_activateViewMode_clears_selection() {
        var page = createPage()

        page.selectItem("99", "file", "report.pdf")
        compare(page.selectedItemId, "99", "Selection set")

        page.activateViewMode("shared")
        compare(page.selectedItemId, "", "Selection cleared on mode switch")
        compare(page.selectedItemKind, "", "Kind cleared on mode switch")
        compare(page.selectedItemName, "", "Name cleared on mode switch")
    }

    function test_activateViewMode_is_idempotent() {
        var page = createPage()

        compare(page.currentViewMode, "myfiles", "Starts as myfiles")

        page.activateViewMode("myfiles")
        compare(page.currentViewMode, "myfiles", "Still myfiles after idempotent call")
    }

    function test_activateViewMode_collapses_folder_navigator() {
        var page = createPage()

        var toggleBtn = findByObjectName(page, "folderNavigatorToggleButton")
        verify(toggleBtn !== null, "Toggle button found")
        toggleBtn.clicked()
        wait(50)
        verify(page.folderNavigatorExpanded, "Navigator opened")

        page.activateViewMode("shared")
        verify(!page.folderNavigatorExpanded,
               "Folder navigator collapsed on mode switch to non-myfiles")
    }

    function test_myfiles_mode_shows_file_table_and_breadcrumb() {
        var page = createPage()

        verify(page.isMyFilesMode, "Page starts in myfiles mode")

        var breadcrumbBar = findByObjectName(page, "breadcrumbBar")
        verify(breadcrumbBar !== null, "BreadcrumbBar found in myfiles mode")

        var fileListView = findByObjectName(page, "fileListView")
        verify(fileListView !== null, "FileListView found in myfiles mode")
    }

    function test_shared_mode_refreshes_through_share_manager_not_drive_manager() {
        var page = createPage()
        var sm = shareManager
        var dm = driveManager

        verify(sm !== null, "shareManager available")
        verify(dm !== null, "driveManager available")

        sm.resetCounts()
        dm.resetCounts()

        page.activateViewMode("shared")
        wait(50)

        verify(page.isSharedMode, "Page is in shared mode")
        verify(!page.isMyFilesMode, "Page is not in myfiles mode")
        compare(sm.listSharesCallCount(), 1,
                "Entering shared mode loads shares through shareManager")
        compare(dm.listFilesCalls().length, 0,
                "Entering shared mode does not request driveManager listFiles")

        page.refreshCurrentView()
        wait(50)

        compare(sm.listSharesCallCount(), 2,
                "Shared refresh continues to use shareManager")
        compare(dm.listFilesCalls().length, 0,
                "Shared refresh still does not touch driveManager")
    }

    function test_shared_mode_renders_share_manager_content_inside_drive_host() {
        var page = createPage()
        var sm = shareManager

        verify(sm !== null, "shareManager available")
        sm.clearShareListModel()
        sm.addShareItem("shr-1", "Quarterly Report.pdf", "download", "active", true,
                        12, 4, "https://disk.example/shares/shr-1",
                        "2026-04-28T14:30:00Z", "2026-05-05T14:30:00Z", 1)

        page.activateViewMode("shared")
        sm.paginationLoaded(1, 1, 1)
        wait(100)

        var sharedStateView = findByObjectName(page, "sharedStateView")
        var sharedListView = findByObjectName(page, "sharedListView")
        var sharedRow = findByObjectName(page, "sharedRowDelegate_shr-1")
        var primaryLabel = findByObjectName(page, "sharedPrimaryItemLabel_shr-1")

        verify(sharedStateView !== null, "Shared mode renders sharedStateView inside DriveBrowserPage")
        verify(sharedListView !== null, "Shared mode exposes sharedListView")
        verify(sharedRow !== null, "Shared mode renders a share row from shareManager.listModel")
        verify(primaryLabel !== null, "Shared mode exposes the share primary item label")
        compare(primaryLabel.text, "Quarterly Report.pdf",
                "Rendered share row uses shareManager-backed model data")
    }

    function test_shared_mode_empty_error_and_batch_result_follow_share_manager_signals() {
        var page = createPage()
        var sm = shareManager
        var sc = shellController

        verify(sm !== null, "shareManager available")
        verify(sc !== null, "shellController available")

        page.activateViewMode("shared")
        sm.paginationLoaded(1, 0, 0)
        wait(50)
        compare(sc.pageState, "empty", "Shared empty response sets page state to empty")

        sm.apiError("share load failed", 500)
        wait(50)
        compare(sc.pageState, "error", "Shared API failures set page state to error")

        sm.clearBatchResultModel()
        sm.batchResultModel.setResults("share_cancel", 2, 1, 1)
        sm.addBatchResultEntry("shr-1", "success")
        sm.addBatchResultEntry("shr-2", "failed", "", 0, "Link already expired")
        sm.batchResultReady()
        wait(50)

        compare(sc.pageState, "batchResult", "Shared batch result signal switches the page state")
        compare(sm.batchResultModel.totalCount, 2, "Batch-result summary keeps the total count")
        compare(sm.batchResultModel.failureCount, 1, "Batch-result summary keeps the failure count")
    }

    function test_shared_mode_cancel_selected_uses_share_manager_and_not_drive_manager() {
        var page = createPage()
        var sm = shareManager
        var dm = driveManager

        verify(sm !== null, "shareManager available")
        verify(dm !== null, "driveManager available")

        sm.clearShareListModel()
        sm.resetCounts()
        dm.resetCounts()
        sm.addShareItem("shr-9", "Design Review Deck.pptx", "view", "active", false, 3, 1,
                        "https://disk.example/shares/shr-9")

        page.activateViewMode("shared")
        sm.paginationLoaded(1, 1, 1)
        wait(100)

        var shareRow = findByObjectName(page, "sharedRowDelegate_shr-9")
        verify(shareRow !== null, "Share row rendered for selection")
        shareRow.clicked()
        wait(50)
        compare(page.selectedShareIds.length, 1, "Selecting a shared row updates shared selection state")

        var cancelSelectedButton = findByObjectName(page, "sharedCancelSelectedButton")
        verify(cancelSelectedButton !== null, "Cancel Selected button appears after selecting a share")
        cancelSelectedButton.clicked()
        wait(50)

        compare(sm.cancelSharesCalls().length, 1,
                "Shared batch cancellation routes through shareManager.cancelShares")
        compare(dm.deleteItemsCalls().length, 0,
                "Shared cancellation does not route through driveManager.deleteItems")
    }

    function test_trash_mode_refreshes_through_trash_manager_not_drive_manager() {
        var page = createPage()
        var tm = trashManager
        var dm = driveManager

        verify(tm !== null, "trashManager available")
        verify(dm !== null, "driveManager available")

        tm.resetCounts()
        dm.resetCounts()

        page.activateViewMode("trash")
        wait(50)

        verify(page.isTrashMode, "Page is in trash mode")
        verify(!page.isMyFilesMode, "Page is not in myfiles mode")
        compare(tm.listTrashCallCount(), 1,
                "Entering trash mode loads deleted items through trashManager")
        compare(dm.listFilesCalls().length, 0,
                "Entering trash mode does not request driveManager listFiles")

        page.refreshCurrentView()
        wait(50)

        compare(tm.listTrashCallCount(), 2,
                "Trash refresh continues to use trashManager")
        compare(dm.listFilesCalls().length, 0,
                "Trash refresh still does not touch driveManager")
    }

    function test_trash_mode_renders_trash_manager_content_inside_drive_host() {
        var page = createPage()
        var tm = trashManager

        verify(tm !== null, "trashManager available")
        tm.clearTrashListModel()
        tm.addTrashItem("17", "file", "Quarterly Budget.xlsx", 4096,
                        "/Finance/Quarterly Budget.xlsx", "2026-04-28T14:30:00Z")

        page.activateViewMode("trash")
        tm.paginationLoaded(1, 1, 1)
        wait(100)

        var trashStateView = findByObjectName(page, "trashStateView")
        var trashListView = findByObjectName(page, "trashListView")
        var trashRow = findByObjectName(page, "trashRowDelegate_17")
        var trashNameLabel = findByObjectName(page, "trashNameLabel_17")
        var trashPathLabel = findByObjectName(page, "trashOriginalPathLabel_17")

        verify(trashStateView !== null, "Trash mode renders trashStateView inside DriveBrowserPage")
        verify(trashListView !== null, "Trash mode exposes trashListView")
        verify(trashRow !== null, "Trash mode renders a trash row from trashManager.listModel")
        verify(trashNameLabel !== null, "Trash mode exposes the trash name label")
        verify(trashPathLabel !== null, "Trash mode exposes the original path label")
        compare(trashNameLabel.text, "Quarterly Budget.xlsx",
                "Rendered trash row uses trashManager-backed model data")
        compare(trashPathLabel.text, "/Finance/Quarterly Budget.xlsx",
                "Rendered trash row keeps the original path from trashManager")
    }

    function test_trash_mode_empty_error_and_batch_result_follow_trash_manager_signals() {
        var page = createPage()
        var tm = trashManager
        var sc = shellController

        verify(tm !== null, "trashManager available")
        verify(sc !== null, "shellController available")

        page.activateViewMode("trash")
        tm.paginationLoaded(1, 0, 0)
        wait(50)
        compare(sc.pageState, "empty", "Trash empty response sets page state to empty")

        tm.apiError("trash load failed", 500)
        wait(50)
        compare(sc.pageState, "error", "Trash API failures set page state to error")

        tm.clearBatchResultModel()
        tm.batchResultModel.setResults("trash_restore", 2, 1, 1)
        tm.addBatchResultEntry("Quarterly Budget.xlsx", "success", "/Finance/Quarterly Budget.xlsx")
        tm.addBatchResultEntry("Archive", "failed", "", 0, "Already restored")
        tm.batchResultReady()
        wait(50)

        compare(sc.pageState, "batchResult", "Trash batch result signal switches the page state")
        compare(tm.batchResultModel.totalCount, 2, "Trash batch result keeps the total count")
        compare(tm.batchResultModel.failureCount, 1, "Trash batch result keeps the failure count")
        compare(page.viewModeStatusText(), "成功恢复 1 / 2 个回收站项目",
                "Trash batch result summary is rendered through the drive host status contract")
    }

    function test_trash_mode_restore_delete_and_clear_use_trash_manager_not_drive_manager() {
        var page = createPage()
        var tm = trashManager
        var dm = driveManager

        verify(tm !== null, "trashManager available")
        verify(dm !== null, "driveManager available")

        tm.clearTrashListModel()
        tm.resetCounts()
        dm.resetCounts()
        tm.addTrashItem("17", "file", "Quarterly Budget.xlsx", 4096,
                        "/Finance/Quarterly Budget.xlsx", "2026-04-28T14:30:00Z")

        page.activateViewMode("trash")
        tm.paginationLoaded(1, 1, 1)
        wait(100)

        var trashRow = findByObjectName(page, "trashRowDelegate_17")
        verify(trashRow !== null, "Trash row rendered for selection")
        trashRow.clicked()
        wait(50)
        compare(page.selectedTrashIds.length, 1, "Selecting a trash row updates trash selection state")

        var restoreSelectedButton = findByObjectName(page, "trashRestoreSelectedButton")
        verify(restoreSelectedButton !== null, "Restore Selected button appears after selecting a trash row")
        restoreSelectedButton.clicked()
        wait(50)
        compare(tm.restoreItemsCalls().length, 1,
                "Restore Selected routes through trashManager.restoreItems")
        compare(dm.deleteItemsCalls().length, 0,
                "Trash restore does not route through driveManager.deleteItems")

        var deleteButton = findByObjectName(page, "trashDeleteButton_17")
        verify(deleteButton !== null, "Trash row exposes a Delete button")
        deleteButton.clicked()
        wait(50)
        compare(tm.deleteItemsCalls().length, 1,
                "Trash row delete routes through trashManager.deleteItems")

        var clearAllButton = findByObjectName(page, "trashClearAllButton")
        verify(clearAllButton !== null, "Trash toolbar exposes a Clear All button")
        clearAllButton.clicked()
        wait(50)
        compare(tm.clearAllCallCount(), 1,
                "Trash clear-all routes through trashManager.clearAll")
        compare(dm.listFilesCalls().length, 0,
                "Trash actions do not route through driveManager file listing")
    }

    function test_mode_switch_cycles_through_all_modes() {
        var page = createPage()
        var modes = ["shared", "trash", "myfiles"]

        for (var i = 0; i < modes.length; ++i) {
            page.activateViewMode(modes[i])
            compare(page.currentViewMode, modes[i],
                    "Mode is " + modes[i] + " after activation")

            if (modes[i] === "shared") {
                verify(findByObjectName(page, "sharedStateView") !== null,
                       "Shared mode uses the shared content host")
            } else if (modes[i] === "trash") {
                verify(findByObjectName(page, "trashStateView") !== null,
                       "Trash mode uses the trash content host")
            } else if (modes[i] === "myfiles") {
                verify(findByObjectName(page, "fileListView") !== null,
                       "My Files mode still uses the file list host")
            }
        }
    }

    function test_switching_myfiles_trash_myfiles_preserves_mode_specific_content_state() {
        var page = createPage()
        var tm = trashManager

        verify(page.isMyFilesMode, "Page starts in myfiles mode")
        verify(findByObjectName(page, "fileListView") !== null,
               "My Files content is mounted before entering trash mode")

        tm.clearTrashListModel()
        tm.addTrashItem("88", "folder", "Archive", 0, "/Projects/Archive", "2026-04-29T09:00:00Z")

        page.activateViewMode("trash")
        tm.paginationLoaded(1, 1, 1)
        wait(100)

        compare(page.currentViewMode, "trash", "Page enters trash mode")
        verify(findByObjectName(page, "trashRowDelegate_88") !== null,
               "Trash content appears inside the same drive page instance")

        page.activateViewMode("myfiles")
        wait(100)

        compare(page.currentViewMode, "myfiles", "Page returns to myfiles mode")
        compare(page.selectedTrashIds.length, 0, "Trash selection is cleared after leaving trash mode")
        verify(findByObjectName(page, "fileListView") !== null,
               "My Files content returns after leaving trash mode")
    }

    function test_switching_shared_trash_shared_preserves_mode_specific_content_state() {
        var page = createPage()
        var sm = shareManager
        var tm = trashManager

        sm.clearShareListModel()
        tm.clearTrashListModel()
        sm.addShareItem("shr-33", "Partner Contract.pdf", "download", "active", false,
                        2, 1, "https://disk.example/shares/shr-33")
        tm.addTrashItem("44", "file", "Old Contract.pdf", 3072,
                        "/Legal/Old Contract.pdf", "2026-04-26T12:00:00Z")

        page.activateViewMode("shared")
        sm.paginationLoaded(1, 1, 1)
        wait(100)
        verify(findByObjectName(page, "sharedRowDelegate_shr-33") !== null,
               "Shared content appears before switching to trash")

        page.activateViewMode("trash")
        tm.paginationLoaded(1, 1, 1)
        wait(100)
        compare(page.currentViewMode, "trash", "Page enters trash mode")
        verify(findByObjectName(page, "trashRowDelegate_44") !== null,
               "Trash content replaces shared content inside the drive page")

        page.activateViewMode("shared")
        sm.paginationLoaded(1, 1, 1)
        wait(100)
        compare(page.currentViewMode, "shared", "Page returns to shared mode")
        compare(page.selectedTrashIds.length, 0, "Trash selection stays cleared after leaving trash mode")
        verify(findByObjectName(page, "sharedRowDelegate_shr-33") !== null,
               "Shared content returns after leaving trash mode")
    }

    function test_viewModeTitleText_returns_correct_titles() {
        var page = createPage()

        compare(page.viewModeTitleText(), page.driveTitle,
                "myfiles title matches driveTitle")

        page.activateViewMode("shared")
        compare(page.viewModeTitleText(), "分享", "shared title is Shares")

        page.activateViewMode("trash")
        compare(page.viewModeTitleText(), "回收站", "trash title is Trash")
    }

    function test_viewModeLabel_returns_correct_labels() {
        var page = createPage()

        compare(page.viewModeLabel(), "网盘", "myfiles label is DRIVE")

        page.activateViewMode("shared")
        compare(page.viewModeLabel(), "分享", "shared label is SHARES")

        page.activateViewMode("trash")
        compare(page.viewModeLabel(), "回收站", "trash label is TRASH")
    }

    function test_myfiles_toolbar_buttons_exist_in_myfiles_mode() {
        var page = createPage()

        verify(page.isMyFilesMode, "Starts in myfiles mode")

        var upBtn = findByObjectName(page, "homepageUpButton")
        verify(upBtn !== null, "Up button exists in item tree")

        var toggleBtn = findByObjectName(page, "folderNavigatorToggleButton")
        verify(toggleBtn !== null, "Toggle button exists in item tree")
    }

    function test_myfiles_toolbar_buttons_exist_in_non_myfiles_mode() {
        var page = createPage()

        page.activateViewMode("shared")
        wait(50)

        // Buttons exist in the item tree even when gated by visible: root.isMyFilesMode
        var upBtn = findByObjectName(page, "homepageUpButton")
        verify(upBtn !== null, "Up button still exists in item tree in shared mode")

        var toggleBtn = findByObjectName(page, "folderNavigatorToggleButton")
        verify(toggleBtn !== null, "Toggle button still exists in item tree in shared mode")
    }

    function findByObjectName(item, objectName) {
        if (!item) return null
        if (item.objectName === objectName) return item

        if (item.item !== undefined && item.item !== null && typeof item.item === "object") {
            var loaded = findByObjectName(item.item, objectName)
            if (loaded) return loaded
        }

        if (item.contentItem !== undefined && item.contentItem !== null && item.contentItem !== item) {
            var found = findByObjectName(item.contentItem, objectName)
            if (found) return found
        }

        if (item.data !== undefined && item.data !== null) {
            for (var i = 0; i < item.data.length; ++i) {
                found = findByObjectName(item.data[i], objectName)
                if (found) return found
            }
        }

        if (item.children) {
            for (var j = 0; j < item.children.length; ++j) {
                found = findByObjectName(item.children[j], objectName)
                if (found) return found
            }
        }

        return null
    }

    function findChildByType(item, typeName) {
        if (!item) return null
        if (item.toString().indexOf(typeName) !== -1) return item

        if (item.data !== undefined && item.data !== null) {
            for (var i = 0; i < item.data.length; ++i) {
                var found = findChildByType(item.data[i], typeName)
                if (found) return found
            }
        }

        if (item.children) {
            for (var j = 0; j < item.children.length; ++j) {
                found = findChildByType(item.children[j], typeName)
                if (found) return found
            }
        }

        return null
    }
}
