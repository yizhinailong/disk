import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: sectionRoot

    property string sectionObjectName: ""
    property string title: ""
    property var items: []
    property string activeItemId: ""
    property color titleTextColor: "#6b7785"
    property color activeFillColor: "#dce8f5"
    property color hoverFillColor: "#eef2f6"
    property color activeStripeColor: "#4f6b8a"
    property color activeTextColor: "#1f2933"
    property color idleTextColor: "#6b7785"

    signal itemActivated(string itemId)

    objectName: sectionRoot.sectionObjectName
    spacing: 4

    Label {
        Layout.fillWidth: true
        objectName: sectionRoot.sectionObjectName !== "" ? sectionRoot.sectionObjectName + "Label" : ""
        text: sectionRoot.title
        color: sectionRoot.titleTextColor
        font.pixelSize: 11
        font.bold: true
        leftPadding: 8
        topPadding: 4
        bottomPadding: 8
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: sectionRoot.items

        delegate: OwnerSidebarNavButton {
            Layout.fillWidth: true

            buttonObjectName: modelData.objectName || ""
            buttonText: modelData.label || ""
            statusBadge: modelData.statusBadge || ""
            active: sectionRoot.activeItemId === (modelData.id || "")
            enabled: modelData.enabled === undefined ? true : Boolean(modelData.enabled)
            activeFillColor: sectionRoot.activeFillColor
            hoverFillColor: sectionRoot.hoverFillColor
            activeStripeColor: sectionRoot.activeStripeColor
            activeTextColor: sectionRoot.activeTextColor
            idleTextColor: sectionRoot.idleTextColor

            onClicked: sectionRoot.itemActivated(modelData.id || "")
        }
    }
}
