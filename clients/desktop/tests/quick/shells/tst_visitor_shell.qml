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

    function test_visitor_shell_navigates_on_pageState_not_shellChange() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("onPageStateChanged") !== -1,
               "Navigates based on pageStateChanged, not currentShellChanged")
        verify(source.indexOf('onCurrentShellChanged') === -1,
               "Does NOT navigate on currentShellChanged (waits for session state)")
    }

    function test_visitor_shell_navigates_on_content_and_verify_states() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf('"content"') !== -1,
               "Checks for content pageState before navigating to browse")
        verify(source.indexOf('"verify"') !== -1,
               "Checks for verify pageState before navigating back to verify page")
        verify(source.indexOf("onPageStateChanged") !== -1,
               "Uses pageStateChanged as the navigation trigger")
    }

    function test_visitor_shell_initial_page_is_share_verify() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("ShareVerifyPage") !== -1,
               "Has ShareVerifyPage as initial content")
        verify(source.indexOf("initialItem: shareVerifyPageComponent") !== -1,
               "StackView initialItem is shareVerifyPageComponent wrapping ShareVerifyPage")
    }

    function test_visitor_shell_verify_page_uses_shareId_binding() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")

        var componentStart = source.indexOf("id: shareVerifyPageComponent")
        verify(componentStart !== -1, "Found shareVerifyPageComponent")

        var componentEnd = source.indexOf("Component {", componentStart + 1)
        if (componentEnd === -1) componentEnd = source.length
        var componentBlock = source.substring(componentStart, componentEnd)

        verify(componentBlock.indexOf("shareId: root.activeShareId") !== -1,
               "ShareVerifyPage shareId uses binding to root.activeShareId (not imperative assignment)")
    }

    function test_visitor_shell_navigates_back_to_verify_via_component() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf('stackView.replace(null, shareVerifyPageComponent)') !== -1,
               "Navigates back to verify page via shareVerifyPageComponent on verify pageState")
    }

    function test_visitor_shell_no_imperative_shareId_assignment() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("verifyPage.shareId =") === -1,
               "Does NOT imperatively assign shareId in Component.onCompleted (uses binding instead)")
    }

    function test_visitor_shell_has_active_share_id_property() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("property string activeShareId") !== -1,
               "Has activeShareId property")
        verify(source.indexOf("sessionStore.visitor.shareId") !== -1,
               "activeShareId is bound to sessionStore.visitor.shareId")
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
        verify(source.indexOf("onPageStateChanged") !== -1,
               "Listens to pageStateChanged signal")
    }

    // ── Task 11: Visitor route guard source contracts ──────────────────────

    function test_visitor_shell_does_not_reference_owner_session() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("sessionStore.owner") === -1,
               "VisitorShell never references owner session (domain isolation)")
        verify(source.indexOf("StartLogout") === -1,
               "VisitorShell never triggers owner logout")
    }

    function test_visitor_shell_does_not_reference_owner_navigation() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("activeDestination") === -1,
               "VisitorShell has no activeDestination (owner-only navigation)")
        verify(source.indexOf("showPage(") === -1,
               "VisitorShell never calls showPage (owner-only routing)")
        verify(source.indexOf("showDriveViewMode(") === -1,
               "VisitorShell never calls showDriveViewMode (owner-only drive modes)")
    }

    function test_visitor_shell_only_navigates_between_verify_and_browse() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/VisitorShell.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "VisitorShell.qml was read")
        verify(source.indexOf("ShareVerifyPage") !== -1,
               "VisitorShell has ShareVerifyPage")
        verify(source.indexOf("ShareBrowsePage") !== -1,
               "VisitorShell has ShareBrowsePage")
        verify(source.indexOf("stackView.replace") !== -1,
               "VisitorShell uses stackView.replace for page transitions")
    }

    function test_session_store_activate_visitor_does_not_close_owner() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../src/auth/SessionStore.cpp"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "SessionStore.cpp was read")
        verify(source.indexOf("ActivateVisitor") !== -1, "Has ActivateVisitor")

        var activateVisitorStart = source.indexOf("void SessionStore::ActivateVisitor")
        verify(activateVisitorStart !== -1, "Found ActivateVisitor method")

        var nextMethod = source.indexOf("void SessionStore::", activateVisitorStart + 1)
        var methodBody = nextMethod > 0
            ? source.substring(activateVisitorStart, nextMethod)
            : source.substring(activateVisitorStart)

        verify(methodBody.indexOf("CloseShare") === -1,
               "ActivateVisitor does not call CloseShare on visitor (owner remains)")
        verify(methodBody.indexOf("StartLogout") === -1,
               "ActivateVisitor does not call StartLogout on owner")
    }

    // ── Visitor share-access flow: ShareVerifyPage drives state machine ────

    function test_share_verify_drives_visitor_session_not_auth_service() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/ShareVerifyPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShareVerifyPage.qml was read")
        verify(source.indexOf("sessionStore.visitor.StartVerify") !== -1,
               "ShareVerifyPage calls sessionStore.visitor.StartVerify (not authService.AccessShare)")
        verify(source.indexOf("authService.AccessShare") === -1,
               "ShareVerifyPage does NOT call authService.AccessShare directly")
    }

    function test_share_verify_activates_visitor_for_new_share_id() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/ShareVerifyPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShareVerifyPage.qml was read")
        verify(source.indexOf("sessionStore.ActivateVisitor") !== -1,
               "ShareVerifyPage calls ActivateVisitor for user-entered share IDs")
    }

    function test_share_verify_listens_to_auth_failure_for_error_display() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/ShareVerifyPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShareVerifyPage.qml was read")
        verify(source.indexOf("onShareAccessFailure") !== -1,
               "ShareVerifyPage listens to authService.shareAccessFailure for error display")
    }

    function test_share_verify_does_not_navigate_on_success() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/ShareVerifyPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShareVerifyPage.qml was read")
        verify(source.indexOf("onShareAccessSuccess") === -1,
               "ShareVerifyPage does NOT handle shareAccessSuccess (navigation driven by state machine)")
        verify(source.indexOf("navigateToVisitor") === -1,
               "ShareVerifyPage does NOT call navigateToVisitor directly")
    }

    function test_share_verify_auto_starts_on_shareId_changed() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/ShareVerifyPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShareVerifyPage.qml was read")
        verify(source.indexOf("onShareIdChanged") !== -1,
               "ShareVerifyPage uses onShareIdChanged for auto-verify (deterministic, not Component.onCompleted)")
        verify(source.indexOf("sessionStore.visitor.StartVerify()") !== -1,
               "Auto-calls StartVerify when shareId is set via binding")
    }

    function test_share_verify_no_component_onCompleted_auto_verify() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/ShareVerifyPage.qml"), false)
        xhr.send()
        var source = xhr.responseText

        verify(source.length > 0, "ShareVerifyPage.qml was read")

        var onCompletedIdx = source.indexOf("Component.onCompleted")
        if (onCompletedIdx !== -1) {
            var nextBrace = source.indexOf("}", onCompletedIdx)
            var block = source.substring(onCompletedIdx, nextBrace > 0 ? nextBrace : source.length)
            verify(block.indexOf("StartVerify") === -1,
                   "Component.onCompleted does NOT call StartVerify (uses onShareIdChanged instead)")
        }
    }
}
