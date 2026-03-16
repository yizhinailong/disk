/**
 * @file MainWindowView.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 主窗口壳：侧边栏(文件/传输双模式) + 工具栏 + 内容路由 + 状态栏
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Disk 1.0

Item {
    id: root

    // ==================== 状态 ====================

    /// 响应式布局模式: "紧凑" | "中等" | "展开"
    readonly property string layoutMode: root.width < 800 ? "compact" : (root.width < 1200 ? "medium" : "expanded")

    /// 侧边栏宽度根据模式动态调整
    readonly property int sidebarWidth: layoutMode === "compact" ? 56 : (layoutMode === "medium" ? 200 : 240)

    /// 是否显示侧边栏文字标签
    readonly property bool showSidebarLabels: layoutMode !== "compact"

    /// 文件模式与传输模式

    /// 文件模式与传输模式
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
        case "user":     return "UserPage.qml"
        default:         return "HomePage.qml"
        }
    }

    // ==================== 布局 ====================

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== 侧边栏 (响应式) ====================
        Rectangle {
            id: sidebar
            Layout.preferredWidth: root.sidebarWidth
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

            // 宽度变化动画
            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
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
                            spacing: root.showSidebarLabels ? 8 : 0

                            Label {
                                text: modelData.icon
                                font.pixelSize: 16
                                Layout.preferredWidth: 24
                                Layout.alignment: Qt.AlignHCenter
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Label {
                                text: modelData.label
                                font.pixelSize: 14
                                color: navDelegate.highlighted
                                       ? palette.highlightedText
                                       : palette.windowText
                                visible: root.showSidebarLabels
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }

                        ToolTip.visible: !root.showSidebarLabels && navDelegate.hovered
                        ToolTip.text: modelData.label
                        ToolTip.delay: 500

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

                // 个人设置按钮
                ItemDelegate {
                    id: userProfileBtn
                    Layout.fillWidth: true
                    height: 40
                    highlighted: root.currentNav === "user"

                    contentItem: RowLayout {
                        spacing: root.showSidebarLabels ? 8 : 0

                        Label {
                            text: "👤"
                            font.pixelSize: 16
                            Layout.preferredWidth: 24
                            Layout.alignment: Qt.AlignHCenter
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "个人设置"
                            font.pixelSize: 14
                            color: userProfileBtn.highlighted
                                   ? palette.highlightedText
                                   : palette.windowText
                            visible: root.showSidebarLabels
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    ToolTip.visible: !root.showSidebarLabels && userProfileBtn.hovered
                    ToolTip.text: qsTr("个人设置")
                    ToolTip.delay: 500

                    background: Rectangle {
                        radius: 6
                        color: userProfileBtn.highlighted
                               ? palette.highlight
                               : userProfileBtn.hovered ? palette.midlight : "transparent"
                    }

                    onClicked: root.currentNav = "user"
                }

                // 模式切换按钮
                ItemDelegate {
                    id: modeSwitchBtn
                    Layout.fillWidth: true
                    height: 40

                    contentItem: RowLayout {
                        spacing: root.showSidebarLabels ? 8 : 0

                        Label {
                            text: root.isTransferMode ? "📁" : "📤"
                            font.pixelSize: 16
                            Layout.preferredWidth: 24
                            Layout.alignment: Qt.AlignHCenter
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: root.isTransferMode ? "文件" : "传输"
                            font.pixelSize: 14
                            color: palette.windowText
                            visible: root.showSidebarLabels
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    ToolTip.visible: !root.showSidebarLabels && modeSwitchBtn.hovered
                    ToolTip.text: root.isTransferMode ? qsTr("切换到文件模式") : qsTr("切换到传输模式")
                    ToolTip.delay: 500

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
                            uploadFileDialog.open()
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

                function onOpenUploadDialog() {
                    uploadFileDialog.open()
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

    // ==================== 文件选择对话框 ====================

    FileDialog {
        id: uploadFileDialog
        title: "选择要上传的文件"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["所有文件 (*)"]
        
        onAccepted: {
            if (selectedFiles.length === 0) {
                notificationToast.showError("请选择要上传的文件")
                return
            }
            
            // 存储选中的文件并显示目标选择器
            pendingUploadFiles = selectedFiles
            uploadDestinationDialog.open()
        }
        
        onRejected: {
            // 用户取消 - 无需操作
        }
    }

    // ==================== 上传目标选择对话框 ====================

    property var pendingUploadFiles: []
    property int uploadTargetFolderId: -1

    Dialog {
        id: uploadDestinationDialog
        title: qsTr("选择上传目标")
        modal: true
        width: 420
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        padding: 24

        onOpened: {
            // 默认为当前文件夹
            destinationModeGroup.checkedButton = currentFolderRadio
            uploadTargetFolderId = -1
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 16

            Label {
                text: qsTr("请选择上传目标文件夹：")
                font.pixelSize: 14
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            ButtonGroup {
                id: destinationModeGroup
            }

            // 当前文件夹选项
            RadioButton {
                id: currentFolderRadio
                ButtonGroup.group: destinationModeGroup
                text: qsTr("当前文件夹")
                checked: true
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("上传到: ") + (FileListViewModel.currentPath || "/")
                font.pixelSize: 11
                color: palette.placeholderText
                Layout.leftMargin: 28
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: currentFolderRadio.checked
            }

            // 根目录选项
            RadioButton {
                id: rootFolderRadio
                ButtonGroup.group: destinationModeGroup
                text: qsTr("根目录")
                Layout.fillWidth: true
            }

            // 自定义文件夹选项
            RadioButton {
                id: customFolderRadio
                ButtonGroup.group: destinationModeGroup
                text: qsTr("选择其他文件夹...")
                Layout.fillWidth: true
            }

            // 自定义文件夹选择器
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: customFolderPicker.visible ? 180 : 40
                color: "transparent"
                visible: customFolderRadio.checked

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Button {
                        text: qsTr("浏览...")
                        visible: !customFolderPicker.visible
                        onClicked: {
                            FileListViewModel.loadFolderTree()
                            customFolderPicker.visible = true
                        }
                    }

                    Rectangle {
                        id: customFolderPicker
                        visible: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: palette.base
                        border.color: palette.mid
                        radius: 4

                        property int pickedFolderId: -1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true

                                Label {
                                    text: qsTr("选择目标文件夹:")
                                    font.pixelSize: 11
                                    font.bold: true
                                }

                                Item { Layout.fillWidth: true }

                                Button {
                                    text: qsTr("关闭")
                                    flat: true
                                    font.pixelSize: 11
                                    onClicked: customFolderPicker.visible = false
                                }
                            }

                            // 选择器中的根目录选项
                            ItemDelegate {
                                Layout.fillWidth: true
                                height: 28
                                highlighted: customFolderPicker.pickedFolderId === 0

                                contentItem: RowLayout {
                                    spacing: 6
                                    Label { text: "📁"; font.pixelSize: 12 }
                                    Label { text: qsTr("根目录"); font.pixelSize: 11; Layout.fillWidth: true }
                                }

                                onClicked: {
                                    customFolderPicker.pickedFolderId = 0
                                    uploadTargetFolderId = 0
                                    customFolderPicker.visible = false
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: customFolderPicker.pickedFolderId === 0
                                           ? palette.highlight
                                           : parent.hovered ? palette.midlight : "transparent"
                                }
                            }

                            ScrollView {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true

                                TreeView {
                                    id: uploadFolderTreeView
                                    anchors.fill: parent
                                    model: FileListViewModel.folderTreeModel
                                    visible: FileListViewModel.folderTreeModel ? !FileListViewModel.folderTreeModel.loading : false

                                    delegate: TreeViewDelegate {
                                        implicitHeight: 28
                                        implicitWidth: uploadFolderTreeView.width

                                        contentItem: RowLayout {
                                            spacing: 6
                                            Label {
                                                text: uploadFolderTreeView.isExpanded(row) ? "📂" : "📁"
                                                font.pixelSize: 11
                                            }
                                            Label {
                                                text: model.folderName ?? model.display ?? ""
                                                font.pixelSize: 11
                                                font.bold: customFolderPicker.pickedFolderId === (model.folderId ?? -1)
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                            }
                                        }

                                        background: Rectangle {
                                            radius: 4
                                            color: {
                                                var fid = model.folderId ?? -1
                                                if (customFolderPicker.pickedFolderId === fid) return palette.highlight
                                                if (hovered) return palette.midlight
                                                return "transparent"
                                            }
                                        }

                                        onClicked: {
                                            var fid = model.folderId ?? -1
                                            customFolderPicker.pickedFolderId = fid
                                            uploadTargetFolderId = fid
                                            customFolderPicker.visible = false
                                        }
                                    }
                                }
                            }

                            BusyIndicator {
                                anchors.centerIn: parent
                                running: FileListViewModel.folderTreeModel ? FileListViewModel.folderTreeModel.loading : false
                                visible: running
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }

            // 按钮行
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("取消")
                    onClicked: {
                        pendingUploadFiles = []
                        uploadDestinationDialog.reject()
                    }
                }

                Button {
                    text: qsTr("开始上传")
                    highlighted: true
                    enabled: pendingUploadFiles.length > 0
                              && (!customFolderRadio.checked || uploadTargetFolderId >= 0)

                    onClicked: {
                        var targetFolderId = 0

                        if (currentFolderRadio.checked) {
                            targetFolderId = FileListViewModel.currentFolderId
                        } else if (rootFolderRadio.checked) {
                            targetFolderId = 0
                        } else if (customFolderRadio.checked) {
                            targetFolderId = uploadTargetFolderId
                        }

                        // 无效目标保护：回退到根目录
                        if (targetFolderId < 0) {
                            console.warn("[UploadDialog] Invalid targetFolderId:", targetFolderId, "- falling back to root")
                            targetFolderId = 0
                        }

                        // 通过 TransfersViewModel 开始上传
                        TransfersViewModel.startUpload(pendingUploadFiles, targetFolderId)

                        // 显示成功反馈
                        notificationToast.showSuccess("已添加 " + pendingUploadFiles.length + " 个文件到上传队列")

                        // 导航到上传页面以显示进度
                        root.isTransferMode = true
                        root.currentNav = "upload"

                        // 重置状态
                        pendingUploadFiles = []
                        uploadDestinationDialog.close()
                    }
                }
            }
        }
    }

    // ==================== 操作结果提示 ====================

    NotificationToast {
        id: notificationToast
    }
}
