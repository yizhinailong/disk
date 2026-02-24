import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
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

    StackView {
        id: pageStack
        anchors.fill: parent
        initialItem: loginView
    }

    Component {
        id: loginView
        Rectangle {
            color: "transparent"
            Button {
                anchors.centerIn: parent
                text: qsTr("创建账号")
                onClicked: pageStack.push(registerView, { "theme": theme })
            }
        }
    }

    Component {
        id: registerView
        Rectangle {
            color: "transparent"
            // Empty placeholder for RegisterView
        }
    }
}
