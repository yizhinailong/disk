/**
 * @file NotificationToast.qml
 * @brief 通知提示组件
 * @author LiuFeng (liufeng.code@outlook.com)
 * @copyright Copyright (c) 2026
 */
import QtQuick
import QtQuick.Controls

Item {
    id: root

    // Optional: allow customizing the margins/offsets
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

    ToolTip {
        id: successTooltip
        timeout: 3000
        y: root.parent ? root.parent.height - root.bottomMargin : 0
        x: root.parent ? (root.parent.width - width) / 2 : 0
    }

    Timer {
        id: successTooltipTimer
        interval: 3000
        onTriggered: successTooltip.visible = false
    }

    ToolTip {
        id: failTooltip
        timeout: 5000
        y: root.parent ? root.parent.height - root.bottomMargin : 0
        x: root.parent ? (root.parent.width - width) / 2 : 0
    }

    Timer {
        id: failTooltipTimer
        interval: 5000
        onTriggered: failTooltip.visible = false
    }
}
