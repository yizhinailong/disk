import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0
import "../tokens"

Rectangle {
    id: root
    color: StyleTokens.colorSurface
    height: StyleTokens.statusBarHeight

    property string currentNav: "home"

    // 顶部分隔线
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: StyleTokens.colorBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: StyleTokens.spacingMd
        anchors.rightMargin: StyleTokens.spacingMd

        // 左侧：项目数量（后续由 FileListViewModel 绑定）
        Label {
            id: itemCountLabel
            text: root.currentNav === "files" && FileListViewModel.totalItems > 0
                  ? FileListViewModel.totalItems + " 个项目"
                  : ""
            color: StyleTokens.colorTextTertiary
            font.pixelSize: StyleTokens.fontSizeSmall
        }

        Item { Layout.fillWidth: true }

        // 右侧：存储使用情况
        Label {
            text: SessionViewModel.isLoggedIn
                  ? "已用 " + SessionViewModel.storageUsedFormatted
                    + " / " + SessionViewModel.storageQuotaFormatted
                  : ""
            color: StyleTokens.colorTextTertiary
            font.pixelSize: StyleTokens.fontSizeSmall
        }
    }
}
