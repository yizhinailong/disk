import QtQuick
import QtQuick.Controls
import "../../tokens"

ItemDelegate {
    id: control

    property string iconText: ""
    property string labelText: ""

    implicitHeight: 40
    implicitWidth: 200

    contentItem: Row {
        spacing: StyleTokens.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: StyleTokens.spacingMd

        Text {
            text: control.iconText
            font.pixelSize: StyleTokens.fontSizeH2
            color: StyleTokens.colorTextPrimary
            visible: control.iconText !== ""
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: control.labelText
            font.pixelSize: StyleTokens.fontSizeBody
            color: StyleTokens.colorTextPrimary
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: StyleTokens.radiusMedium
        color: control.hovered ? StyleTokens.colorHover : "transparent"
    }
}
