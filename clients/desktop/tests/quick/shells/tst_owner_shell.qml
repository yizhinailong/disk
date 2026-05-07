import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopOwnerShell"
    id: testOwnerShell
    when: windowShown

    width: 1280
    height: 900

    property var _created: []

    function _tryInjectStubs() {
        var s = null
        try { s = _q_quicktest_setup } catch (e) {}
        if (!s) {
            try { s = setup } catch (e2) {}
        }
        if (s && typeof s.inject === "function") {
            s.inject()
        }
    }

    function initTestCase() {
        _tryInjectStubs()
    }

    function cleanup() {
        for (var i = 0; i < _created.length; ++i) {
            if (_created[i]) {
                _created[i].destroy()
            }
        }

        _created = []
        shellController.setPageState("loading")
    }

    function readSource(relPath) {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/" + relPath), false)
        xhr.send()
        verify(xhr.responseText.length > 0, relPath + " was read")
        return xhr.responseText
    }

    function readDriveCompositeSource() {
        return [
            readSource("pages/DriveBrowserPage.qml"),
            readSource("components/drive/DriveToolbarCard.qml"),
            readSource("components/drive/DriveStatusCard.qml"),
            readSource("components/drive/DriveMyFilesView.qml"),
            readSource("components/drive/DriveSharedView.qml"),
            readSource("components/drive/DriveTrashView.qml"),
            readSource("components/drive/DriveSeamView.qml")
        ].join("\n")
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

    function sourceUrl(relPath) {
        var base = Qt.resolvedUrl(".").toString()
        return normalizeFileUrl(base + "../../../qml/" + relPath)
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

    function createOwnerShell() {
        var component = loadComponent("shells/OwnerShell.qml")
        var shell = component.createObject(testOwnerShell, {
            visible: true,
            x: 0,
            y: 0
        })
        verify(shell !== null, "OwnerShell instance created")
        _created.push(shell)
        wait(100)
        return shell
    }

    function createCompressedOwnerShell() {
        var component = loadComponent("shells/OwnerShell.qml")
        var shell = component.createObject(testOwnerShell, {
            visible: true,
            x: 0,
            y: 0,
            width: 1024,
            height: 640
        })
        verify(shell !== null, "Compressed OwnerShell instance created")
        _created.push(shell)
        wait(100)
        return shell
    }

    function findByObjectName(item, objectName) {
        if (!item) {
            return null
        }
        if (item.objectName === objectName) {
            return item
        }

        if (item.item !== undefined && item.item !== null && typeof item.item === "object") {
            var found = findByObjectName(item.item, objectName)
            if (found) {
                return found
            }
        }

        if (item.contentItem !== undefined && item.contentItem !== null && item.contentItem !== item) {
            found = findByObjectName(item.contentItem, objectName)
            if (found) {
                return found
            }
        }

        if (item.data !== undefined && item.data !== null) {
            for (var dataIndex = 0; dataIndex < item.data.length; ++dataIndex) {
                found = findByObjectName(item.data[dataIndex], objectName)
                if (found) {
                    return found
                }
            }
        }

        if (item.children) {
            for (var childIndex = 0; childIndex < item.children.length; ++childIndex) {
                found = findByObjectName(item.children[childIndex], objectName)
                if (found) {
                    return found
                }
            }
        }

        return null
    }

    function waitForObject(item, objectName) {
        var found = null
        tryVerify(function() {
            found = findByObjectName(item, objectName)
            return found !== null
        }, 1000, objectName + " becomes available")
        return found
    }

    function clickCenter(item) {
        mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton)
        wait(80)
    }

    function waitForDriveMode(stackView, expectedMode) {
        tryVerify(function() {
            return stackView.currentItem !== null
                    && stackView.currentItem.currentViewMode === expectedMode
        }, 1000, "Drive host currentViewMode becomes " + expectedMode)
    }

    // ── Top-level route contract (DOC-01 §2.1, DOC-03 §2.2) ───────────────
    // Owner shell has exactly three top-level StackView destinations:
    //   PAGE-DRIVE, PAGE-TRANSFER, PAGE-SETTINGS.
    // Shares and Trash are VIEW-SHARED / VIEW-TRASH inside PAGE-DRIVE.

    function test_owner_shell_source_has_grouped_sidebar_buttons() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf("File Views") !== -1, "Has file-view group label")
        verify(source.indexOf("Independent Pages") !== -1, "Has independent-page group label")
        verify(source.indexOf("My Files") !== -1, "Has My Files button")
        verify(source.indexOf("Recent") !== -1, "Has Recent placeholder button")
        verify(source.indexOf("Recent (coming soon)") === -1, "Old long Recent label removed")
        verify(source.indexOf('statusBadge: "Soon"') !== -1, "Placeholder items have explicit Soon badge metadata")
        verify(source.indexOf("Transfers") !== -1, "Has Transfers button")
        verify(source.indexOf("Shares") !== -1, "Has Shares button")
        verify(source.indexOf("Trash") !== -1, "Has Trash button")
        verify(source.indexOf("Favorites") !== -1, "Has Favorites placeholder button")
        verify(source.indexOf("Favorites (coming soon)") === -1, "Old long Favorites label removed")
        verify(source.indexOf("Settings") !== -1, "Has Settings button")
        verify(source.indexOf("Logout") !== -1, "Has Logout button")
    }

    function test_owner_shell_runtime_has_distinct_navigation_groups() {
        var shell = createOwnerShell()

        var fileViewGroup = waitForObject(shell, "ownerFileViewGroup")
        var independentPageGroup = waitForObject(shell, "ownerIndependentPageGroup")

        verify(fileViewGroup !== null, "File-view group exists at runtime")
        verify(independentPageGroup !== null, "Independent-page group exists at runtime")
        verify(fileViewGroup !== independentPageGroup, "Groups are distinct sections")

        verify(waitForObject(shell, "ownerNavMyFilesButton") !== null, "My Files button is in the shell")
        verify(waitForObject(shell, "ownerNavRecentButton") !== null, "Recent button is in the shell")
        verify(waitForObject(shell, "ownerNavSharesButton") !== null, "Shares button is in the shell")
        verify(waitForObject(shell, "ownerNavTrashButton") !== null, "Trash button is in the shell")
        verify(waitForObject(shell, "ownerNavFavoritesButton") !== null, "Favorites button is in the shell")
        verify(waitForObject(shell, "ownerNavTransfersButton") !== null, "Transfers button is in the shell")
        verify(waitForObject(shell, "ownerNavSettingsButton") !== null, "Settings button is in the shell")
    }

    function test_owner_shell_sidebar_has_stable_runtime_selectors() {
        var shell = createCompressedOwnerShell()

        // Navigation panel (pre-existing, verify still present)
        var navPanel = waitForObject(shell, "ownerNavigationPanel")
        verify(navPanel !== null, "ownerNavigationPanel exists")

        // Storage card
        var storageCard = waitForObject(shell, "ownerStorageCard")
        verify(storageCard !== null, "ownerStorageCard exists")

        // Session card
        var sessionCard = waitForObject(shell, "ownerSessionCard")
        verify(sessionCard !== null, "ownerSessionCard exists")

        // All three must be distinct
        verify(navPanel !== storageCard, "Nav panel and storage card are distinct")
        verify(storageCard !== sessionCard, "Storage card and session card are distinct")
    }

    function test_owner_shell_nav_panel_has_scrollview() {
        var shell = createOwnerShell()
        var scrollView = waitForObject(shell, "ownerNavigationScrollView")
        verify(scrollView !== null, "Navigation ScrollView exists")
    }

    function test_owner_shell_nav_panel_encloses_all_buttons_at_default_height() {
        var shell = createOwnerShell()
        var navPanel = waitForObject(shell, "ownerNavigationPanel")

        var navButtons = [
            "ownerNavMyFilesButton", "ownerNavRecentButton",
            "ownerNavSharesButton", "ownerNavTrashButton",
            "ownerNavFavoritesButton", "ownerNavTransfersButton",
            "ownerNavSettingsButton"
        ]

        for (var i = 0; i < navButtons.length; ++i) {
            var btn = waitForObject(shell, navButtons[i])
            verify(btn !== null, navButtons[i] + " found")
            var btnPos = btn.mapToItem(navPanel, 0, 0)
            verify(btnPos.y >= 0, navButtons[i] + " within nav panel top")
            verify(btnPos.y + btn.height <= navPanel.height, navButtons[i] + " within nav panel bottom")
        }
    }

    function test_owner_shell_bottom_cards_pinned_at_compressed_height() {
        var shell = createCompressedOwnerShell()
        var storageCard = waitForObject(shell, "ownerStorageCard")
        var sessionCard = waitForObject(shell, "ownerSessionCard")
        verify(storageCard.visible, "Storage card visible")
        verify(sessionCard.visible, "Session card visible")
    }

    function test_owner_shell_placeholders_have_short_labels_and_remain_disabled() {
        var shell = createOwnerShell()
        var recentButton = waitForObject(shell, "ownerNavRecentButton")
        var favoritesButton = waitForObject(shell, "ownerNavFavoritesButton")

        verify(recentButton !== null, "Recent button exists")
        verify(favoritesButton !== null, "Favorites button exists")
        compare(recentButton.enabled, false, "Recent remains disabled")
        compare(favoritesButton.enabled, false, "Favorites remains disabled")

        // Verify short labels (no "(coming soon)" suffix)
        var recentText = recentButton.text
        var favoritesText = favoritesButton.text
        verify(recentText.indexOf("(coming soon)") === -1, "Recent label has no (coming soon)")
        verify(favoritesText.indexOf("(coming soon)") === -1, "Favorites label has no (coming soon)")
    }

    function test_owner_shell_recent_and_favorites_are_disabled_placeholders() {
        var shell = createOwnerShell()
        var recentButton = waitForObject(shell, "ownerNavRecentButton")
        var favoritesButton = waitForObject(shell, "ownerNavFavoritesButton")
        var stackView = waitForObject(shell, "ownerStackView")

        verify(recentButton !== null, "Recent placeholder button exists")
        verify(favoritesButton !== null, "Favorites placeholder button exists")
        compare(recentButton.enabled, false, "Recent is disabled until its drive mode is implemented")
        compare(favoritesButton.enabled, false, "Favorites is disabled until its drive mode is implemented")
        compare(shell.activeDestination, "drive", "Shell remains on drive by default")
        compare(shell.activeDriveViewMode, "myfiles", "Shell remains on My Files by default")
        compare(stackView.depth, 1, "Placeholder entries do not add stack depth")
    }

    function test_owner_shell_has_exactly_three_top_level_page_components() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf("id: driveBrowserPageComponent") !== -1,
               "Has driveBrowserPageComponent (PAGE-DRIVE)")
        verify(source.indexOf("id: transferCenterPageComponent") !== -1,
               "Has transferCenterPageComponent (PAGE-TRANSFER)")
        verify(source.indexOf("id: settingsPageComponent") !== -1,
               "Has settingsPageComponent (PAGE-SETTINGS)")

        verify(source.indexOf("id: shareManagementPageComponent") === -1,
               "ShareManagementPage is NOT a top-level StackView component (now VIEW-SHARED)")
        verify(source.indexOf("id: trashPageComponent") === -1,
               "TrashPage is NOT a top-level StackView component (now VIEW-TRASH)")
    }

    function test_owner_shell_transfers_and_settings_use_showPage() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf("root.showPage(transferCenterPageComponent)") !== -1,
               "Transfers uses showPage → stackView.replace (FLOW-PAGE-SWITCH)")
        verify(source.indexOf("root.showPage(settingsPageComponent)") !== -1,
               "Settings uses showPage → stackView.replace (FLOW-PAGE-SWITCH)")
    }

    function test_owner_shell_live_file_category_buttons_use_showDriveViewMode() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf('root.showDriveViewMode("myfiles")') !== -1,
               "My Files button routes through showDriveViewMode('myfiles')")
        verify(source.indexOf('root.showDriveViewMode("shared")') !== -1,
               "Shares button routes through showDriveViewMode('shared')")
        verify(source.indexOf('root.showDriveViewMode("trash")') !== -1,
               "Trash button routes through showDriveViewMode('trash')")
        verify(source.indexOf('root.showDriveViewMode("recent")') === -1,
               "Recent is not routed as a live drive mode yet")
        verify(source.indexOf('root.showDriveViewMode("favorites")') === -1,
               "Favorites is not routed as a live drive mode yet")
    }

    function test_owner_shell_live_file_view_group_reuses_drive_host_for_supported_modes() {
        var shell = createOwnerShell()
        var stackView = waitForObject(shell, "ownerStackView")
        var sequence = [
            ["ownerNavSharesButton", "shared"],
            ["ownerNavTrashButton", "trash"],
            ["ownerNavMyFilesButton", "myfiles"]
        ]

        compare(stackView.depth, 1, "Drive host starts as a single StackView entry")

        for (var index = 0; index < sequence.length; ++index) {
            var button = waitForObject(shell, sequence[index][0])
            clickCenter(button)

            compare(shell.activeDestination, "drive",
                    sequence[index][1] + " keeps the top-level destination on drive")
            compare(shell.activeDriveViewMode, sequence[index][1],
                    sequence[index][1] + " updates activeDriveViewMode")
            compare(stackView.depth, 1,
                    sequence[index][1] + " does not grow StackView depth")
            waitForDriveMode(stackView, sequence[index][1])
        }
    }

    function test_owner_shell_drive_host_identity_preserved_across_view_mode_switches() {
        var shell = createOwnerShell()
        var stackView = waitForObject(shell, "ownerStackView")

        var initialItem = stackView.currentItem
        verify(initialItem !== null, "Initial drive host exists")

        var sequence = [
            ["ownerNavSharesButton", "shared"],
            ["ownerNavTrashButton", "trash"],
            ["ownerNavMyFilesButton", "myfiles"],
            ["ownerNavSharesButton", "shared"],
            ["ownerNavTrashButton", "trash"],
            ["ownerNavMyFilesButton", "myfiles"]
        ]

        for (var index = 0; index < sequence.length; ++index) {
            var button = waitForObject(shell, sequence[index][0])
            clickCenter(button)
            waitForDriveMode(stackView, sequence[index][1])

            verify(stackView.currentItem === initialItem,
                   sequence[index][1] + " preserves the same drive host instance (step " + index + ")")
            compare(stackView.depth, 1,
                    sequence[index][1] + " keeps stack depth at 1")
        }
    }

    function test_owner_shell_drive_host_replaced_when_entering_from_transfers() {
        var shell = createOwnerShell()
        var stackView = waitForObject(shell, "ownerStackView")
        var transfersButton = waitForObject(shell, "ownerNavTransfersButton")
        var sharesButton = waitForObject(shell, "ownerNavSharesButton")

        var originalDriveHost = stackView.currentItem
        verify(originalDriveHost !== null, "Initial drive host exists")

        clickCenter(transfersButton)
        compare(shell.activeDestination, "transfers", "Entered transfers page")
        compare(typeof stackView.currentItem.currentViewMode, "undefined",
                "Transfers page is not a drive host")

        clickCenter(sharesButton)
        waitForDriveMode(stackView, "shared")
        compare(shell.activeDestination, "drive", "Returned to drive destination")
        verify(stackView.currentItem !== originalDriveHost,
               "Drive host is a fresh instance after re-entering from Transfers")

        // Once mounted, the new host should be reused for subsequent switches
        var secondDriveHost = stackView.currentItem
        var myFilesButton = waitForObject(shell, "ownerNavMyFilesButton")
        clickCenter(myFilesButton)
        waitForDriveMode(stackView, "myfiles")
        verify(stackView.currentItem === secondDriveHost,
               "Second drive host is reused after re-entry from Transfers")
    }

    function test_owner_shell_drive_host_replaced_when_entering_from_settings() {
        var shell = createOwnerShell()
        var stackView = waitForObject(shell, "ownerStackView")
        var settingsButton = waitForObject(shell, "ownerNavSettingsButton")
        var myFilesButton = waitForObject(shell, "ownerNavMyFilesButton")

        var originalDriveHost = stackView.currentItem

        clickCenter(settingsButton)
        compare(shell.activeDestination, "settings", "Entered settings page")

        clickCenter(myFilesButton)
        waitForDriveMode(stackView, "myfiles")
        compare(shell.activeDestination, "drive", "Returned to drive destination")
        verify(stackView.currentItem !== originalDriveHost,
               "Drive host is a fresh instance after re-entering from Settings")

        var secondDriveHost = stackView.currentItem
        var trashButton = waitForObject(shell, "ownerNavTrashButton")
        clickCenter(trashButton)
        waitForDriveMode(stackView, "trash")
        verify(stackView.currentItem === secondDriveHost,
               "Second drive host is reused after re-entry from Settings")
    }

    function test_owner_shell_switches_myfiles_shared_myfiles_with_one_drive_host_and_content_swap() {
        var shell = createOwnerShell()
        var stackView = waitForObject(shell, "ownerStackView")
        var sharesButton = waitForObject(shell, "ownerNavSharesButton")
        var myFilesButton = waitForObject(shell, "ownerNavMyFilesButton")

        shareManager.clearShareListModel()
        shareManager.addShareItem("shr-shell-1", "Release Checklist.md", "download", "active", false,
                                  8, 2, "https://disk.example/shares/shr-shell-1")

        compare(shell.activeDestination, "drive", "Shell starts in drive host")
        compare(stackView.depth, 1, "Drive host starts as one StackView entry")
        verify(findByObjectName(stackView.currentItem, "fileListView") !== null,
               "My Files content is mounted in the drive host by default")

        clickCenter(sharesButton)
        waitForDriveMode(stackView, "shared")
        shareManager.paginationLoaded(1, 1, 1)
        wait(100)

        compare(shell.activeDestination, "drive", "Shared keeps the top-level destination on drive")
        compare(stackView.depth, 1, "Shared still uses the single drive host")
        verify(findByObjectName(stackView.currentItem, "sharedStateView") !== null,
               "Shared content renders inside the drive host")
        verify(findByObjectName(stackView.currentItem, "sharedRowDelegate_shr-shell-1") !== null,
               "Shared content uses the shareManager-backed row delegate")

        clickCenter(myFilesButton)
        waitForDriveMode(stackView, "myfiles")

        compare(shell.activeDestination, "drive", "Switching back stays inside the drive host")
        compare(stackView.depth, 1, "Switching back to My Files keeps one drive host")
        verify(findByObjectName(stackView.currentItem, "fileListView") !== null,
               "My Files content returns after leaving shared mode")
    }

    function test_owner_shell_switches_myfiles_trash_myfiles_with_one_drive_host_and_content_swap() {
        var shell = createOwnerShell()
        var stackView = waitForObject(shell, "ownerStackView")
        var trashButton = waitForObject(shell, "ownerNavTrashButton")
        var myFilesButton = waitForObject(shell, "ownerNavMyFilesButton")

        trashManager.clearTrashListModel()
        trashManager.addTrashItem("trash-shell-1", "file", "Archive.zip", 8192,
                                  "/Backups/Archive.zip", "2026-04-28T10:15:00Z")

        compare(shell.activeDestination, "drive", "Shell starts in drive host")
        compare(stackView.depth, 1, "Drive host starts as one StackView entry")
        verify(findByObjectName(stackView.currentItem, "fileListView") !== null,
               "My Files content is mounted in the drive host by default")

        clickCenter(trashButton)
        waitForDriveMode(stackView, "trash")
        trashManager.paginationLoaded(1, 1, 1)
        wait(100)

        compare(shell.activeDestination, "drive", "Trash keeps the top-level destination on drive")
        compare(stackView.depth, 1, "Trash still uses the single drive host")
        verify(findByObjectName(stackView.currentItem, "trashStateView") !== null,
               "Trash content renders inside the drive host")
        verify(findByObjectName(stackView.currentItem, "trashRowDelegate_trash-shell-1") !== null,
               "Trash content uses the trashManager-backed row delegate")

        clickCenter(myFilesButton)
        waitForDriveMode(stackView, "myfiles")

        compare(shell.activeDestination, "drive", "Switching back stays inside the drive host")
        compare(stackView.depth, 1, "Switching back to My Files keeps one drive host")
        verify(findByObjectName(stackView.currentItem, "fileListView") !== null,
               "My Files content returns after leaving trash mode")
    }

    function test_owner_shell_switches_shared_trash_shared_with_one_drive_host_and_content_swap() {
        var shell = createOwnerShell()
        var stackView = waitForObject(shell, "ownerStackView")
        var sharesButton = waitForObject(shell, "ownerNavSharesButton")
        var trashButton = waitForObject(shell, "ownerNavTrashButton")

        shareManager.clearShareListModel()
        shareManager.addShareItem("shr-shell-2", "Roadmap.md", "view", "active", false,
                                  5, 1, "https://disk.example/shares/shr-shell-2")
        trashManager.clearTrashListModel()
        trashManager.addTrashItem("trash-shell-2", "folder", "Roadmap Archive", 0,
                                  "/Strategy/Roadmap Archive", "2026-04-27T17:45:00Z")

        compare(shell.activeDestination, "drive", "Shell starts in drive host")
        compare(stackView.depth, 1, "Drive host starts as one StackView entry")

        clickCenter(sharesButton)
        waitForDriveMode(stackView, "shared")
        shareManager.paginationLoaded(1, 1, 1)
        wait(100)

        verify(findByObjectName(stackView.currentItem, "sharedRowDelegate_shr-shell-2") !== null,
               "Shared content renders inside the drive host before switching to trash")

        clickCenter(trashButton)
        waitForDriveMode(stackView, "trash")
        trashManager.paginationLoaded(1, 1, 1)
        wait(100)

        compare(shell.activeDestination, "drive", "Trash keeps the shell on the drive destination")
        compare(stackView.depth, 1, "Switching from Shared to Trash keeps one drive host")
        verify(findByObjectName(stackView.currentItem, "trashRowDelegate_trash-shell-2") !== null,
               "Trash content replaces shared content inside the drive host")

        clickCenter(sharesButton)
        waitForDriveMode(stackView, "shared")
        shareManager.paginationLoaded(1, 1, 1)
        wait(100)

        compare(shell.activeDestination, "drive", "Switching back to Shared keeps the shell on drive")
        compare(stackView.depth, 1, "Switching back to Shared still keeps one drive host")
        verify(findByObjectName(stackView.currentItem, "sharedRowDelegate_shr-shell-2") !== null,
               "Shared content returns after leaving trash mode")
    }

    function test_owner_shell_shares_and_trash_are_not_standalone_pages() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf("root.showPage(shareManagementPageComponent)") === -1,
               "Shares does NOT use showPage (no longer a standalone page)")
        verify(source.indexOf("root.showPage(trashPageComponent)") === -1,
               "Trash does NOT use showPage (no longer a standalone page)")
    }

    function test_owner_shell_no_string_based_navigation() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf('"DriveBrowserPage.qml"') === -1,
               "No string-based DriveBrowserPage navigation")
        verify(source.indexOf('"TransferCenterPage.qml"') === -1,
               "No string-based TransferCenterPage navigation")
        verify(source.indexOf('"SettingsPage.qml"') === -1,
               "No string-based SettingsPage navigation")
    }

    function test_owner_shell_default_page_is_drive_browser() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf("initialItem: DriveBrowserPage") !== -1,
               "Default page is DriveBrowserPage (PAGE-DRIVE)")
    }

    function test_owner_shell_has_stackview() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf("StackView") !== -1, "Has StackView for page navigation")
        verify(source.indexOf("id: stackView") !== -1, "StackView has id")
    }

    function test_owner_shell_independent_pages_replace_stackview_content() {
        var shell = createOwnerShell()
        var stackView = waitForObject(shell, "ownerStackView")
        var transfersButton = waitForObject(shell, "ownerNavTransfersButton")
        var settingsButton = waitForObject(shell, "ownerNavSettingsButton")

        clickCenter(transfersButton)
        compare(shell.activeDestination, "transfers", "Transfers activates its own top-level page")
        compare(shell.activeDriveViewMode, "", "Transfers clears drive view mode")
        compare(stackView.depth, 1, "Transfers still uses replace, not push")
        verify(stackView.currentItem !== null, "Transfers loads a page item")
        compare(typeof stackView.currentItem.currentViewMode, "undefined",
                "Transfers is not a drive-mode host item")

        clickCenter(settingsButton)
        compare(shell.activeDestination, "settings", "Settings activates its own top-level page")
        compare(shell.activeDriveViewMode, "", "Settings clears drive view mode")
        compare(stackView.depth, 1, "Settings also replaces in place")
        verify(stackView.currentItem !== null, "Settings loads a page item")
        compare(typeof stackView.currentItem.currentViewMode, "undefined",
                "Settings is not a drive-mode host item")
    }

    function test_owner_shell_logout_uses_owner_session_path() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf("sessionStore.owner.StartLogout()") !== -1,
               "Logout triggers the owner session logout flow")
        verify(source.indexOf("shellController.navigateToLogin()") === -1,
               "Logout does not bypass the real owner logout flow")
    }

    function test_owner_shell_has_no_header_search_field() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf("Search...") === -1,
               "Owner shell does not expose the out-of-scope search placeholder")
        verify(source.indexOf("TextField {") === -1,
               "Owner shell does not render a search field control")
    }

    function test_owner_shell_has_showPage_and_showDriveViewMode() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf("function showPage(pageComponent)") !== -1,
               "Has showPage function for FLOW-PAGE-SWITCH")
        verify(source.indexOf("function showDriveViewMode(viewMode)") !== -1,
               "Has showDriveViewMode function for FLOW-VIEW-SWITCH")
        verify(source.indexOf("stackView.replace(pageComponent)") !== -1,
               "showPage replaces StackView content")
        verify(source.indexOf("stackView.replace(driveBrowserPageComponent)") !== -1,
               "showDriveViewMode replaces StackView with DriveBrowserPage when entering from non-drive page")
        verify(source.indexOf("typeof stackView.currentItem.activateViewMode === \"function\"") !== -1,
               "showDriveViewMode checks for activateViewMode before deciding to reuse or replace")
    }

    function test_owner_shell_tracks_three_top_level_destinations() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf('property string activeDestination: "drive"') !== -1,
               "Shell default destination is 'drive' (PAGE-DRIVE)")
        verify(source.indexOf('property string activeDriveViewMode: "myfiles"') !== -1,
               "Shell tracks active drive view mode")

        verify(source.indexOf('root.activeDestination = "drive"') !== -1,
               "Sets activeDestination to 'drive'")
        verify(source.indexOf('root.activeDestination = "transfers"') !== -1,
               "Sets activeDestination to 'transfers'")
        verify(source.indexOf('root.activeDestination = "settings"') !== -1,
               "Sets activeDestination to 'settings'")

        verify(source.indexOf('root.activeDestination = "shares"') === -1,
               "activeDestination is NEVER 'shares' (VIEW-SHARED is inside PAGE-DRIVE)")
        verify(source.indexOf('root.activeDestination = "trash"') === -1,
               "activeDestination is NEVER 'trash' (VIEW-TRASH is inside PAGE-DRIVE)")
        verify(source.indexOf('root.activeDestination = "files"') === -1,
               "activeDestination is NEVER 'files' (renamed to 'drive')")
    }

    function test_owner_shell_keeps_drive_specific_chrome_inside_drive_page() {
        var ownerSource = readSource("shells/OwnerShell.qml")
        var driveSource = readDriveCompositeSource()

        verify(ownerSource.indexOf("PageStateView") === -1,
               "Owner shell does not own the drive page state view")
        verify(ownerSource.indexOf("BreadcrumbBar") === -1,
               "Owner shell does not own drive breadcrumbs")
        verify(ownerSource.indexOf("FolderTreePanel") === -1,
               "Owner shell does not own the drive folder tree")
        verify(driveSource.indexOf("PageStateView") !== -1,
               "Drive browser page owns the page state view")
        verify(driveSource.indexOf("BreadcrumbBar") !== -1,
               "Drive browser page owns breadcrumbs")
        verify(driveSource.indexOf("FolderTreePanel") !== -1,
               "Drive browser page owns the folder tree")
    }

    function test_owner_shell_visually_separates_navigation_and_logout_areas() {
        var source = readSource("shells/OwnerShell.qml")

        verify(source.indexOf('text: "Workspace"') !== -1,
               "Brand area has a secondary label")
        verify(source.indexOf('text: "Navigation"') !== -1,
               "Navigation section is explicitly labeled")
        verify(source.indexOf('title: "File Views"') !== -1,
               "File-view section is explicitly labeled")
        verify(source.indexOf('title: "Independent Pages"') !== -1,
               "Independent-page section is explicitly labeled")
        verify(source.indexOf('text: "Session"') !== -1,
               "Logout area is explicitly separated")
    }

    // ── DriveBrowserPage view-mode seam ─────────────────────────────────────

    function test_drive_browser_has_view_mode_seam() {
        var source = readSource("pages/DriveBrowserPage.qml")

        verify(source.indexOf('property string currentViewMode: "myfiles"') !== -1,
               "PAGE-DRIVE has currentViewMode property defaulting to VIEW-MYFILES")
        verify(source.indexOf("function activateViewMode(mode)") !== -1,
               "PAGE-DRIVE has activateViewMode function for VIEW-SWITCH")
    }

    function test_drive_browser_view_mode_seam_is_idempotent() {
        var source = readSource("pages/DriveBrowserPage.qml")

        verify(source.indexOf("if (root.currentViewMode === nextMode)") !== -1,
               "activateViewMode is idempotent: returns early if mode unchanged")
    }

    function test_drive_browser_does_not_use_stackview() {
        var source = readSource("pages/DriveBrowserPage.qml")

        verify(source.indexOf("StackView") === -1,
               "PAGE-DRIVE does not push/pop StackView — folder drill-down is same-page")
        verify(source.indexOf("stackView") === -1,
               "PAGE-DRIVE does not reference stackView")
    }

    function test_drive_browser_does_not_drive_shell_routing() {
        var source = readSource("pages/DriveBrowserPage.qml")

        verify(source.indexOf("showPage(") === -1,
               "PAGE-DRIVE does not call showPage (shell-level routing)")
        verify(source.indexOf("showDriveViewMode(") === -1,
               "PAGE-DRIVE does not call showDriveViewMode (shell-level routing)")
    }
}
