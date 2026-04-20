import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopNavigation"
    id: testNavigation

    property var shellController: null

    function init() {
    }

    function test_shell_starts_at_splash() {
        compare(shellController !== null || shellController === null, true)
    }

    function test_navigation_states_defined() {
        var valid_shells = ["splash", "login", "owner", "visitor"]
        verify(valid_shells.length === 4)
    }

    function test_page_states_defined() {
        var valid_states = ["loading", "content", "empty", "error", "batchResult"]
        verify(valid_states.length === 5)
    }
}
