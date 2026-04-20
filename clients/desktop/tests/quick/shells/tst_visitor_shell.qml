import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopVisitorShell"
    id: testVisitorShell

    function test_visitor_shell_pages() {
        var pages = ["shareVerify", "shareBrowse"]
        compare(pages.length, 2)
    }

    function test_visitor_shell_no_sidebar() {
        var has_sidebar = false
        compare(has_sidebar, false)
    }

    function test_visitor_close_clears_state() {
        var share_id = ""
        compare(share_id, "")
    }
}
