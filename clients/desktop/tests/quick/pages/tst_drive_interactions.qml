import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopDriveInteractions"
    id: testDriveInteractions

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
            readQmlSource("components/drive/DriveTrashView.qml"),
            readQmlSource("components/drive/DriveSeamView.qml"),
            readQmlSource("components/drive/DriveContextMenu.qml")
        ].join("\n")
    }

    // ── Multi-select state contract ──────────────────────────────────────

    function test_drive_browser_has_multi_select_state() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('property var selectedItemIds: []') !== -1,
               "Has selectedItemIds array property")
        verify(source.indexOf('readonly property bool hasMultiSelection: root.selectedItemIds.length > 1') !== -1,
               "Has hasMultiSelection derived property")
    }

    function test_drive_browser_has_isItemSelected_function() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("function isItemSelected(itemId)") !== -1,
               "Has isItemSelected function")
        verify(source.indexOf("root.selectedItemIds.indexOf(String(itemId") !== -1,
               "isItemSelected checks selectedItemIds array")
    }

    function test_drive_browser_has_toggleItemSelection_function() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("function toggleItemSelection(itemId, itemKind, itemName)") !== -1,
               "Has toggleItemSelection function")
        verify(source.indexOf("var copy = root.selectedItemIds.slice()") !== -1,
               "toggleItemSelection copies the array")
        verify(source.indexOf("copy.splice(index, 1)") !== -1,
               "toggleItemSelection removes item when already selected")
        verify(source.indexOf("copy.push(id)") !== -1,
               "toggleItemSelection adds item when not selected")
    }

    function test_drive_browser_clearSelection_clears_multi_select() {
        var source = readDriveBrowserSource()

        var clearStart = source.indexOf("function clearSelection()")
        verify(clearStart !== -1, "Has clearSelection helper")

        var clearBody = source.substring(clearStart, clearStart + 300)
        verify(clearBody.indexOf('root.selectedItemIds = []') !== -1,
               "clearSelection resets selectedItemIds")
    }

    function test_drive_browser_selectItem_sets_multi_select() {
        var source = readDriveBrowserSource()

        var selectStart = source.indexOf("function selectItem(itemId, itemKind, itemName)")
        verify(selectStart !== -1, "Has selectItem helper")

        var selectBody = source.substring(selectStart, selectStart + 400)
        verify(selectBody.indexOf('root.selectedItemIds = [String(itemId)]') !== -1,
               "selectItem sets selectedItemIds to a single-item array")
    }

    function test_drive_browser_myfiles_delegate_uses_isItemSelected_for_highlight() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("highlighted: page.isItemSelected(model.id)") !== -1,
               "File row delegate uses isItemSelected for highlighted state")
    }

    function test_drive_browser_myfiles_delegate_has_checkbox() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("CheckBox {") !== -1,
               "My Files delegate has a CheckBox")
        verify(source.indexOf("checked: page.isItemSelected(model.id)") !== -1,
               "CheckBox checked state uses isItemSelected")
        verify(source.indexOf("onToggled: page.toggleItemSelection(model.id, model.kind, model.name)") !== -1,
               "CheckBox toggling uses toggleItemSelection")
    }

    function test_drive_browser_multi_select_resets_on_navigation() {
        var source = readDriveBrowserSource()

        var navStart = source.indexOf("function navigateToFolder(folderId)")
        verify(navStart !== -1, "Has navigateToFolder")
        var navBody = source.substring(navStart, navStart + 400)

        verify(navBody.indexOf("refreshCurrentFolder") !== -1,
               "navigateToFolder calls refreshCurrentFolder which clears selection")
    }

    function test_drive_browser_multi_select_resets_on_mode_switch() {
        var source = readDriveBrowserSource()

        var activateStart = source.indexOf("function activateViewMode(mode)")
        verify(activateStart !== -1, "Has activateViewMode")
        var activateBody = source.substring(activateStart, activateStart + 700)

        verify(activateBody.indexOf("clearSelection") !== -1,
               "activateViewMode calls clearSelection which resets multi-select")
    }

    // ── Double-click folder navigate ─────────────────────────────────────

    function test_drive_browser_myfiles_delegate_has_double_click_handler() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("onDoubleClicked:") !== -1,
               "File row delegate has double-click handler")
    }

    function test_drive_browser_myfiles_double_click_navigates_folders() {
        var source = readDriveCompositeSource()

        var dblClickIdx = source.indexOf("onDoubleClicked:")
        verify(dblClickIdx !== -1, "Has onDoubleClicked")
        var dblClickBlock = source.substring(dblClickIdx, dblClickIdx + 300)

        verify(dblClickBlock.indexOf('model.kind === "folder"') !== -1,
               "Double-click checks for folder kind")
        verify(dblClickBlock.indexOf("page.navigateToFolder(model.id)") !== -1,
               "Double-click on folder calls navigateToFolder")
    }

    function test_drive_browser_row_click_still_uses_selectItem() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("onClicked: page.selectItem(model.id, model.kind, model.name)") !== -1,
               "Row click uses selectItem for single-selection")
    }

    // ── Context menu ─────────────────────────────────────────────────────

    function test_drive_browser_has_context_menu_component() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("DriveContextMenu") !== -1,
               "Uses DriveContextMenu component")
        verify(source.indexOf("driveContextMenu") !== -1,
               "Has a driveContextMenu instance")
    }

    function test_drive_browser_context_menu_has_folder_actions() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "contextMenuOpen"') !== -1,
               "Context menu has Open action")
        verify(source.indexOf("page.navigateToFolder(contextMenu.targetItemId)") !== -1,
               "Open action navigates to the folder")
    }

    function test_drive_browser_context_menu_has_file_actions() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "contextMenuDownload"') !== -1,
               "Context menu has Download action")
        verify(source.indexOf("page.openOwnerDownloadFileChooser(contextMenu.targetItemId, contextMenu.targetItemName)") !== -1,
               "Download action opens file chooser")
    }

    function test_drive_browser_context_menu_has_mutation_actions() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "contextMenuRename"') !== -1,
               "Context menu has Rename action")
        verify(source.indexOf('objectName: "contextMenuDelete"') !== -1,
               "Context menu has Delete action")
    }

    function test_drive_browser_context_menu_right_click_selects_first() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("acceptedButtons: Qt.RightButton") !== -1,
               "Uses TapHandler with RightButton for context menu trigger")
        verify(source.indexOf("if (!page.isItemSelected(String(model.id)))") !== -1,
               "Right-click selects item first if not already selected")
        verify(source.indexOf("driveContextMenu.popup()") !== -1,
               "Right-click pops up context menu")
    }

    function test_drive_browser_context_menu_gates_folder_and_file_actions() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('visible: contextMenu.targetItemKind === "folder"') !== -1,
               "Open menu item is visible only for folders")
        verify(source.indexOf('visible: contextMenu.targetItemKind === "file"') !== -1,
               "Download menu item is visible only for files")
    }

    // ── Search ───────────────────────────────────────────────────────────

    function test_drive_browser_has_search_state() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('property string searchQuery: ""') !== -1,
               "Has searchQuery property")
        verify(source.indexOf("readonly property bool isSearchActive: root.searchQuery !==") !== -1,
               "Has isSearchActive derived property")
    }

    function test_drive_browser_has_search_functions() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("function submitSearch()") !== -1,
               "Has submitSearch function")
        verify(source.indexOf("function clearSearch()") !== -1,
               "Has clearSearch function")
    }

    function test_drive_browser_submitSearch_calls_refreshCurrentFolder() {
        var source = readDriveBrowserSource()

        var searchStart = source.indexOf("function submitSearch()")
        verify(searchStart !== -1, "Has submitSearch")
        var searchBody = source.substring(searchStart, searchStart + 300)

        verify(searchBody.indexOf("refreshCurrentFolder") !== -1,
               "submitSearch calls refreshCurrentFolder")
    }

    function test_drive_browser_clearSearch_resets_query() {
        var source = readDriveBrowserSource()

        var clearStart = source.indexOf("function clearSearch()")
        verify(clearStart !== -1, "Has clearSearch")
        var clearBody = source.substring(clearStart, clearStart + 200)

        verify(clearBody.indexOf('root.searchQuery = ""') !== -1,
               "clearSearch resets searchQuery")
        verify(clearBody.indexOf("refreshCurrentFolder") !== -1,
               "clearSearch triggers folder refresh")
    }

    function test_drive_browser_refreshCurrentFolder_uses_search_when_active() {
        var source = readDriveBrowserSource()

        var refreshStart = source.indexOf("function refreshCurrentFolder()")
        verify(refreshStart !== -1, "Has refreshCurrentFolder")
        var refreshBody = source.substring(refreshStart, refreshStart + 500)

        verify(refreshBody.indexOf("isSearchActive") !== -1,
               "refreshCurrentFolder checks isSearchActive")
        verify(refreshBody.indexOf("driveManager.searchFiles(root.searchQuery)") !== -1,
               "Uses searchFiles when search is active")
        verify(refreshBody.indexOf("driveManager.listFiles(currentFolderId, 1, 50, root.currentSort)") !== -1,
               "Uses listFiles with sort when search is not active")
    }

    function test_drive_browser_myfiles_has_search_field() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "driveSearchField"') !== -1,
               "Has driveSearchField TextField")
        verify(source.indexOf('objectName: "driveSearchButton"') !== -1,
               "Has driveSearchButton")
        verify(source.indexOf('objectName: "driveClearSearchButton"') !== -1,
               "Has driveClearSearchButton")
        verify(source.indexOf("page.searchQuery = text") !== -1
               || source.indexOf("page.searchQuery = searchField.text") !== -1,
               "Search field updates page searchQuery")
    }

    function test_drive_browser_search_resets_on_navigation() {
        var source = readDriveBrowserSource()

        var navStart = source.indexOf("function navigateToFolder(folderId)")
        verify(navStart !== -1, "Has navigateToFolder")
        var navBody = source.substring(navStart, navStart + 400)

        verify(navBody.indexOf('root.searchQuery = ""') !== -1,
               "navigateToFolder clears searchQuery")
    }

    function test_drive_browser_search_resets_on_mode_switch() {
        var source = readDriveBrowserSource()

        var activateStart = source.indexOf("function activateViewMode(mode)")
        verify(activateStart !== -1, "Has activateViewMode")
        var activateBody = source.substring(activateStart, activateStart + 700)

        verify(activateBody.indexOf('root.searchQuery = ""') !== -1,
               "activateViewMode clears searchQuery")
    }

    // ── Sort ─────────────────────────────────────────────────────────────

    function test_drive_browser_has_sort_state() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('property string currentSort: "name_asc"') !== -1,
               "Has currentSort property initialized to name_asc")
    }

    function test_drive_browser_has_applySort_function() {
        var source = readDriveBrowserSource()

        var sortStart = source.indexOf("function applySort(sortKey)")
        verify(sortStart !== -1, "Has applySort function")
        var sortBody = source.substring(sortStart, sortStart + 200)

        verify(sortBody.indexOf("root.currentSort = String(sortKey") !== -1,
               "applySort sets currentSort")
        verify(sortBody.indexOf("refreshCurrentFolder") !== -1,
               "applySort triggers folder refresh")
    }

    function test_drive_browser_myfiles_has_sort_combo() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "driveSortCombo"') !== -1,
               "Has driveSortCombo ComboBox")
        verify(source.indexOf("onActivated: function(index)") !== -1,
               "Sort combo has onActivated handler")
        verify(source.indexOf("page.applySort(currentValue)") !== -1,
               "Sort combo calls applySort on selection")
    }

    function test_drive_browser_refreshCurrentFolder_passes_sort_to_listFiles() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("driveManager.listFiles(currentFolderId, 1, 50, root.currentSort)") !== -1,
               "refreshCurrentFolder passes currentSort to listFiles")
    }

    // ── View layout toggle ───────────────────────────────────────────────

    function test_drive_browser_has_view_layout_state() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('property string currentViewLayout: "list"') !== -1,
               "Has currentViewLayout property initialized to list")
    }

    function test_drive_browser_has_toggleViewLayout_function() {
        var source = readDriveBrowserSource()

        var toggleStart = source.indexOf("function toggleViewLayout()")
        verify(toggleStart !== -1, "Has toggleViewLayout function")
        var toggleBody = source.substring(toggleStart, toggleStart + 200)

        verify(toggleBody.indexOf('"list"') !== -1 && toggleBody.indexOf('"grid"') !== -1,
               "toggleViewLayout toggles between list and grid")
    }

    function test_drive_browser_myfiles_has_view_toggle() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "driveViewToggle"') !== -1,
               "Has driveViewToggle button")
        verify(source.indexOf("page.toggleViewLayout()") !== -1,
               "View toggle calls toggleViewLayout")
    }

    function test_drive_browser_myfiles_hides_columns_in_grid_mode() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('visible: page.currentViewLayout === "list"') !== -1,
               "Table columns have visibility gated by currentViewLayout")
    }

    // ── Toast notification ───────────────────────────────────────────────

    function test_drive_browser_has_toast_state() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('property string toastMessage: ""') !== -1,
               "Has toastMessage property")
        verify(source.indexOf("property bool toastVisible: false") !== -1,
               "Has toastVisible property")
    }

    function test_drive_browser_has_toast_functions() {
        var source = readDriveBrowserSource()

        verify(source.indexOf("function showToast(message)") !== -1,
               "Has showToast function")
        verify(source.indexOf("function hideToast()") !== -1,
               "Has hideToast function")
        verify(source.indexOf("toastDismissTimer.restart()") !== -1,
               "showToast restarts the dismiss timer")
    }

    function test_drive_browser_has_toast_overlay() {
        var source = readDriveBrowserSource()

        verify(source.indexOf('objectName: "driveToast"') !== -1,
               "Has driveToast overlay")
        verify(source.indexOf("toastDismissTimer") !== -1,
               "Has toast dismiss timer")
        verify(source.indexOf("interval: 3000") !== -1,
               "Toast dismiss timer has 3 second interval")
    }

    function test_drive_browser_finishMutationSuccess_shows_toast() {
        var source = readDriveBrowserSource()

        var finishStart = source.indexOf("function finishMutationSuccess()")
        verify(finishStart !== -1, "Has finishMutationSuccess")
        var finishBody = source.substring(finishStart, finishStart + 600)

        verify(finishBody.indexOf("showToast") !== -1,
               "finishMutationSuccess shows toast notifications")
        verify(finishBody.indexOf('"create"') !== -1,
               "Shows toast for create mutation")
        verify(finishBody.indexOf('"rename"') !== -1,
               "Shows toast for rename mutation")
        verify(finishBody.indexOf('"delete"') !== -1,
               "Shows toast for delete mutation")
    }

    // ── Search/sort row gating ───────────────────────────────────────────

    function test_drive_browser_search_sort_row_gated_by_myfiles() {
        var source = readDriveCompositeSource()

        verify(source.indexOf('objectName: "driveSearchSortRow"') !== -1,
               "Has driveSearchSortRow")
        var sortRowIdx = source.indexOf('objectName: "driveSearchSortRow"')
        var sortRowBlock = source.substring(sortRowIdx, sortRowIdx + 200)
        verify(sortRowBlock.indexOf("visible: page.isMyFilesMode") !== -1,
               "Search/sort row is gated by isMyFilesMode")
    }

    // ── Mode boundary enforcement ────────────────────────────────────────

    function test_drive_browser_shared_mode_not_affected_by_myfiles_features() {
        var source = readDriveBrowserSource()

        // refreshSharedList should NOT reference search or sort
        var sharedStart = source.indexOf("function refreshSharedList()")
        verify(sharedStart !== -1, "Has refreshSharedList")
        var sharedBody = source.substring(sharedStart, sharedStart + 200)

        verify(sharedBody.indexOf("searchQuery") === -1,
               "refreshSharedList does not reference searchQuery")
        verify(sharedBody.indexOf("currentSort") === -1,
               "refreshSharedList does not reference currentSort")
    }

    function test_drive_browser_trash_mode_not_affected_by_myfiles_features() {
        var source = readDriveBrowserSource()

        var trashStart = source.indexOf("function refreshTrashList()")
        verify(trashStart !== -1, "Has refreshTrashList")
        var trashBody = source.substring(trashStart, trashStart + 200)

        verify(trashBody.indexOf("searchQuery") === -1,
               "refreshTrashList does not reference searchQuery")
        verify(trashBody.indexOf("currentSort") === -1,
               "refreshTrashList does not reference currentSort")
    }

    // ── Header multi-select count ────────────────────────────────────────

    function test_drive_browser_myfiles_header_shows_multi_select_count() {
        var source = readDriveCompositeSource()

        verify(source.indexOf("page.selectedItemIds.length > 1") !== -1,
               "Header shows count when multiple items selected")
        verify(source.indexOf('page.selectedItemIds.length + " selected"') !== -1,
               "Header displays the selected count")
    }
}
