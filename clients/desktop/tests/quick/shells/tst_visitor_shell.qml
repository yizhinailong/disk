import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopVisitorShell"
    id: testVisitorShell

    function test_visitor_shell_has_stackview() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("StackView") !== -1, "Has StackView for page navigation")
        verify(source.indexOf("id: stackView") !== -1, "StackView has id")
    }

    function test_visitor_shell_has_share_browse_component() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("id: shareBrowsePageComponent") !== -1,
               "Has shareBrowsePageComponent")
        verify(source.indexOf("ShareBrowsePage") !== -1,
               "References ShareBrowsePage")
    }

    function test_visitor_shell_uses_component_navigation() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("stackView.replace(null, shareBrowsePageComponent") !== -1,
               "Navigates to share browse via component, not string path")
        verify(source.indexOf('"ShareBrowsePage.qml"') === -1,
               "Does NOT use string-based ShareBrowsePage.qml navigation")
    }

    function test_visitor_shell_initial_page_is_share_verify() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("ShareVerifyPage") !== -1,
               "Has ShareVerifyPage as initial content")
        verify(source.indexOf("initialItem: ShareVerifyPage") !== -1,
               "StackView initialItem is ShareVerifyPage")
    }

    function test_visitor_shell_has_active_share_id_property() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("property string activeShareId") !== -1,
               "Has activeShareId property")
    }

    function test_visitor_shell_no_sidebar() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("Sidebar") === -1,
               "Visitor shell has no sidebar")
        verify(source.indexOf("ToolBar") === -1,
               "Visitor shell has no toolbar")
    }

    function test_visitor_shell_listens_to_shell_controller() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("shellController") !== -1,
               "References shellController")
        verify(source.indexOf("onCurrentShellChanged") !== -1,
               "Listens to currentShellChanged signal")
        verify(source.indexOf('"visitor"') !== -1,
               "Checks for visitor shell state")
    }
}
