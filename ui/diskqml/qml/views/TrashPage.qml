/**
 * @file TrashPage.qml
 * @brief 回收站页 — 已删除文件列表、恢复、彻底删除、清空回收站
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    // ==================== 初始化 ====================

    Component.onCompleted: {
        TrashViewModel.refresh()
    }

    // ==================== 辅助函数 ====================

    // Formatter functions are now centralized in FormatUtils singleton

    function isExpiringSoon(expiresAt: string) : bool {
        if (!expiresAt) return false
        var now = new Date()
        var expiry = new Date(expiresAt)
        var diffMs = expiry.getTime() - now.getTime()
        var diffDays = diffMs / (1000 * 60 * 60 * 24)
        return diffDays >= 0 && diffDays <= 7
    }

    function daysUntilExpiry(expiresAt: string) : int {
        if (!expiresAt) return -1
        var now = new Date()
        var expiry = new Date(expiresAt)
        var diffMs = expiry.getTime() - now.getTime()
        return Math.ceil(diffMs / (1000 * 60 * 60 * 24))
    }

    // ==================== 主布局 ====================

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== 顶部工具条 ====================

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Label {
                    text: "🗑️ 回收站"
                    font.pixelSize: 20
                    font.bold: true
                    color: palette.windowText
                }

                Label {
                    text: TrashViewModel.totalItems > 0
                          ? "（共 " + TrashViewModel.totalItems + " 项）"
                          : ""
                    font.pixelSize: 13
                    color: palette.placeholderText
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "🔄 刷新"
                    font.pixelSize: 12
                    onClicked: TrashViewModel.refresh()
                }

                Button {
                    text: "🗑 清空回收站"
                    font.pixelSize: 12
                    enabled: TrashViewModel.trashListModel.count > 0 && !TrashViewModel.loading
                    onClicked: clearAllConfirmDialog.open()
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
            visible: TrashViewModel.hasSelection

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Label {
                    text: "已选择 " + TrashViewModel.selectionCount + " 项"
                    font.pixelSize: 13
                    color: palette.windowText
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "♻️ 恢复"
                    font.pixelSize: 12
                    onClicked: TrashViewModel.restoreSelected()
                }

                Button {
                    text: "🔥 彻底删除"
                    font.pixelSize: 12
                    onClicked: deleteSelectedConfirmDialog.open()
                }

                ToolButton {
                    text: "全选"
                    font.pixelSize: 12
                    onClicked: TrashViewModel.selectAll()
                }

                ToolButton {
                    text: "取消选择"
                    font.pixelSize: 12
                    onClicked: TrashViewModel.clearSelection()
                }
            }
        }

        // ==================== 内容区 ====================

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // --- 加载指示器 ---
            BusyIndicator {
                anchors.centerIn: parent
                running: TrashViewModel.loading
                visible: TrashViewModel.loading
            }

            // --- 错误状态 ---
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                visible: !TrashViewModel.loading && TrashViewModel.errorMessage !== ""

                Label {
                    text: "⚠️ " + TrashViewModel.errorMessage
                    font.pixelSize: 14
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Button {
                    text: "重试"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: TrashViewModel.refresh()
                }
            }

            // --- 空回收站状态 ---
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                visible: !TrashViewModel.loading
                         && TrashViewModel.errorMessage === ""
                         && TrashViewModel.trashListModel.count === 0

                Label {
                    text: "🗑️ 回收站为空"
                    font.pixelSize: 18
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: "删除的文件会在此处保留 30 天"
                    font.pixelSize: 13
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            // --- 列表视图 ---
            ListView {
                id: trashListView
                anchors.fill: parent
                visible: !TrashViewModel.loading
                         && TrashViewModel.errorMessage === ""
                         && TrashViewModel.trashListModel.count > 0
                clip: true
                model: TrashViewModel.trashListModel
                ScrollBar.vertical: ScrollBar {}

                // 列表头
                header: Rectangle {
                    width: trashListView.width
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
                            text: "原始路径"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 160
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: "删除时间"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 100
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: "到期"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                delegate: Rectangle {
                    id: trashRow
                    width: trashListView.width
                    height: 44
                    color: TrashViewModel.isSelected(model.trashId)
                           ? palette.highlight
                           : trashRowMa.containsMouse ? palette.midlight : "transparent"

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
                            text: FormatUtils.fileIcon(model.trashType, "")
                            font.pixelSize: 16
                            Layout.preferredWidth: 24
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                text: model.trashName
                                font.pixelSize: 13
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                                color: TrashViewModel.isSelected(model.trashId)
                                       ? palette.highlightedText : palette.windowText
                            }

                            // 即将过期警告
                            Label {
                                text: {
                                    var days = root.daysUntilExpiry(model.trashExpiresAt)
                                    if (days <= 0) return "⚠️ 即将自动删除"
                                    if (days <= 7) return "⚠️ " + days + " 天后自动删除"
                                    return ""
                                }
                                font.pixelSize: 10
                                color: "#e74c3c"
                                visible: root.isExpiringSoon(model.trashExpiresAt)
                                Layout.fillWidth: true
                            }
                        }

                        // 大小
                        Label {
                            text: model.trashIsFolder ? "文件夹" : FormatUtils.formatSize(model.trashSize)
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignRight
                        }

                        // 原始路径
                        Label {
                            text: model.trashOriginalPath || "-"
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 160
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideMiddle
                        }

                        // 删除时间
                        Label {
                            text: FormatUtils.formatDate(model.trashDeletedAt)
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 100
                            horizontalAlignment: Text.AlignRight
                        }

                        // 到期时间
                        Label {
                            text: FormatUtils.formatDate(model.trashExpiresAt)
                            font.pixelSize: 12
                            color: root.isExpiringSoon(model.trashExpiresAt) ? "#e74c3c" : palette.placeholderText
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignRight
                        }
                    }

                    MouseArea {
                        id: trashRowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton

                        onClicked: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                trashContextMenu.targetTrashId = model.trashId
                                trashContextMenu.targetTrashName = model.trashName
                                trashContextMenu.popup()
                            } else if (mouse.modifiers & Qt.ControlModifier) {
                                TrashViewModel.toggleSelection(model.trashId)
                            } else {
                                TrashViewModel.clearSelection()
                                TrashViewModel.toggleSelection(model.trashId)
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
            visible: TrashViewModel.totalPages > 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Item { Layout.fillWidth: true }

                Label {
                    text: "第 " + TrashViewModel.currentPage + " / " + TrashViewModel.totalPages + " 页"
                    font.pixelSize: 12
                    color: palette.placeholderText
                }

                Button {
                    text: "上一页"
                    font.pixelSize: 12
                    enabled: TrashViewModel.currentPage > 1
                    onClicked: TrashViewModel.goToPage(TrashViewModel.currentPage - 1)
                }

                Button {
                    text: "下一页"
                    font.pixelSize: 12
                    enabled: TrashViewModel.currentPage < TrashViewModel.totalPages
                    onClicked: TrashViewModel.goToPage(TrashViewModel.currentPage + 1)
                }

                Label {
                    text: "共 " + TrashViewModel.totalItems + " 项"
                    font.pixelSize: 12
                    color: palette.placeholderText
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    // ==================== 右键菜单 ====================

    Menu {
        id: trashContextMenu

        property int targetTrashId: 0
        property string targetTrashName: ""

        MenuItem {
            text: "♻️ 恢复"
            onTriggered: {
                TrashViewModel.clearSelection()
                TrashViewModel.toggleSelection(trashContextMenu.targetTrashId)
                TrashViewModel.restoreSelected()
            }
        }

        MenuSeparator {}

        MenuItem {
            text: "🔥 彻底删除"
            onTriggered: {
                TrashViewModel.clearSelection()
                TrashViewModel.toggleSelection(trashContextMenu.targetTrashId)
                deleteSelectedConfirmDialog.open()
            }
        }
    }

    // ==================== 确认对话框 ====================

    Dialog {
        id: clearAllConfirmDialog
        title: "清空回收站"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        Label {
            text: "确定要清空回收站吗？\n此操作不可恢复。"
            wrapMode: Text.Wrap
        }

        onAccepted: TrashViewModel.clearAll()
    }

    Dialog {
        id: deleteSelectedConfirmDialog
        title: "彻底删除"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel

        Label {
            text: "确定要彻底删除选中的 " + TrashViewModel.selectionCount + " 项吗？\n此操作不可恢复。"
            wrapMode: Text.Wrap
        }

        onAccepted: TrashViewModel.deleteSelected()
    }

    // ==================== 操作结果提示 ====================

    Connections {
        target: TrashViewModel

        function onTrashOperationSucceeded(message) {
            successTooltip.text = message
            successTooltip.visible = true
            successTooltipTimer.restart()
        }

        function onTrashOperationFailed(message) {
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
