import QtQuick
import QtQuick.Controls
import QtTest
import DiskAuth 1.0

TestCase {
    name: "AuthNavigationTests"
    when: windowShown

    Component {
        id: appComponent
        Loader {
            source: Qt.resolvedUrl("../../qml/App.qml")
            asynchronous: false
            active: true
        }
    }

    Loader {
        id: mainLoader
        anchors.fill: parent
    }

    property var appWindow: null
    property var pageStack: null

    function findChildByObjName(parent, name) {
        if (!parent) return null;
        if (parent.objectName === name) return parent;
        
        // Special case for ApplicationWindow
        if (parent.contentItem) {
            let found = findChildByObjName(parent.contentItem, name)
            if (found) return found;
        }

        let children = parent.children || []
        for (let i = 0; i < children.length; ++i) {
            let found = findChildByObjName(children[i], name)
            if (found) return found;
        }
        return null;
    }

    function init() {
        // Ensure logged out state before each test
        AuthViewModel.logout()
        AuthViewModel.clearForm()
        
        mainLoader.sourceComponent = appComponent
        mainLoader.active = true
        appWindow = mainLoader.item.item
        verify(appWindow !== null, "App should load")
        
        pageStack = findChildByObjName(appWindow, "pageStack")
        verify(pageStack !== null, "Should find pageStack")
    }

    function cleanup() {
        AuthViewModel.logout()
        AuthViewModel.clearForm()
        mainLoader.active = false
        mainLoader.sourceComponent = undefined
        appWindow = null
        pageStack = null
    }

    function test_initialPage_whenLoggedOut() {
        // When not logged in, should start on LoginView
        verify(!AuthViewModel.isLoggedIn, "Should start logged out")
        verify(pageStack.currentItem !== null, "Current item should not be null")
        compare(pageStack.depth, 1, "Stack depth should be 1")
        
        // Should find createAccountButton (only on LoginView)
        let createAccountBtn = findChildByObjName(pageStack.currentItem, "createAccountButton")
        verify(createAccountBtn !== null, "Should find createAccountButton on LoginView")
    }

    function test_navigationToRegister() {
        let createAccountBtn = findChildByObjName(pageStack.currentItem, "createAccountButton")
        verify(createAccountBtn !== null, "Should find createAccountButton")
        
        mouseClick(createAccountBtn)
        tryCompare(pageStack, "depth", 2)
        verify(pageStack.currentItem !== null, "Current item should not be null after push")
    }

    function test_registerSignalPopsAndPrefills() {
        let createAccountBtn = findChildByObjName(pageStack.currentItem, "createAccountButton")
        verify(createAccountBtn !== null, "Should find createAccountButton in test 2")

        mouseClick(createAccountBtn)
        tryCompare(pageStack, "depth", 2)
        
        let registerView = pageStack.currentItem
        verify(registerView !== null, "Should have RegisterView")
        
        // Simulate 'registered' signal
        registerView.registered("testuser", "test@test.com")
        
        // Should pop back to LoginView
        tryCompare(pageStack, "depth", 1)
        
        // Check prefillAccount on root
        compare(appWindow.prefillAccount, "testuser", "Should prefill account")
    }

    function test_navigationOnLogin() {
        // Start on LoginView
        verify(!AuthViewModel.isLoggedIn, "Should start logged out")
        compare(pageStack.depth, 1, "Stack depth should be 1")
        
        // Simulate login via AuthViewModel
        AuthViewModel.simulateLogin("testuser", "test@example.com")
        
        // Wait for navigation to HomeView
        tryVerify(function() { return AuthViewModel.isLoggedIn })
        tryCompare(pageStack, "depth", 1)
        
        // Should NOT find createAccountButton (we're on HomeView now)
        let createAccountBtn = findChildByObjName(pageStack.currentItem, "createAccountButton")
        verify(createAccountBtn === null, "Should NOT find createAccountButton on HomeView")
    }

    function test_navigationOnLogout() {
        // First login
        AuthViewModel.simulateLogin("testuser", "test@example.com")
        tryVerify(function() { return AuthViewModel.isLoggedIn })
        
        // Now logout
        AuthViewModel.logout()
        
        // Wait for navigation back to LoginView
        tryVerify(function() { return !AuthViewModel.isLoggedIn })
        tryCompare(pageStack, "depth", 1)
        
        // Should find createAccountButton again
        let createAccountBtn = findChildByObjName(pageStack.currentItem, "createAccountButton")
        verify(createAccountBtn !== null, "Should find createAccountButton after logout")
    }

    function test_backNavigationFromRegister() {
        // Navigate to RegisterView
        let createAccountBtn = findChildByObjName(pageStack.currentItem, "createAccountButton")
        mouseClick(createAccountBtn)
        tryCompare(pageStack, "depth", 2)
        
        // Click back button
        let backButton = findChildByObjName(pageStack.currentItem, "backButton")
        if (backButton) {
            mouseClick(backButton)
        } else {
            // Alternative: find back button by text
            let view = pageStack.currentItem
            let buttons = view.children
            for (let i = 0; i < buttons.length; i++) {
                if (buttons[i].text && buttons[i].text.indexOf("返回") >= 0) {
                    mouseClick(buttons[i])
                    break
                }
            }
        }
        
        // Should be back to LoginView
        tryCompare(pageStack, "depth", 1)
        let createAccountBtn2 = findChildByObjName(pageStack.currentItem, "createAccountButton")
        verify(createAccountBtn2 !== null, "Should be back on LoginView")
    }
}
