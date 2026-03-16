import QtQuick
import QtQuick.Controls
import "../../tokens"

Rectangle {
    id: control

    property bool selected: false
    property bool hoverEnabled: true
    property bool isHovered: mouseArea.containsMouse

    signal clicked()

    implicitWidth: 120
    implicitHeight: 140
    radius: StyleTokens.radiusLarge

    color: selected ? StyleTokens.colorPrimaryLight : StyleTokens.colorSurface
    border.color: {
        if (selected) return StyleTokens.colorPrimary;
        if (isHovered && hoverEnabled) return StyleTokens.colorPrimaryLight;
        return "transparent";
    }
    border.width: (selected || (isHovered && hoverEnabled)) ? 1 : 0

    // Simple shadow simulation using border or a separate rectangle
    // For now, we just use the border and color changes to indicate state
    // as Qt6 MultiEffect might not be available or too heavy for a simple card

    Behavior on color {
        ColorAnimation { duration: 200; easing.type: Easing.OutQuad }
    }
    Behavior on border.color {
        ColorAnimation { duration: 200; easing.type: Easing.OutQuad }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: control.hoverEnabled
        onClicked: control.clicked()
    }
}
