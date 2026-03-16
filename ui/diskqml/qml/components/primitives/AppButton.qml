import QtQuick
import QtQuick.Controls
import "../../tokens"

Button {
    id: control

    // "primary", "secondary", "icon", "pill"
    property string variant: "primary"
    property bool selected: false // For Pill variant

    implicitHeight: {
        if (variant === "icon" || variant === "pill") return 32;
        return 36;
    }
    implicitWidth: {
        if (variant === "icon") return 32;
        return Math.max(implicitBackgroundWidth + leftInset + rightInset,
                        implicitContentWidth + leftPadding + rightPadding);
    }

    leftPadding: variant === "icon" ? 0 : StyleTokens.spacingMd
    rightPadding: variant === "icon" ? 0 : StyleTokens.spacingMd

    contentItem: Text {
        text: control.text
        font.pixelSize: variant === "icon" ? StyleTokens.fontSizeH1 : StyleTokens.fontSizeBody
        font.weight: StyleTokens.fontWeightBody
        color: {
            if (!control.enabled) return StyleTokens.colorTextTertiary;
            if (variant === "primary") return "white";
            if (variant === "pill" && control.selected) return "white";
            return StyleTokens.colorTextPrimary;
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 80
        implicitHeight: control.implicitHeight
        radius: {
            if (variant === "pill") return StyleTokens.radiusFull;
            return StyleTokens.radiusSmall;
        }
        color: {
            if (!control.enabled) return StyleTokens.colorBackground;
            if (variant === "primary") {
                return control.hovered ? StyleTokens.colorPrimaryHover : StyleTokens.colorPrimary;
            }
            if (variant === "secondary") {
                return control.hovered ? StyleTokens.colorHover : "transparent";
            }
            if (variant === "icon") {
                return control.hovered ? StyleTokens.colorHover : "transparent";
            }
            if (variant === "pill") {
                if (control.selected) return StyleTokens.colorPrimary;
                return control.hovered ? StyleTokens.colorHover : "transparent";
            }
            return "transparent";
        }
        border.color: {
            if (!control.enabled) return "transparent";
            if (variant === "secondary") return StyleTokens.colorBorder;
            return "transparent";
        }
        border.width: variant === "secondary" ? 1 : 0

        scale: {
            if (!control.enabled) return 1.0;
            if (control.pressed) return 0.98;
            if (control.hovered && variant === "primary") return 1.02;
            return 1.0;
        }
        Behavior on scale {
            NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
        }
        Behavior on color {
            ColorAnimation { duration: 200; easing.type: Easing.OutQuad }
        }
    }
}
