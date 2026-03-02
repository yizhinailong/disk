import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Disk 1.0
import "views"

ApplicationWindow {
    id: root
    visible: true
    width: 640
    height: 480
    title: qsTr("Disk - 云盘客户端")

    Material.theme: Material.Light
    Material.accent: "#2196F3"

    property string prefillAccount: ""

    StackView {
        id: pageStack
        objectName: "pageStack"
        anchors.fill: parent
        initialItem: SessionViewModel.isLoggedIn ? homeView : loginView
    }

    Component {
        id: loginView
        LoginView {
            prefillAccount: root.prefillAccount
            onRegisterRequested: pageStack.push(registerView)
        }
    }

    Component {
        id: registerView
        RegisterView {
            onRegistered: function(username, email) {
                root.prefillAccount = username
                pageStack.pop()
            }
            onBackRequested: pageStack.pop()
        }
    }

    Component {
        id: homeView
        HomePlaceholder {
            onLogoutRequested: {
                pageStack.clear()
                pageStack.push(loginView)
            }
        }
    }

    // Handle login state changes
    Connections {
        target: SessionViewModel
        function onIsLoggedInChanged() {
            if (SessionViewModel.isLoggedIn) {
                pageStack.clear()
                pageStack.push(homeView)
            } else {
                pageStack.clear()
                pageStack.push(loginView)
            }
        }
    }
}
