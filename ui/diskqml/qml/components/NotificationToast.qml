/**
 * @file NotificationToast.qml
 * @brief 通知提示组件
 * @author LiuFeng (liufeng.code@outlook.com)
 * @copyright Copyright (c) 2026
 */
import QtQuick
import QtQuick.Controls
import "../tokens"

Item {
    id: root

    // 可选：允许自定义边距/偏移量
    property int bottomMargin: 60

    function showSuccess(message) {
        successTooltip.text = message
        successTooltip.visible = true
        successTooltipTimer.restart()
    }

    function showError(message) {
        failTooltip.text = message
        failTooltip.visible = true
        failTooltipTimer.restart()
    }

    component ToastToolTip : ToolTip {
        id: control
        property color bgColor: StyleTokens.colorSurface
        property color textColor: StyleTokens.colorTextPrimary

        contentItem: Text {
            text: control.text
            font.pixelSize: StyleTokens.fontSizeBody
            font.weight: StyleTokens.fontWeightBody
            color: control.textColor
        }

        background: Rectangle {
            color: control.bgColor
            radius: StyleTokens.radiusMedium
            border.color: StyleTokens.colorBorder
            border.width: 1
        }
    }

    ToastToolTip {
        id: successTooltip
        timeout: 3000
        y: root.parent ? root.parent.height - root.bottomMargin : 0
        x: root.parent ? (root.parent.width - width) / 2 : 0
        bgColor: StyleTokens.colorSuccess
        textColor: StyleTokens.colorSurface
    }

    Timer {
        id: successTooltipTimer
        interval: 3000
        onTriggered: successTooltip.visible = false
    }

    ToastToolTip {
        id: failTooltip
        timeout: 5000
        y: root.parent ? root.parent.height - root.bottomMargin : 0
        x: root.parent ? (root.parent.width - width) / 2 : 0
        bgColor: StyleTokens.colorError
        textColor: StyleTokens.colorSurface
    }

    Timer {
        id: failTooltipTimer
        interval: 5000
        onTriggered: failTooltip.visible = false
    }
}
