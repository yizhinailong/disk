/**
 * @file TrashPage.qml
 * @brief 回收站页 — 已删除文件列表、恢复、彻底删除、清空回收站
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0
import "../tokens"
import "../components/primitives"
import "../components"

Item {
    id: root

    // ==================== 初始化 ====================

    Component.onCompleted: {
        TrashViewModel.refresh()
    }

    // ==================== 辅助函数 ====================

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
            Layout.preferredHeight: StyleTokens.titleBarHeight
            color: StyleTokens.colorSurface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: StyleTokens.spacingLg
                anchors.rightMargin: StyleTokens.spacingLg
                spacing: StyleTokens.spacingMd

                Label {
                    text: "🗑️ 回收站"
                    font.pixelSize: StyleTokens.fontSizeH1
                    font.weight: StyleTokens.fontWeightH1
                    color: StyleTokens.colorTextPrimary
                }

                Label {
                    text: TrashViewModel.totalItems > 0
                          ? "（共 " + TrashViewModel.totalItems + " 项）"
                          : ""
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorTextSecondary
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "🔄 刷新"
                    variant: "secondary"
                    onClicked: TrashViewModel.refresh()
                }

                AppButton {
                    text: "🗑 清空回收站"
                    variant: "secondary"
                    enabled: TrashViewModel.trashListModel.count > 0 && !TrashViewModel.loading
                    onClicked: clearAllConfirmDialog.open()
                }
            }
        }

        // --- 顶部分隔线 ---
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: StyleTokens.colorBorder
        }

        // ==================== 多选操作栏 ====================

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: StyleTokens.toolBarHeight
            color: StyleTokens.colorPrimaryLight
            visible: TrashViewModel.hasSelection

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: StyleTokens.spacingLg
                anchors.rightMargin: StyleTokens.spacingLg
                spacing: StyleTokens.spacingSm

                Label {
                    text: "已选择 " + TrashViewModel.selectionCount + " 项"
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorPrimary
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "♻️ 恢复"
                    variant: "secondary"
                    onClicked: TrashViewModel.restoreSelected()
                }

                AppButton {
                    text: "🔥 彻底删除"
                    variant: "secondary"
                    onClicked: deleteSelectedConfirmDialog.open()
                }

                AppButton {
                    text: "全选"
                    variant: "secondary"
                    onClicked: TrashViewModel.selectAll()
                }

                AppButton {
                    text: "取消选择"
                    variant: "secondary"
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
                spacing: StyleTokens.spacingMd
                visible: !TrashViewModel.loading && TrashViewModel.errorMessage !== ""

                Label {
                    text: "⚠️ " + TrashViewModel.errorMessage
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorError
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                AppButton {
                    text: "重试"
                    variant: "secondary"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: TrashViewModel.refresh()
                }
            }

            // --- 空回收站状态 ---
            ColumnLayout {
                anchors.centerIn: parent
                spacing: StyleTokens.spacingSm
                visible: !TrashViewModel.loading
                         && TrashViewModel.errorMessage === ""
                         && TrashViewModel.trashListModel.count === 0

                Label {
                    text: "🗑️ 回收站为空"
                    font.pixelSize: StyleTokens.fontSizeH1
                    color: StyleTokens.colorTextTertiary
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: "删除的文件会在此处保留 30 天"
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorTextTertiary
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            // --- 列表视图 ---
            ListView {
                id: trashListView
                anchors.fill: parent
                anchors.margins: StyleTokens.spacingMd
                visible: !TrashViewModel.loading
                         && TrashViewModel.errorMessage === ""
                         && TrashViewModel.trashListModel.count > 0
                clip: true
                model: TrashViewModel.trashListModel
                ScrollBar.vertical: ScrollBar {}

                // 列表头
                header: Rectangle {
                    width: trashListView.width
                    height: 40
                    color: StyleTokens.colorSurface

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: StyleTokens.colorBorder
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: StyleTokens.spacingMd
                        anchors.rightMargin: StyleTokens.spacingMd
                        spacing: StyleTokens.spacingSm

                        Label {
                            text: "名称"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.fillWidth: true
                        }

                        Label {
                            text: "大小"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: "原始路径"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 160
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: "删除时间"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 120
                            horizontalAlignment: Text.AlignRight
                        }

                        Label {
                            text: "到期"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 100
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }

                delegate: Rectangle {
                    id: trashRow
                    width: trashListView.width
                    height: 48
                    color: TrashViewModel.isSelected(model.trashId)
                           ? StyleTokens.colorPrimaryLight
                           : trashRowMa.containsMouse ? StyleTokens.colorHover : "transparent"

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: StyleTokens.colorBorder
                        opacity: 0.5
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: StyleTokens.spacingMd
                        anchors.rightMargin: StyleTokens.spacingMd
                        spacing: StyleTokens.spacingSm

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
                                font.pixelSize: StyleTokens.fontSizeBody
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                                color: TrashViewModel.isSelected(model.trashId)
                                       ? StyleTokens.colorPrimary : StyleTokens.colorTextPrimary
                            }

                            // 即将过期警告
                            Label {
                                text: {
                                    var days = root.daysUntilExpiry(model.trashExpiresAt)
                                    if (days <= 0) return "⚠️ 即将自动删除"
                                    if (days <= 7) return "⚠️ " + days + " 天后自动删除"
                                    return ""
                                }
                                font.pixelSize: StyleTokens.fontSizeSmall
                                color: StyleTokens.colorError
                                visible: root.isExpiringSoon(model.trashExpiresAt)
                                Layout.fillWidth: true
                            }
                        }

                        // 大小
                        Label {
                            text: model.trashIsFolder ? "文件夹" : FormatUtils.formatSize(model.trashSize)
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignRight
                        }

                        // 原始路径
                        Label {
                            text: model.trashOriginalPath || "-"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 160
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideMiddle
                        }

                        // 删除时间
                        Label {
                            text: FormatUtils.formatDate(model.trashDeletedAt)
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 120
                            horizontalAlignment: Text.AlignRight
                        }

                        // 到期时间
                        Label {
                            text: FormatUtils.formatDate(model.trashExpiresAt)
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: root.isExpiringSoon(model.trashExpiresAt) ? StyleTokens.colorError : StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 100
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

        PaginationBar {
            currentPage: TrashViewModel.currentPage
            totalPages: TrashViewModel.totalPages
            totalItems: TrashViewModel.totalItems
            onPageRequested: function(page) { TrashViewModel.goToPage(page) }
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
            font.pixelSize: StyleTokens.fontSizeBody
            color: StyleTokens.colorTextPrimary
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
            font.pixelSize: StyleTokens.fontSizeBody
            color: StyleTokens.colorTextPrimary
            wrapMode: Text.Wrap
        }

        onAccepted: TrashViewModel.deleteSelected()
    }

    // ==================== 操作结果提示 ====================

    Connections {
        target: TrashViewModel

        function onTrashOperationSucceeded(message) {
            notificationToast.showSuccess(message)
        }

        function onTrashOperationFailed(message) {
            notificationToast.showError(message)
        }
    }

    NotificationToast {
        id: notificationToast
    }
}
