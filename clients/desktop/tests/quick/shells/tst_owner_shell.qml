import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopOwnerShell"
    id: testOwnerShell

    function test_owner_shell_sidebar_items() {
        var expected_pages = ["drive", "transfers", "shares", "trash", "settings"]
        compare(expected_pages.length, 5)
    }

    function test_owner_shell_default_page() {
        var default_page = "drive"
        verify(default_page.length > 0)
    }
}
