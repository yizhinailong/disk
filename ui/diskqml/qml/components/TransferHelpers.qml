/**
 * @file TransferHelpers.qml
 * @brief 传输辅助工具组件
 * @author LiuFeng (liufeng.code@outlook.com)
 * @copyright Copyright (c) 2026
 */
pragma Singleton
import QtQuick
import "../tokens"

QtObject {
    id: root

    function formatSpeed(bytesPerSec) {
        if (bytesPerSec <= 0) return ""
        if (bytesPerSec < 1024) return bytesPerSec + " B/s"
        if (bytesPerSec < 1048576) return (bytesPerSec / 1024).toFixed(1) + " KB/s"
        if (bytesPerSec < 1073741824) return (bytesPerSec / 1048576).toFixed(1) + " MB/s"
        return (bytesPerSec / 1073741824).toFixed(1) + " GB/s"
    }

    function formatEta(seconds) {
        if (seconds <= 0) return ""
        if (seconds < 60) return seconds + "秒"
        if (seconds < 3600) return Math.floor(seconds / 60) + "分" + (seconds % 60) + "秒"
        var h = Math.floor(seconds / 3600)
        var m = Math.floor((seconds % 3600) / 60)
        return h + "时" + m + "分"
    }

    function formatSize(bytes) {
        return FormatUtils.formatSize(bytes)
    }

    function statusColor(status, palette) {
        switch (status) {
        case 1: return StyleTokens.colorPrimary       // 运行中
        case 2: return StyleTokens.colorTextTertiary  // 已暂停
        case 3: return StyleTokens.colorSuccess       // 已完成 (绿色)
        case 4: return StyleTokens.colorError         // 失败 (红色)
        default: return StyleTokens.colorTextTertiary // 排队中
        }
    }

    function statusText(status, isUpload) {
        switch (status) {
        case 0: return "等待中"
        case 1: return isUpload ? "上传中" : "下载中"
        case 2: return "已暂停"
        case 3: return "已完成"
        case 4: return "失败"
        default: return ""
        }
    }
}