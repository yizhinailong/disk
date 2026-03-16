import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0
import "../tokens"

Rectangle {
    id: root
    color: StyleTokens.colorSurface
    height: StyleTokens.toolBarHeight

    property string currentNav: "home"

    signal uploadClicked()
    signal newFolderClicked()
    signal settingsClicked()

    // 底部分隔线
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: StyleTokens.colorBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: StyleTokens.spacingLarge || 12
        anchors.rightMargin: StyleTokens.spacingLarge || 12
        spacing: StyleTokens.spacingXs

        // 左侧：主要操作
        ToolButton {
            text: "⬆ 上传"
            font.pixelSize: 13
            ToolTip.visible: hovered
            ToolTip.text: "上传文件"
            visible: root.currentNav === "files"
            onClicked: root.uploadClicked()
        }

        ToolButton {
            text: "📁 新建"
            font.pixelSize: 13
            ToolTip.visible: hovered
            ToolTip.text: "新建文件夹"
            visible: root.currentNav === "files"
            onClicked: root.newFolderClicked()
        }

        // 分隔线
        Rectangle {
            width: 1
            height: 24
            color: StyleTokens.colorBorder
            Layout.alignment: Qt.AlignVCenter
            visible: root.currentNav === "files"
        }

        // 导航控制
        ToolButton {
            text: "◀"
            font.pixelSize: 14
            ToolTip.visible: hovered
            ToolTip.text: "返回"
            visible: root.currentNav === "files"
            enabled: FileListViewModel.canGoBack
            onClicked: FileListViewModel.goBack()
        }

        ToolButton {
            text: "▶"
            font.pixelSize: 14
            ToolTip.visible: hovered
            ToolTip.text: "前进"
            visible: root.currentNav === "files"
            enabled: FileListViewModel.canGoForward
            onClicked: FileListViewModel.goForward()
        }

        ToolButton {
            text: "🔄"
            font.pixelSize: 14
            ToolTip.visible: hovered
            ToolTip.text: "刷新"
            visible: root.currentNav === "files"
            onClicked: FileListViewModel.refresh()
        }

        // 弹簧
        Item { Layout.fillWidth: true }

        // 搜索框
        TextField {
            id: searchField
            placeholderText: "搜索..."
            Layout.preferredWidth: 200
            font.pixelSize: 13
            visible: root.currentNav === "files"
            onTextChanged: FileListViewModel.search(text)
        }

        // 分隔线
        Rectangle {
            width: 1
            height: 24
            color: StyleTokens.colorBorder
            Layout.alignment: Qt.AlignVCenter
        }

        // 设置
        ToolButton {
            text: "⚙"
            font.pixelSize: 16
            ToolTip.visible: hovered
            ToolTip.text: "设置"
            onClicked: root.settingsClicked()
        }

        // 退出登录
        ToolButton {
            text: "退出"
            font.pixelSize: 13
            ToolTip.visible: hovered
            ToolTip.text: "退出登录"
            onClicked: SessionViewModel.logout()
        }
    }
}
