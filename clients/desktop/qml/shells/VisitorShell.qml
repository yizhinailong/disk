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

    property string activeShareId: sessionStore.visitor.shareId

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: shareVerifyPageComponent
    }

    Connections {
        target: shellController

        function onPageStateChanged() {
            if (shellController.currentShell !== "visitor") return

            if (shellController.pageState === "content") {
                stackView.replace(null, shareBrowsePageComponent,
                                  { shareId: root.activeShareId })
            } else if (shellController.pageState === "verify") {
                stackView.replace(null, shareVerifyPageComponent)
            }
        }
    }

    Component {
        id: shareVerifyPageComponent
        ShareVerifyPage {
            shareId: root.activeShareId
        }
    }

    Component {
        id: shareBrowsePageComponent
        ShareBrowsePage {}
    }
}
