import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import "views"
import "styles"

ApplicationWindow {
    id: root
    visible: true
    width: 640
    height: 480
    title: qsTr("Disk - 云盘客户端")

    AppTheme {
        id: theme
    }

    Material.theme: Material.Light
    Material.accent: theme.primary

    property string serverUrl: "http://127.0.0.1:8080"
    property string prefillAccount: ""

    StackView {
        id: pageStack
        objectName: "pageStack"
        anchors.fill: parent
        initialItem: loginView
    }

    Component {
        id: loginView
        LoginView {
            theme: theme
            serverUrl: root.serverUrl
            prefillAccount: root.prefillAccount
            onRegisterRequested: pageStack.push(registerView, { "theme": theme, "serverUrl": root.serverUrl })
        }
    }

    Component {
        id: registerView
        RegisterView {
            theme: theme
            serverUrl: root.serverUrl
            onRegistered: function(username, email) {
                root.prefillAccount = username
                pageStack.pop()
            }
            onBackRequested: pageStack.pop()
        }
    }
}
