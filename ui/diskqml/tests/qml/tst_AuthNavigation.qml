import QtQuick
import QtQuick.Controls
import QtTest

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
        mainLoader.sourceComponent = appComponent
        mainLoader.active = true
        appWindow = mainLoader.item.item
        verify(appWindow !== null, "App should load")
        
        pageStack = findChildByObjName(appWindow, "pageStack")
        verify(pageStack !== null, "Should find pageStack")
    }

    function cleanup() {
        mainLoader.active = false
        mainLoader.sourceComponent = undefined
        appWindow = null
        pageStack = null
    }

    function test_initialPage() {
        verify(pageStack.currentItem !== null, "Current item should not be null")
        compare(pageStack.depth, 1, "Stack depth should be 1")
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
}
