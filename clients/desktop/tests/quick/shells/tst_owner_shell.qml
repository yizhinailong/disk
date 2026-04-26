import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopOwnerShell"
    id: testOwnerShell

    function test_owner_shell_source_has_sidebar_buttons() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        // Owner shell exposes the approved owner surfaces + logout.
        verify(source.indexOf("Files") !== -1, "Has Files button")
        verify(source.indexOf("Transfers") !== -1, "Has Transfers button")
        verify(source.indexOf("Shares") !== -1, "Has Shares button")
        verify(source.indexOf("Trash") !== -1, "Has Trash button")
        verify(source.indexOf("Settings") !== -1, "Has Settings button")
        verify(source.indexOf("Logout") !== -1, "Has Logout button")
    }

    function test_owner_shell_uses_approved_component_navigation() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")

        // Uses showPage with Components, not string-based page paths
        verify(source.indexOf("root.showPage(driveBrowserPageComponent)") !== -1,
               "Navigates to drive browser via component")
        verify(source.indexOf("root.showPage(transferCenterPageComponent)") !== -1,
               "Navigates to transfer center via component")
        verify(source.indexOf("root.showPage(shareManagementPageComponent)") !== -1,
               "Navigates to share management via component")
        verify(source.indexOf("root.showPage(trashPageComponent)") !== -1,
               "Navigates to trash via component")
        verify(source.indexOf("root.showPage(settingsPageComponent)") !== -1,
               "Navigates to settings via component")
    }

    function test_owner_shell_no_string_based_navigation() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        // Should NOT use string-based page replacement
        verify(source.indexOf('"DriveBrowserPage.qml"') === -1,
               "No string-based DriveBrowserPage navigation")
        verify(source.indexOf('"TransferCenterPage.qml"') === -1,
               "No string-based TransferCenterPage navigation")
        verify(source.indexOf('"ShareManagementPage.qml"') === -1,
               "No string-based ShareManagementPage navigation")
        verify(source.indexOf('"TrashPage.qml"') === -1,
               "No string-based TrashPage navigation")
        verify(source.indexOf('"SettingsPage.qml"') === -1,
               "No string-based SettingsPage navigation")
    }

    function test_owner_shell_defines_approved_page_components() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf("id: driveBrowserPageComponent") !== -1, "Has driveBrowserPageComponent")
        verify(source.indexOf("id: transferCenterPageComponent") !== -1, "Has transferCenterPageComponent")
        verify(source.indexOf("id: shareManagementPageComponent") !== -1, "Has shareManagementPageComponent")
        verify(source.indexOf("id: trashPageComponent") !== -1, "Has trashPageComponent")
        verify(source.indexOf("id: settingsPageComponent") !== -1, "Has settingsPageComponent")
    }

    function test_owner_shell_default_page_is_drive_browser() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        // StackView initialItem should be DriveBrowserPage
        verify(source.indexOf("initialItem: DriveBrowserPage") !== -1,
               "Default page is DriveBrowserPage")
    }

    function test_owner_shell_has_stackview() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf("StackView") !== -1, "Has StackView for page navigation")
        verify(source.indexOf("id: stackView") !== -1, "StackView has id")
    }

    function test_owner_shell_logout_uses_owner_session_path() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf("sessionStore.owner.StartLogout()") !== -1,
               "Logout triggers the owner session logout flow")
        verify(source.indexOf("shellController.navigateToLogin()") === -1,
               "Logout does not bypass the real owner logout flow")
    }

    function test_owner_shell_has_no_header_search_field() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf("Search...") === -1,
               "Owner shell does not expose the out-of-scope search placeholder")
        verify(source.indexOf("TextField {") === -1,
               "Owner shell does not render a search field control")
    }

    function test_owner_shell_has_showPage_function() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf("function destinationForPageComponent(pageComponent)") !== -1,
               "Has a component-to-destination helper for shell-owned labeling")
        verify(source.indexOf("function showPage(pageComponent)") !== -1,
               "Has showPage function taking a component parameter")
        verify(source.indexOf("root.activeDestination = root.destinationForPageComponent(pageComponent)") !== -1,
               "showPage keeps the active destination aligned with the shown component")
        verify(source.indexOf("stackView.replace(pageComponent)") !== -1,
               "showPage replaces StackView content")
    }

    function test_owner_shell_tracks_active_destination_in_shell_state() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf('property string activeDestination: "files"') !== -1,
               "Shell owns the default active destination state")
        verify(source.indexOf('root.activeDestination = "files"') !== -1,
               "Files button updates active destination")
        verify(source.indexOf('root.activeDestination = "transfers"') !== -1,
               "Transfers button updates active destination")
        verify(source.indexOf('root.activeDestination = "shares"') !== -1,
               "Shares button updates active destination")
        verify(source.indexOf('root.activeDestination = "trash"') !== -1,
               "Trash button updates active destination")
        verify(source.indexOf('root.activeDestination = "settings"') !== -1,
               "Settings button updates active destination")
    }

    function test_owner_shell_keeps_drive_specific_chrome_inside_drive_page() {
        var ownerXhr = new XMLHttpRequest()
        ownerXhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        ownerXhr.send()
        var ownerSource = ownerXhr.responseText

        var driveXhr = new XMLHttpRequest()
        driveXhr.open("GET", Qt.resolvedUrl("../../../qml/pages/DriveBrowserPage.qml"), false)
        driveXhr.send()
        var driveSource = driveXhr.responseText

        verify(ownerSource.length > 0, "OwnerShell.qml was read")
        verify(driveSource.length > 0, "DriveBrowserPage.qml was read")
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
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf('text: "Workspace"') !== -1,
               "Brand area has a secondary label")
        verify(source.indexOf('text: "Navigation"') !== -1,
               "Navigation section is explicitly labeled")
        verify(source.indexOf('text: "Session"') !== -1,
               "Logout area is explicitly separated")
    }
}
