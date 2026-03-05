/**
 * @file FilesPage.qml
 * @brief 我的文件页 — 面包屑 + 网格/列表视图 + 右键菜单 + 排序 + 搜索 + 空状态
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    // ==================== 信号 (供 MainWindowView Connections 路由) ====================

    signal navigateToUpload
    signal navigateToDownload

    // ==================== 初始化 ====================

    Component.onCompleted: {
        FileListViewModel.refresh()
    }

    // ==================== 辅助函数 ====================

    function fileIcon(fileType: string, mimeType: string) : string {
        if (fileType === "folder") return "📁"
        if (mimeType.startsWith("image/")) return "🖼️"
        if (mimeType.startsWith("video/")) return "🎦"
        if (mimeType.startsWith("audio/")) return "🎵"
        if (mimeType === "application/pdf") return "📕"
        if (mimeType.indexOf("spreadsheet") >= 0 || mimeType.indexOf("excel") >= 0 || mimeType.indexOf("csv") >= 0) return "📊"
        if (mimeType.indexOf("presentation") >= 0 || mimeType.indexOf("powerpoint") >= 0) return "🎥️"
        if (mimeType.indexOf("word") >= 0 || mimeType.indexOf("document") >= 0 || mimeType.startsWith("text/")) return "📄"
        if (mimeType.indexOf("zip") >= 0 || mimeType.indexOf("rar") >= 0 || mimeType.indexOf("tar") >= 0 || mimeType.indexOf("compress") >= 0 || mimeType.indexOf("7z") >= 0) return "📦"
        if (mimeType.indexOf("javascript") >= 0 || mimeType.indexOf("json") >= 0 || mimeType.indexOf("xml") >= 0 || mimeType.indexOf("x-c") >= 0 || mimeType.indexOf("python") >= 0) return "💻"
        return "📎"
    }

    function formatSize(bytes: int) : string {
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + " MB"
        return (bytes / 1073741824).toFixed(1) + " GB"
    }

    function formatDate(dateStr: string) : string {
        if (!dateStr) return "-"
        // Truncate to first 10 chars: "2026-02-15"
        return dateStr.substring(0, 10)
    }

    function fileTypeLabel(fileType: string, mimeType: string) : string {
        if (fileType === "folder") return "文件夹"
        if (mimeType.startsWith("image/")) return "图片"
        if (mimeType.startsWith("video/")) return "视频"
        if (mimeType.startsWith("audio/")) return "音频"
        if (mimeType === "application/pdf") return "PDF"
        if (mimeType.indexOf("spreadsheet") >= 0 || mimeType.indexOf("excel") >= 0) return "表格"
        if (mimeType.indexOf("presentation") >= 0 || mimeType.indexOf("powerpoint") >= 0) return "演示"
        if (mimeType.indexOf("word") >= 0 || mimeType.indexOf("document") >= 0) return "文档"
        if (mimeType.startsWith("text/")) return "文本"
        if (mimeType.indexOf("zip") >= 0 || mimeType.indexOf("rar") >= 0 || mimeType.indexOf("compress") >= 0) return "压缩包"
        return "文件"
    }

    // ==================== 主布局 ====================

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== 面包屑 + 排序/视图切换 工具条 ====================

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 4

                // --- 面包屑 ---
                Repeater {
                    model: FileListViewModel.breadcrumbModel

                    delegate: Row {
                        spacing: 0

                        // 分隔符 (除了第一项)
                        Label {
                            text: " / "
                            visible: index > 0
                            font.pixelSize: 13
                            color: palette.placeholderText
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        // 可点击面包屑
                        Label {
                            text: model.folderName
                            font.pixelSize: 13
                            font.bold: index === FileListViewModel.breadcrumbModel.count - 1
                            color: index === FileListViewModel.breadcrumbModel.count - 1
                                   ? palette.windowText
                                   : palette.link
                            anchors.verticalCenter: parent.verticalCenter

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (index < FileListViewModel.breadcrumbModel.count - 1) {
                                        FileListViewModel.navigateToFolder(model.folderId)
                                    }
                                }
                            }
                        }
                    }
                }


                // --- 新建文件夹 ---
                ToolButton {
                    text: "📁+"
                    font.pixelSize: 14
                    ToolTip.visible: hovered
                    ToolTip.text: "新建文件夹"
                    onClicked: newFolderDialog.open()
                }

                Item { Layout.fillWidth: true }

                // --- 排序 ---
                ComboBox {
                    id: sortCombo
                    Layout.preferredWidth: 120
                    font.pixelSize: 12
                    model: [
                        { text: "名称", value: "name" },
                        { text: "大小", value: "size" },
                        { text: "修改时间", value: "updated_at" },
                        { text: "创建时间", value: "created_at" }
                    ]
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: {
                        var sb = FileListViewModel.sortBy
                        for (var i = 0; i < model.length; ++i) {
                            if (model[i].value === sb) return i
                        }
                        return 0
                    }
                    onActivated: FileListViewModel.setSortField(currentValue)
                }

                ToolButton {
                    text: FileListViewModel.sortOrder === "asc" ? "↑" : "↓"
                    font.pixelSize: 14
                    ToolTip.visible: hovered
                    ToolTip.text: FileListViewModel.sortOrder === "asc" ? "升序" : "降序"
                    onClicked: FileListViewModel.toggleSortOrder()
                }

                // --- 分隔线 ---
                Rectangle {
                    width: 1; height: 20; color: palette.mid
                    Layout.alignment: Qt.AlignVCenter
                }

                // --- 视图切换 ---
                ToolButton {
                    text: "⊞"
                    font.pixelSize: 16
                    ToolTip.visible: hovered
                    ToolTip.text: "网格视图"
                    highlighted: FileListViewModel.viewMode === "grid"
                    onClicked: FileListViewModel.viewMode = "grid"
                }

                ToolButton {
                    text: "☰"
                    font.pixelSize: 16
                    ToolTip.visible: hovered
                    ToolTip.text: "列表视图"
                    highlighted: FileListViewModel.viewMode === "list"
                    onClicked: FileListViewModel.viewMode = "list"
                }
            }
        }

        // --- 顶部分隔线 ---
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: palette.mid
        }

        // ==================== 多选操作栏 ====================

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: palette.highlight
            opacity: 0.15
            visible: FileListViewModel.hasSelection

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Label {
                    text: "已选择 " + FileListViewModel.selectionCount + " 项"
                    font.pixelSize: 13
                    color: palette.windowText
                }

                Item { Layout.fillWidth: true }

                ToolButton {
                    text: "取消选择"
                    font.pixelSize: 12
                    onClicked: FileListViewModel.clearSelection()
                }
            }
        }

        // ==================== 内容区（加载 / 错误 / 空 / 网格 / 列表）====================

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // --- 加载指示器 ---
            BusyIndicator {
                anchors.centerIn: parent
                running: FileListViewModel.loading
                visible: FileListViewModel.loading
            }

            // --- 错误状态 ---
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                visible: !FileListViewModel.loading && FileListViewModel.errorMessage !== ""

                Label {
                    text: "⚠️ " + FileListViewModel.errorMessage
                    font.pixelSize: 14
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Button {
                    text: "重试"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: FileListViewModel.refresh()
                }
            }

            // --- 空目录状态 ---
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                visible: !FileListViewModel.loading
                         && FileListViewModel.errorMessage === ""
                         && FileListViewModel.fileListModel.count === 0

                Label {
                    text: "📭 此文件夹为空"
                    font.pixelSize: 18
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: FileListViewModel.isSearching
                          ? "没有找到匹配的文件"
                          : "点击上传按钮添加文件，或创建新文件夹"
                    font.pixelSize: 13
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            // --- 网格视图 ---
            GridView {
                id: gridView
                anchors.fill: parent
                anchors.margins: 16
                visible: !FileListViewModel.loading
                         && FileListViewModel.errorMessage === ""
                         && FileListViewModel.fileListModel.count > 0
                         && FileListViewModel.viewMode === "grid"
                clip: true
                cellWidth: 116
                cellHeight: 116
                model: FileListViewModel.fileListModel
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    width: gridView.cellWidth
                    height: gridView.cellHeight

                    Rectangle {
                        id: gridCard
                        anchors.fill: parent
                        anchors.margins: 8
                        radius: 8
                        color: FileListViewModel.isSelected(model.fileId)
                               ? palette.highlight
                               : gridCardMa.containsMouse ? palette.midlight : palette.base
                        border.color: FileListViewModel.isSelected(model.fileId) ? palette.highlight : palette.mid
                        border.width: 1

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4

                            Item { Layout.fillHeight: true }

                            Label {
                                text: root.fileIcon(model.fileType, model.fileMimeType)
                                font.pixelSize: 36
                                Layout.alignment: Qt.AlignHCenter
                            }

                            Label {
                                text: model.fileName
                                font.pixelSize: 11
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                maximumLineCount: 2
                                wrapMode: Text.Wrap
                                color: FileListViewModel.isSelected(model.fileId)
                                       ? palette.highlightedText : palette.windowText
                            }

                            Item { Layout.fillHeight: true }
                        }

                        MouseArea {
                            id: gridCardMa
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton

                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    contextMenu.targetFileId = model.fileId
                                    contextMenu.targetFileName = model.fileName
                                    contextMenu.targetIsFolder = model.fileIsFolder
                                    contextMenu.popup()
                                } else if (mouse.modifiers & Qt.ControlModifier) {
                                    FileListViewModel.toggleSelection(model.fileId)
                                } else {
                                    FileListViewModel.clearSelection()
                                    FileListViewModel.toggleSelection(model.fileId)
                                }
                            }

                            onDoubleClicked: {
                                if (model.fileIsFolder) {
                                    FileListViewModel.navigateToFolder(model.fileId)
                                }
                            }
                        }
                    }
                }
            }

            // --- 列表视图 ---
            ListView {
                id: listView
                anchors.fill: parent
                visible: !FileListViewModel.loading
                         && FileListViewModel.errorMessage === ""
                         && FileListViewModel.fileListModel.count > 0
                         && FileListViewModel.viewMode === "list"
                clip: true
                model: FileListViewModel.fileListModel
                ScrollBar.vertical: ScrollBar {}

                // 列表头
                header: Rectangle {
                    width: listView.width
                    height: 36
                    color: palette.window

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
                        spacing: 8

                        Label {
                            text: "名称"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.fillWidth: true
                        }

                        Label {
                            text: "大小"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: "修改时间"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 100
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: "类型"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 60
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                delegate: Rectangle {
                    id: listRow
                    width: listView.width
                    height: 40
                    color: FileListViewModel.isSelected(model.fileId)
                           ? palette.highlight
                           : listRowMa.containsMouse ? palette.midlight : "transparent"

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: palette.mid
                        opacity: 0.3
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 8

                        // 图标 + 文件名
                        Label {
                            text: root.fileIcon(model.fileType, model.fileMimeType)
                            font.pixelSize: 16
                            Layout.preferredWidth: 24
                        }

                        Label {
                            text: model.fileName
                            font.pixelSize: 13
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            color: FileListViewModel.isSelected(model.fileId)
                                   ? palette.highlightedText : palette.windowText
                        }

                        // 大小
                        Label {
                            text: model.fileIsFolder ? (model.fileItemCount + " 项") : root.formatSize(model.fileSize)
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignRight
                        }

                        // 修改时间
                        Label {
                            text: root.formatDate(model.fileUpdatedAt)
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 100
                            horizontalAlignment: Text.AlignRight
                        }

                        // 类型
                        Label {
                            text: root.fileTypeLabel(model.fileType, model.fileMimeType)
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 60
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    MouseArea {
                        id: listRowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton

                        onClicked: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                contextMenu.targetFileId = model.fileId
                                contextMenu.targetFileName = model.fileName
                                contextMenu.targetIsFolder = model.fileIsFolder
                                contextMenu.popup()
                            } else if (mouse.modifiers & Qt.ControlModifier) {
                                FileListViewModel.toggleSelection(model.fileId)
                            } else {
                                FileListViewModel.clearSelection()
                                FileListViewModel.toggleSelection(model.fileId)
                            }
                        }

                        onDoubleClicked: {
                            if (model.fileIsFolder) {
                                FileListViewModel.navigateToFolder(model.fileId)
                            }
                        }
                    }
                }
            }
        }

        // ==================== 分页栏 ====================

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: "transparent"
            visible: FileListViewModel.totalPages > 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Item { Layout.fillWidth: true }

                Label {
                    text: "第 " + FileListViewModel.currentPage + " / " + FileListViewModel.totalPages + " 页"
                    font.pixelSize: 12
                    color: palette.placeholderText
                }

                Button {
                    text: "上一页"
                    font.pixelSize: 12
                    enabled: FileListViewModel.currentPage > 1
                    onClicked: FileListViewModel.goToPage(FileListViewModel.currentPage - 1)
                }

                Button {
                    text: "下一页"
                    font.pixelSize: 12
                    enabled: FileListViewModel.currentPage < FileListViewModel.totalPages
                    onClicked: FileListViewModel.goToPage(FileListViewModel.currentPage + 1)
                }

                Label {
                    text: "共 " + FileListViewModel.totalItems + " 项"
                    font.pixelSize: 12
                    color: palette.placeholderText
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    // ==================== 右键菜单 ====================

    Menu {
        id: contextMenu

        property int targetFileId: 0
        property string targetFileName: ""
        property bool targetIsFolder: false

        MenuItem {
            text: "📂 打开"
            visible: contextMenu.targetIsFolder
            onTriggered: FileListViewModel.navigateToFolder(contextMenu.targetFileId)
        }

        MenuItem {
            text: "⬇ 下载"
            visible: !contextMenu.targetIsFolder
            onTriggered: {
                // Stub — will be wired when download engine is connected
                console.log("Download requested:", contextMenu.targetFileId)
            }
        }

        MenuSeparator {}

        MenuItem {
            text: "✏️ 重命名"
            onTriggered: {
                renameDialog.openForFile(contextMenu.targetFileId, contextMenu.targetFileName)
            }
        }

        MenuItem {
            text: "📋 复制到..."
            onTriggered: {
                // Stub — will show folder picker in Task 14b
                console.log("Copy requested:", contextMenu.targetFileId)
            }
        }

        MenuItem {
            text: "📦 移动到..."
            onTriggered: {
                // Stub — will show folder picker in Task 14b
                console.log("Move requested:", contextMenu.targetFileId)
            }
        }

        MenuItem {
            text: "🔗 分享"
            visible: !contextMenu.targetIsFolder
            onTriggered: {
                // Stub — will show share dialog in Task 20
                console.log("Share requested:", contextMenu.targetFileId)
            }
        }

        MenuSeparator {}

        MenuItem {
            text: "🗑 删除"
            onTriggered: {
                deleteConfirmDialog.openForFiles([contextMenu.targetFileId], contextMenu.targetFileName)
            }
        }
    }

    // ==================== 对话框 ====================

    NewFolderDialog {
        id: newFolderDialog
    }

    RenameDialog {
        id: renameDialog
    }

    DeleteConfirmDialog {
        id: deleteConfirmDialog
    }

    // ==================== 操作结果提示 ====================

    Connections {
        target: FileListViewModel

        function onFileOperationSucceeded(message) {
            successTooltip.text = message
            successTooltip.visible = true
            successTooltipTimer.restart()
        }

        function onFileOperationFailed(message) {
            failTooltip.text = message
            failTooltip.visible = true
            failTooltipTimer.restart()
        }
    }

    // --- Success tooltip ---
    ToolTip {
        id: successTooltip
        timeout: 3000
        y: parent.height - 60
        x: (parent.width - width) / 2
    }

    Timer {
        id: successTooltipTimer
        interval: 3000
        onTriggered: successTooltip.visible = false
    }

    // --- Fail tooltip ---
    ToolTip {
        id: failTooltip
        timeout: 5000
        y: parent.height - 60
        x: (parent.width - width) / 2
    }

    Timer {
        id: failTooltipTimer
        interval: 5000
        onTriggered: failTooltip.visible = false
    }

}
