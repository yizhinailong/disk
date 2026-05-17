import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import "../components/admin"

Page {
    id: root
    objectName: "adminPage"
    title: qsTr("管理")

    WorkspaceTheme { id: theme }

    readonly property color pageBackground: theme.pageBackgroundColor

    background: Rectangle {
        color: root.pageBackground
    }

    property string toastMessage: ""
    property bool toastVisible: false

    function showToast(message) {
        root.toastMessage = String(message || "")
        root.toastVisible = true
        toastDismissTimer.restart()
    }

    function hideToast() {
        root.toastVisible = false
        root.toastMessage = ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.pagePadding
        spacing: 12

        TabBar {
            id: tabBar
            Layout.fillWidth: true

            TabButton {
                text: qsTr("用户")
                width: implicitWidth
            }
            TabButton {
                text: qsTr("分享")
                width: implicitWidth
            }
            TabButton {
                text: qsTr("系统")
                width: implicitWidth
            }
        }

        SwipeView {
            id: swipeView
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            UserTab {
            }

            ShareTab {
            }

            SystemTab {
            }
        }
    }

    // Toast notification
    Popup {
        id: toastPopup
        x: (parent.width - width) / 2
        y: parent.height - height - 32
        width: toastLabel.implicitWidth + 32
        height: toastLabel.implicitHeight + 20
        visible: root.toastVisible
        modal: false
        closePolicy: Popup.NoAutoClose
        padding: 0

        background: Rectangle {
            color: "#323232"
            radius: theme.innerPanelRadius
        }

        contentItem: Label {
            id: toastLabel
            text: root.toastMessage
            color: "#ffffff"
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Timer {
        id: toastDismissTimer
        interval: 3000
        onTriggered: root.hideToast()
    }

    Connections {
        target: adminManager
        ignoreUnknownSignals: true

        function onApiError(message, code) {
            root.showToast(message)
        }

        function onOperationSuccess(message) {
            root.showToast(message)
        }
    }
}
