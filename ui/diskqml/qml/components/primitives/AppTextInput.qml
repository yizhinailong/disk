import QtQuick
import QtQuick.Controls
import "../../tokens"

TextField {
    id: control

    implicitHeight: 40
    implicitWidth: 200

    font.pixelSize: StyleTokens.fontSizeBody
    font.weight: StyleTokens.fontWeightBody
    color: StyleTokens.colorTextPrimary

    leftPadding: StyleTokens.spacingMd
    rightPadding: StyleTokens.spacingMd

    placeholderTextColor: StyleTokens.colorTextTertiary

    background: Rectangle {
        radius: StyleTokens.radiusMedium
        color: control.activeFocus ? StyleTokens.colorSurface : StyleTokens.colorBackground
        border.color: control.activeFocus ? StyleTokens.colorPrimary : "transparent"
        border.width: 1
    }
}
