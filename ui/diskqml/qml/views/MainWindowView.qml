/**
 * @file MainWindowView.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 主窗口壳：侧边栏(文件/传输双模式) + 工具栏 + 内容路由 + 状态栏
 * @version 0.3
 * @date 2026-03-05
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    // ==================== 状态 ====================

    /// 文件模式 vs 传输模式
    property bool isTransferMode: false

    /// 当前选中的导航 key（与 pageMap 对应）
    property string currentNav: "home"

    /// 向 App.qml 发出退出登录信号
    signal logoutRequested

    // ==================== 登出监听 ====================

    Connections {
        target: SessionViewModel
        function onIsLoggedInChanged() {
            if (!SessionViewModel.isLoggedIn) {
                root.logoutRequested()
            }
        }
    }

    // ==================== 导航模型 ====================

    /// 文件模式导航项
    readonly property var fileNavItems: [
        { key: "home",  label: "首页",   icon: "🏠" },
        { key: "files", label: "我的文件", icon: "📁" },
        { key: "trash", label: "回收站",  icon: "🗑" }
    ]

    /// 传输模式导航项
    readonly property var transferNavItems: [
        { key: "home",     label: "首页", icon: "🏠" },
        { key: "upload",   label: "上传", icon: "⬆" },
        { key: "download", label: "下载", icon: "⬇" },
        { key: "share",    label: "分享", icon: "🔗" }
    ]

    /// 当前活跃的导航列表
    readonly property var activeNavItems: isTransferMode ? transferNavItems : fileNavItems

    // ==================== 页面 URL 映射 ====================

    function pageSourceForNav(navKey: string) : string {
        switch (navKey) {
        case "home":     return "HomePage.qml"
        case "files":    return "FilesPage.qml"
        case "trash":    return "TrashPage.qml"
        case "upload":   return "UploadPage.qml"
        case "download": return "DownloadPage.qml"
        case "share":    return "SharePage.qml"
        case "settings": return "SettingsPage.qml"
        default:         return "HomePage.qml"
        }
    }

    // ==================== 布局 ====================

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== 侧边栏 (200px) ====================
        Rectangle {
            id: sidebar
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: palette.window

            // 右侧分隔线
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: palette.mid
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                // 导航项
                Repeater {
                    model: root.activeNavItems

                    delegate: ItemDelegate {
                        id: navDelegate
                        Layout.fillWidth: true
                        height: 40
                        highlighted: root.currentNav === modelData.key
                        onClicked: root.currentNav = modelData.key

                        contentItem: RowLayout {
                            spacing: 8

                            Label {
                                text: modelData.icon
                                font.pixelSize: 16
                                Layout.preferredWidth: 24
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Label {
                                text: modelData.label
                                font.pixelSize: 14
                                color: navDelegate.highlighted
                                       ? palette.highlightedText
                                       : palette.windowText
                                Layout.fillWidth: true
                            }
                        }

                        background: Rectangle {
                            radius: 6
                            color: navDelegate.highlighted
                                   ? palette.highlight
                                   : navDelegate.hovered ? palette.midlight : "transparent"
                        }
                    }
                }

                // 弹簧
                Item { Layout.fillHeight: true }

                // 模式切换按钮
                ItemDelegate {
                    id: modeSwitchBtn
                    Layout.fillWidth: true
                    height: 40

                    contentItem: RowLayout {
                        spacing: 8

                        Label {
                            text: root.isTransferMode ? "📁" : "📤"
                            font.pixelSize: 16
                            Layout.preferredWidth: 24
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: root.isTransferMode ? "文件" : "传输"
                            font.pixelSize: 14
                            color: palette.windowText
                            Layout.fillWidth: true
                        }
                    }

                    background: Rectangle {
                        radius: 6
                        color: modeSwitchBtn.hovered ? palette.midlight : "transparent"
                    }

                    onClicked: {
                        root.isTransferMode = !root.isTransferMode
                        root.currentNav = "home"
                    }
                }
            }
        }

        // ==================== 右侧主区域 ====================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ==================== 工具栏 (48px) ====================
            Rectangle {
                id: toolbar
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                color: palette.window

                // 底部分隔线
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: palette.mid
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 4

                    // 左侧：主要操作
                    ToolButton {
                        text: "⬆ 上传"
                        font.pixelSize: 13
                        ToolTip.visible: hovered
                        ToolTip.text: "上传文件"
                        visible: root.currentNav === "files"
                        onClicked: {
                            root.isTransferMode = true
                            root.currentNav = "upload"
                        }
                    }

                    ToolButton {
                        text: "📁 新建"
                        font.pixelSize: 13
                        ToolTip.visible: hovered
                        ToolTip.text: "新建文件夹"
                        visible: root.currentNav === "files"
                        onClicked: {
                            if (pageLoader.item && pageLoader.item.newFolderDialog) {
                                pageLoader.item.newFolderDialog.open()
                            }
                        }
                    }

                    // 分隔线
                    Rectangle {
                        width: 1
                        height: 24
                        color: palette.mid
                        Layout.alignment: Qt.AlignVCenter
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
                        color: palette.mid
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // 设置
                    ToolButton {
                        text: "⚙"
                        font.pixelSize: 16
                        ToolTip.visible: hovered
                        ToolTip.text: "设置"
                        onClicked: root.currentNav = "settings"
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

            // ==================== 主内容区 (Loader路由) ====================
            Loader {
                id: pageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                source: root.pageSourceForNav(root.currentNav)
            }

            // ==================== 页面信号路由 ====================

            Connections {
                target: pageLoader.item
                ignoreUnknownSignals: true

                function onNavigateToUpload() {
                    root.isTransferMode = true
                    root.currentNav = "upload"
                }

                function onNavigateToDownload() {
                    root.isTransferMode = true
                    root.currentNav = "download"
                }

                function onNavigateToFiles() {
                    root.isTransferMode = false
                    root.currentNav = "files"
                }
            }

            // ==================== 状态栏 (32px) ====================
            Rectangle {
                id: statusBar
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                color: palette.window

                // 顶部分隔线
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: palette.mid
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16

                    // 左侧：项目数量（后续由 FileListViewModel 绑定）
                    Label {
                        id: itemCountLabel
                        text: root.currentNav === "files" && FileListViewModel.totalItems > 0
                              ? FileListViewModel.totalItems + " 个项目"
                              : ""
                        color: palette.placeholderText
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }

                    // 右侧：存储使用情况
                    Label {
                        text: SessionViewModel.isLoggedIn
                              ? "已用 " + SessionViewModel.storageUsedFormatted
                                + " / " + SessionViewModel.storageQuotaFormatted
                              : ""
                        color: palette.placeholderText
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
