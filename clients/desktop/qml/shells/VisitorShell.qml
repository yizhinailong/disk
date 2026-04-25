import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../pages"

ApplicationWindow {
    id: root
    visible: true
    width: 800
    height: 600
    title: "Disk Share"

    property string activeShareId: ""

    StackView {
        id: stackView
        anchors.fill: parent

        initialItem: ShareVerifyPage {
            id: verifyPage

            Component.onCompleted: {
                verifyPage.shareId = root.activeShareId
            }
        }
    }

    Connections {
        target: shellController

        function onCurrentShellChanged() {
            if (shellController.currentShell === "visitor") {
                stackView.replace(null, shareBrowsePageComponent, { shareId: root.activeShareId })
            }
        }
    }

    Component {
        id: shareBrowsePageComponent
        ShareBrowsePage {}
    }
}
