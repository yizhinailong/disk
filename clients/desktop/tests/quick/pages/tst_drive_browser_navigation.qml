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

        return prefix + stack.join("/")
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

        compare(headerName.text, "Name", "Name header text is correct")
        compare(headerType.text, "Type", "Type header text is correct")
        compare(headerSize.text, "Size", "Size header text is correct")
        compare(headerUpdated.text, "Updated", "Updated header text is correct")

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

    function findByObjectName(item, objectName) {
        if (!item) return null
        if (item.objectName === objectName) return item

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
