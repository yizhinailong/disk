import QtQuick
import QtQuick.Controls

Button {
    id: root

    property string buttonText: ""
    property string buttonObjectName: ""
    property bool active: false
    property color activeFillColor: "#dce8f5"
    property color hoverFillColor: "#eef2f6"
    property color activeStripeColor: "#4f6b8a"
    property color activeTextColor: "#1f2933"
    property color idleTextColor: "#6b7785"

    objectName: root.buttonObjectName
    text: root.buttonText
    flat: true
    hoverEnabled: true
    leftPadding: 18
    rightPadding: 12
    topPadding: 10
    bottomPadding: 10

    background: Rectangle {
        radius: 8
        color: !root.enabled
               ? "transparent"
               : (root.active
                      ? root.activeFillColor
                      : (root.hovered ? root.hoverFillColor : "transparent"))
        opacity: root.enabled ? 1 : 0.6

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 4
            radius: 2
            color: root.activeStripeColor
            visible: root.active
        }
    }

    contentItem: Text {
        text: root.text
        color: root.active ? root.activeTextColor : root.idleTextColor
        opacity: root.enabled ? 1 : 0.7
        font.pixelSize: 14
        font.bold: root.active
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
