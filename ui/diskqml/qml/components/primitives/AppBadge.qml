import QtQuick
import QtQuick.Controls
import "../../tokens"

Rectangle {
    id: control

    property string text: ""
    property string status: "success" // "success", "warning", "error", "info"

    implicitHeight: 20
    implicitWidth: Math.max(40, badgeText.implicitWidth + StyleTokens.spacingSm * 2)
    radius: StyleTokens.radiusFull

    color: {
        if (status === "success") return StyleTokens.colorSuccessLight; // Success Light
        if (status === "warning") return StyleTokens.colorWarningLight; // Warning Light
        if (status === "error") return StyleTokens.colorErrorLight; // Error Light
        if (status === "info") return StyleTokens.colorPrimaryLight;
        return StyleTokens.colorBackground;
    }

    Text {
        id: badgeText
        anchors.centerIn: parent
        text: control.text
        font.pixelSize: StyleTokens.fontSizeSmall
        font.weight: StyleTokens.fontWeightSmall
        color: {
            if (status === "success") return StyleTokens.colorSuccess;
            if (status === "warning") return StyleTokens.colorWarning;
            if (status === "error") return StyleTokens.colorError;
            if (status === "info") return StyleTokens.colorInfo;
            return StyleTokens.colorTextSecondary;
        }
    }
}
