import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopRouteTaxonomy"
    id: testRouteTaxonomy

    // ── Target IA contracts (DOC-01 §2.1, §3.2; DOC-03 §2.2, §3.1) ──────
    // These tests assert architectural invariants from the authoritative docs.
    // Task 3 delivers the three-destination route contract:
    //   PAGE-DRIVE, PAGE-TRANSFER, PAGE-SETTINGS as the only top-level pages.
    //   Shares and Trash are VIEW-SHARED / VIEW-TRASH inside PAGE-DRIVE.

    function test_page_drive_is_the_sole_page_for_file_view_modes() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/DriveBrowserPage.qml"), false)
        xhr.send()
        var source = xhr.responseText
        verify(source.length > 0, "DriveBrowserPage.qml was read")

        verify(source.indexOf("currentFolderId") !== -1,
               "PAGE-DRIVE tracks currentFolderId for drive-internal navigation")
        verify(source.indexOf("navigateToFolder(") !== -1,
               "PAGE-DRIVE has navigateToFolder for folder drill-down (FLOW-FOLDER-NAVIGATE)")
    }

    function test_drive_internal_navigation_does_not_use_stackview() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/DriveBrowserPage.qml"), false)
        xhr.send()
        var source = xhr.responseText
        verify(source.length > 0, "DriveBrowserPage.qml was read")

        verify(source.indexOf("StackView") === -1,
               "DriveBrowserPage does not push/pop StackView — folder drill-down is same-page")
        verify(source.indexOf("stackView") === -1,
               "DriveBrowserPage does not reference stackView")
    }

    function test_drive_browser_does_not_drive_shell_routing() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/DriveBrowserPage.qml"), false)
        xhr.send()
        var source = xhr.responseText
        verify(source.length > 0, "DriveBrowserPage.qml was read")

        verify(source.indexOf("showPage(") === -1,
               "DriveBrowserPage does not call showPage (shell-level routing)")
    }

    function test_owner_shell_initial_page_is_drive_browser() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf("initialItem: DriveBrowserPage") !== -1,
               "OwnerShell StackView initialItem is PAGE-DRIVE (DriveBrowserPage)")
    }

    function test_owner_shell_has_stackview_for_page_replacement() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf("StackView") !== -1,
               "OwnerShell has StackView for FLOW-PAGE-SWITCH")
        verify(source.indexOf("stackView.replace(") !== -1,
               "OwnerShell uses stackView.replace() for page switching (DOC-03 §3.2.5)")
    }

    function test_owner_shell_has_exactly_three_stackview_destinations() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        // Three approved page components (DOC-01 §4, DOC-03 §2.2)
        verify(source.indexOf("id: driveBrowserPageComponent") !== -1,
               "PAGE-DRIVE component exists")
        verify(source.indexOf("id: transferCenterPageComponent") !== -1,
               "PAGE-TRANSFER component exists")
        verify(source.indexOf("id: settingsPageComponent") !== -1,
               "PAGE-SETTINGS component exists")

        // Shares and Trash are NOT StackView destinations
        verify(source.indexOf("id: shareManagementPageComponent") === -1,
               "No shareManagementPageComponent (Shares is VIEW-SHARED inside PAGE-DRIVE)")
        verify(source.indexOf("id: trashPageComponent") === -1,
               "No trashPageComponent (Trash is VIEW-TRASH inside PAGE-DRIVE)")
    }

    function test_owner_shell_activeDestination_only_uses_three_values() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        // Default and set values
        verify(source.indexOf('property string activeDestination: "drive"') !== -1,
               "Default activeDestination is 'drive'")
        verify(source.indexOf('root.activeDestination = "drive"') !== -1,
               "Sets activeDestination to 'drive'")
        verify(source.indexOf('root.activeDestination = "transfers"') !== -1,
               "Sets activeDestination to 'transfers'")
        verify(source.indexOf('root.activeDestination = "settings"') !== -1,
               "Sets activeDestination to 'settings'")

        // Forbidden values
        verify(source.indexOf('root.activeDestination = "shares"') === -1,
               "Never sets activeDestination to 'shares' (VIEW-SHARED, not a page)")
        verify(source.indexOf('root.activeDestination = "trash"') === -1,
               "Never sets activeDestination to 'trash' (VIEW-TRASH, not a page)")
        verify(source.indexOf('root.activeDestination = "files"') === -1,
               "Never sets activeDestination to 'files' (renamed to 'drive')")
    }

    function test_owner_shell_live_file_category_entries_use_view_mode_seam() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf('root.showDriveViewMode("myfiles")') !== -1,
               "Files button uses showDriveViewMode (VIEW-MYFILES)")
        verify(source.indexOf('root.showDriveViewMode("shared")') !== -1,
               "Shares button uses showDriveViewMode (VIEW-SHARED)")
        verify(source.indexOf('root.showDriveViewMode("trash")') !== -1,
               "Trash button uses showDriveViewMode (VIEW-TRASH)")
        verify(source.indexOf('root.showDriveViewMode("recent")') === -1,
               "Recent is not exposed as a live view-mode route before implementation")
        verify(source.indexOf('root.showDriveViewMode("favorites")') === -1,
               "Favorites is not exposed as a live view-mode route before implementation")

        // Verify no showPage for shares/trash
        verify(source.indexOf("root.showPage(shareManagementPageComponent)") === -1,
               "Shares does NOT use showPage (no standalone page replacement)")
        verify(source.indexOf("root.showPage(trashPageComponent)") === -1,
               "Trash does NOT use showPage (no standalone page replacement)")
    }

    function test_settings_page_remains_independent_page() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/SettingsPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "SettingsPage.qml was read")
        verify(source.indexOf("profileManager.") !== -1,
               "PAGE-SETTINGS uses profileManager (stays independent per DOC-01 §4.2)")
    }

    function test_transfer_center_remains_independent_page() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/TransferCenterPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "TransferCenterPage.qml was read")
        verify(source.indexOf("transferManager.") !== -1,
               "PAGE-TRANSFER uses transferManager (stays independent per DOC-01 §4.1)")
    }

    function test_visitor_shell_is_navigationally_isolated() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("activeDestination") === -1,
               "VisitorShell has no activeDestination (DOC-01 §2.2.2: navigation isolation)")
        verify(source.indexOf("Sidebar") === -1,
               "VisitorShell has no Sidebar (DOC-03 §3.3)")
    }

    // ── pageState shared-state risk and isolation ─────────────────────────
    // shellController.pageState is a singleton shared across all pages and
    // view modes. This section documents the contract and risk.

    function test_pageState_is_shared_via_shellController() {
        var driveXhr = new XMLHttpRequest()
        driveXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/DriveBrowserPage.qml"), false)
        driveXhr.send()
        var driveSource = driveXhr.responseText
        verify(driveSource.length > 0, "DriveBrowserPage.qml was read")

        verify(driveSource.indexOf("shellController.pageState") !== -1,
               "PAGE-DRIVE reads pageState from shellController (shared singleton)")
    }

    function test_pageState_transitions_cover_five_documented_states() {
        var sc = shellController
        verify(sc !== null, "shellController stub is available")

        var validStates = ["loading", "content", "empty", "error", "batchResult"]
        for (var i = 0; i < validStates.length; i++) {
            sc.setPageState(validStates[i])
            compare(sc.pageState, validStates[i],
                    "pageState transitioned to " + validStates[i] +
                    " without bleed from previous value")
        }
    }

    function test_pageState_isolation_no_stale_state_across_transitions() {
        var sc = shellController
        verify(sc !== null, "shellController stub is available")

        sc.setPageState("content")
        compare(sc.pageState, "content", "Set to content")

        sc.setPageState("loading")
        compare(sc.pageState, "loading",
                "Transition to loading clears previous content state")

        sc.setPageState("error")
        compare(sc.pageState, "error",
                "Transition to error clears previous loading state")

        sc.setPageState("empty")
        compare(sc.pageState, "empty",
                "Transition to empty clears previous error state")

        sc.setPageState("batchResult")
        compare(sc.pageState, "batchResult",
                "Transition to batchResult clears previous empty state")
    }

    // ── Harness seam verification ──────────────────────────────────────────

    function test_stub_shellController_has_currentShell_property() {
        verify(shellController !== null, "shellController stub is available")
        verify(typeof shellController.currentShell === "string",
               "shellController.currentShell is a string property")
    }

    function test_stub_shellController_has_all_route_methods() {
        verify(shellController !== null, "shellController stub is available")
        verify(typeof shellController.navigateToLogin === "function",
               "shellController has navigateToLogin")
        verify(typeof shellController.navigateToRegister === "function",
               "shellController has navigateToRegister")
        verify(typeof shellController.navigateToOwner === "function",
               "shellController has navigateToOwner")
        verify(typeof shellController.navigateToVisitor === "function",
               "shellController has navigateToVisitor")
        verify(typeof shellController.navigateToSplash === "function",
               "shellController has navigateToSplash")
    }

    function test_stub_shellController_can_drive_shell_transitions() {
        verify(shellController !== null, "shellController stub is available")

        shellController.setCurrentShell("owner")
        compare(shellController.currentShell, "owner",
                "setCurrentShell updates currentShell to owner")

        shellController.setCurrentShell("visitor")
        compare(shellController.currentShell, "visitor",
                "setCurrentShell updates currentShell to visitor")

        shellController.setCurrentShell("login")
        compare(shellController.currentShell, "login",
                "setCurrentShell updates currentShell to login")
    }

    function test_stub_profileManager_is_available() {
        verify(profileManager !== null, "profileManager stub is available")
        verify(typeof profileManager.userProfile === "object",
               "profileManager.userProfile is accessible")
        verify(typeof profileManager.storageStats === "object",
               "profileManager.storageStats is accessible")
    }

    function test_stub_profileManager_has_test_seam_methods() {
        verify(profileManager !== null, "profileManager stub is available")
        verify(typeof profileManager.setUserProfile === "function",
               "profileManager has setUserProfile test seam")
        verify(typeof profileManager.setStorageStats === "function",
               "profileManager has setStorageStats test seam")
    }

    function test_stub_shareManager_is_available() {
        verify(shareManager !== null, "shareManager stub is available")
        verify(shareManager.listModel !== null, "shareManager.listModel is accessible")
        verify(shareManager.browseModel !== null, "shareManager.browseModel is accessible")
        verify(shareManager.batchResultModel !== null, "shareManager.batchResultModel is accessible")
    }

    function test_stub_shareManager_has_owner_operations() {
        verify(shareManager !== null, "shareManager stub is available")
        verify(typeof shareManager.listShares === "function", "shareManager has listShares")
        verify(typeof shareManager.createShare === "function", "shareManager has createShare")
        verify(typeof shareManager.cancelShares === "function", "shareManager has cancelShares")
        verify(typeof shareManager.getShareDetail === "function", "shareManager has getShareDetail")
    }

    function test_stub_shareManager_has_visitor_operations() {
        verify(shareManager !== null, "shareManager stub is available")
        verify(typeof shareManager.browseShare === "function", "shareManager has browseShare")
        verify(typeof shareManager.getShareDetailVisitor === "function", "shareManager has getShareDetailVisitor")
    }

    function test_stub_trashManager_is_available() {
        verify(trashManager !== null, "trashManager stub is available")
        verify(trashManager.listModel !== null, "trashManager.listModel is accessible")
        verify(trashManager.batchResultModel !== null, "trashManager.batchResultModel is accessible")
    }

    function test_stub_trashManager_has_all_operations() {
        verify(trashManager !== null, "trashManager stub is available")
        verify(typeof trashManager.listTrash === "function", "trashManager has listTrash")
        verify(typeof trashManager.restoreItems === "function", "trashManager has restoreItems")
        verify(typeof trashManager.deleteItems === "function", "trashManager has deleteItems")
        verify(typeof trashManager.clearAll === "function", "trashManager has clearAll")
    }

    function test_stub_batchResultModel_has_test_seam() {
        verify(shareManager !== null, "shareManager stub is available")
        var brm = shareManager.batchResultModel
        verify(brm !== null, "batchResultModel accessible via shareManager")
        verify(typeof brm.setResults === "function",
               "batchResultModel has setResults test seam")

        brm.setResults("share_cancel", 5, 3, 2)
        compare(brm.operation, "share_cancel", "BatchResult operation set correctly")
        compare(brm.totalCount, 5, "BatchResult totalCount set correctly")
        compare(brm.successCount, 3, "BatchResult successCount set correctly")
        compare(brm.failureCount, 2, "BatchResult failureCount set correctly")
    }

    function test_shell_controller_has_approved_shell_domains() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/app/ShellController.hpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShellController.hpp was read")
        verify(source.indexOf("navigateToOwner") !== -1, "Has owner route")
        verify(source.indexOf("navigateToVisitor") !== -1, "Has visitor route")
        verify(source.indexOf("navigateToLogin") !== -1, "Has login route")
        verify(source.indexOf("navigateToRegister") !== -1, "Has register route")
        verify(source.indexOf("navigateToSplash") !== -1, "Has splash route")
        verify(source.indexOf("setPageState") !== -1, "Has setPageState")
        verify(source.indexOf("currentShell") !== -1, "Has currentShell property")
        verify(source.indexOf("pageState") !== -1, "Has pageState property")
    }

    function test_stub_context_properties_do_not_produce_reference_errors() {
        verify(shellController !== null, "shellController available")
        verify(authService !== null, "authService available")
        verify(sessionStore !== null, "sessionStore available")
        verify(driveManager !== null, "driveManager available")
        verify(profileManager !== null, "profileManager available")
        verify(transferManager !== null, "transferManager available")
        verify(shareManager !== null, "shareManager available")
        verify(trashManager !== null, "trashManager available")
        verify(screenshotHelper !== null, "screenshotHelper available")
    }

}
