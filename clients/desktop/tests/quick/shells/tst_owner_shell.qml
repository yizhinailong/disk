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
        // P0 owner shell exposes only approved surface + logout.
        verify(source.indexOf("Files") !== -1, "Has Files button")
        verify(source.indexOf("Transfers") !== -1, "Has Transfers button")
        verify(source.indexOf("Logout") !== -1, "Has Logout button")
        verify(source.indexOf("Shares") === -1, "Does not expose Shares button")
        verify(source.indexOf("Trash") === -1, "Does not expose Trash button")
        verify(source.indexOf("Settings") === -1, "Does not expose Settings button")
    }

    function test_owner_shell_uses_only_p0_component_navigation() {
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
        verify(source.indexOf("root.showPage(shareManagementPageComponent)") === -1,
               "Does not navigate to share management")
        verify(source.indexOf("root.showPage(trashPageComponent)") === -1,
               "Does not navigate to trash")
        verify(source.indexOf("root.showPage(settingsPageComponent)") === -1,
               "Does not navigate to settings")
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

    function test_owner_shell_defines_only_p0_page_components() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/OwnerShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "OwnerShell.qml was read")
        verify(source.indexOf("id: driveBrowserPageComponent") !== -1, "Has driveBrowserPageComponent")
        verify(source.indexOf("id: transferCenterPageComponent") !== -1, "Has transferCenterPageComponent")
        verify(source.indexOf("id: shareManagementPageComponent") === -1, "Has no shareManagementPageComponent")
        verify(source.indexOf("id: trashPageComponent") === -1, "Has no trashPageComponent")
        verify(source.indexOf("id: settingsPageComponent") === -1, "Has no settingsPageComponent")
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
        verify(source.indexOf("function showPage(pageComponent)") !== -1,
               "Has showPage function taking a component parameter")
        verify(source.indexOf("stackView.replace(pageComponent)") !== -1,
               "showPage replaces StackView content")
    }
}
