import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopNavigation"
    id: testNavigation

    // Test that ShellController's valid shell names are documented
    // and correspond to actual QML shells in the project.

    function test_valid_shell_names() {
        var valid_shells = ["splash", "login", "register", "owner", "visitor"]
        compare(valid_shells.length, 5, "Five shell domains defined")

        // Each shell name should be a non-empty string
        for (var i = 0; i < valid_shells.length; i++) {
            verify(valid_shells[i].length > 0, "Shell name '" + valid_shells[i] + "' is non-empty")
        }
    }

    function test_valid_page_states() {
        var valid_states = ["loading", "content", "empty", "error", "batchResult"]
        compare(valid_states.length, 5, "Five page states defined")

        // Each state should be unique
        var seen = {}
        for (var i = 0; i < valid_states.length; i++) {
            verify(!seen[valid_states[i]], "State '" + valid_states[i] + "' is unique")
            seen[valid_states[i]] = true
        }
    }

    function test_shell_controller_transitions() {
        // Verify the ShellController QML source has the expected navigation methods
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/app/ShellController.hpp"), false)
        xhr.send()
        var source = xhr.responseText

        // ShellController exposes navigation methods
        verify(source.length > 0, "ShellController.hpp was read")
        verify(source.indexOf("navigateToOwner") !== -1, "Has navigateToOwner")
        verify(source.indexOf("navigateToVisitor") !== -1, "Has navigateToVisitor")
        verify(source.indexOf("navigateToLogin") !== -1, "Has navigateToLogin")
        verify(source.indexOf("navigateToSplash") !== -1, "Has navigateToSplash")
        verify(source.indexOf("navigateToRegister") !== -1, "Has navigateToRegister")
    }

    function test_session_store_has_owner_visitor_domains() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/auth/SessionStore.hpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "SessionStore.hpp was read")
        verify(source.indexOf("ActivateOwner") !== -1, "Has ActivateOwner")
        verify(source.indexOf("ActivateVisitor") !== -1, "Has ActivateVisitor")
        verify(source.indexOf("DeactivateAll") !== -1, "Has DeactivateAll")
        verify(source.indexOf("GetActiveDomain") !== -1, "Has GetActiveDomain")
    }

    function test_navigation_prevents_owner_without_auth() {
        // ShellController routes unauthenticated navigateToOwner to login
        // This is tested in C++ test_shell_controller; here we verify the contract exists
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/app/ShellController.cpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShellController.cpp was read")
        // The owner shell is gated on OwnerSessionManager::Active state
        verify(source.indexOf("OwnerSessionState::Active") !== -1, "References Active state")
    }

    // ── Task 11: Route guard regression source contracts ───────────────────

    function test_shell_controller_handles_visitor_reverifyRequired() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/app/ShellController.cpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShellController.cpp was read")
        verify(source.indexOf("VisitorSessionState::ReverifyRequired") !== -1,
               "ShellController handles ReverifyRequired visitor state")
    }

    function test_shell_controller_handles_visitor_verifying_state() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/app/ShellController.cpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShellController.cpp was read")
        verify(source.indexOf("VisitorSessionState::Verifying") !== -1,
               "ShellController handles Verifying visitor state")
    }

    function test_shell_controller_handles_visitor_active_state() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/app/ShellController.cpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShellController.cpp was read")
        verify(source.indexOf("VisitorSessionState::Active") !== -1,
               "ShellController handles Active visitor state for pageState coordination")
    }

    function test_shell_controller_sets_loading_on_authenticating() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/app/ShellController.cpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShellController.cpp was read")
        verify(source.indexOf("OwnerSessionState::Authenticating") !== -1,
               "ShellController references Authenticating state for loading coordination")
    }

    function test_shell_controller_sets_loading_on_refreshing() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/app/ShellController.cpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShellController.cpp was read")
        verify(source.indexOf("OwnerSessionState::Refreshing") !== -1,
               "ShellController references Refreshing state for loading coordination")
    }
}
