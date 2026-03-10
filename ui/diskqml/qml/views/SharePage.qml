/**
 * @file SharePage.qml
 * @brief 分享页 - 分享列表、创建/取消分享
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    // ==================== 初始化 ====================

    Component.onCompleted: {
        ShareViewModel.refresh()
    }

    // ==================== 辅助函数 ====================

    function formatDate(dateStr: string) : string {
        if (!dateStr) return "-"
        // Truncate to first 10 chars: "2026-02-15"
        return dateStr.substring(0, 10)
    }

    function permissionLabel(permission: string) : string {
        if (permission === "view") return "仅查看"
        if (permission === "download") return "可下载"
        return permission
    }

    function statusLabel(status: string) : string {
        if (status === "active") return "有效"
        if (status === "expired") return "已过期"
        if (status === "cancelled") return "已取消"
        return status
    }

    function statusColor(status: string) : color {
        if (status === "active") return "#4CAF50"
        if (status === "expired") return palette.placeholderText
        if (status === "cancelled") return palette.placeholderText
        return palette.windowText
    }

    // ==================== 主布局 ====================

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== 标题栏 + 创建按钮 ====================

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 16

                Label {
                    text: "分享"
                    font.pixelSize: 20
                    font.bold: true
                    color: palette.windowText
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "➕ 创建分享"
                    font.pixelSize: 13
                    enabled: !ShareViewModel.loading
                    onClicked: createShareDialog.open()
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
            visible: ShareViewModel.hasSelection

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 8

                Label {
                    text: "已选择 " + ShareViewModel.selectionCount + " 项"
                    font.pixelSize: 13
                    color: palette.windowText
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "取消分享"
                    font.pixelSize: 12
                    enabled: !ShareViewModel.loading && ShareViewModel.hasSelection
                    onClicked: cancelConfirmDialog.open()
                }

                ToolButton {
                    text: "取消选择"
                    font.pixelSize: 12
                    onClicked: ShareViewModel.clearSelection()
                }
            }
        }

        // ==================== 内容区（加载 / 错误 / 空 / 列表）====================

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // --- 加载指示器 ---
            BusyIndicator {
                anchors.centerIn: parent
                running: ShareViewModel.loading
                visible: ShareViewModel.loading
            }

            // --- 错误状态 ---
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                visible: !ShareViewModel.loading && ShareViewModel.errorMessage !== ""

                Label {
                    text: "⚠️ " + ShareViewModel.errorMessage
                    font.pixelSize: 14
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Button {
                    text: "重试"
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: ShareViewModel.refresh()
                }
            }

            // --- 空状态 ---
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 8
                visible: !ShareViewModel.loading
                         && ShareViewModel.errorMessage === ""
                         && ShareViewModel.shareListModel.count === 0

                Label {
                    text: "📭 暂无分享"
                    font.pixelSize: 18
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: "点击「创建分享」按钮分享您的文件"
                    font.pixelSize: 13
                    color: palette.placeholderText
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            // --- 列表视图 ---
            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: 16
                visible: !ShareViewModel.loading
                         && ShareViewModel.errorMessage === ""
                         && ShareViewModel.shareListModel.count > 0
                clip: true
                model: ShareViewModel.shareListModel
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
                            text: "文件名"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.fillWidth: true
                        }

                        Label {
                            text: "权限"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 70
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "密码"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "访问"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "下载"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "创建时间"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "过期时间"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "状态"
                            font.pixelSize: 12
                            font.bold: true
                            color: palette.placeholderText
                            Layout.preferredWidth: 60
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                delegate: Rectangle {
                    id: listRow
                    width: listView.width
                    height: 44
                    color: ShareViewModel.isSelected(model.shareId)
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

                        // 文件名
                        Label {
                            text: model.shareFileCount > 1
                                  ? model.shareFileName + " (+" + (model.shareFileCount - 1) + ")"
                                  : model.shareFileName
                            font.pixelSize: 13
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            color: ShareViewModel.isSelected(model.shareId)
                                   ? palette.highlightedText : palette.windowText
                        }

                        // 权限
                        Label {
                            text: root.permissionLabel(model.sharePermission)
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 70
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 密码
                        Label {
                            text: model.shareHasPassword ? "🔒" : "-"
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 访问次数
                        Label {
                            text: model.shareViewCount
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 下载次数
                        Label {
                            text: model.shareDownloadCount
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 创建时间
                        Label {
                            text: root.formatDate(model.shareCreatedAt)
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 过期时间
                        Label {
                            text: root.formatDate(model.shareExpiresAt)
                            font.pixelSize: 12
                            color: palette.placeholderText
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 状态
                        Label {
                            text: root.statusLabel(model.shareStatus)
                            font.pixelSize: 12
                            color: root.statusColor(model.shareStatus)
                            Layout.preferredWidth: 60
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        id: listRowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton

                        onClicked: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                contextMenu.targetShareId = model.shareId
                                contextMenu.targetShareLink = model.shareLink
                                contextMenu.targetShareStatus = model.shareStatus
                                contextMenu.targetSharePermission = model.sharePermission
                                contextMenu.targetShareHasPassword = model.shareHasPassword
                                contextMenu.targetShareExpiresAt = model.shareExpiresAt
                                contextMenu.popup()
                            } else if (mouse.modifiers & Qt.ControlModifier) {
                                ShareViewModel.toggleSelection(model.shareId)
                            } else {
                                ShareViewModel.clearSelection()
                                ShareViewModel.toggleSelection(model.shareId)
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
            visible: ShareViewModel.totalPages > 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                spacing: 8

                Item { Layout.fillWidth: true }

                Label {
                    text: "第 " + ShareViewModel.currentPage + " / " + ShareViewModel.totalPages + " 页"
                    font.pixelSize: 12
                    color: palette.placeholderText
                }

                Button {
                    text: "上一页"
                    font.pixelSize: 12
                    enabled: ShareViewModel.currentPage > 1 && !ShareViewModel.loading
                    onClicked: ShareViewModel.goToPage(ShareViewModel.currentPage - 1)
                }

                Button {
                    text: "下一页"
                    font.pixelSize: 12
                    enabled: ShareViewModel.currentPage < ShareViewModel.totalPages && !ShareViewModel.loading
                    onClicked: ShareViewModel.goToPage(ShareViewModel.currentPage + 1)
                }

                Label {
                    text: "共 " + ShareViewModel.totalItems + " 项"
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

        property string targetShareId: ""
        property string targetShareLink: ""
        property string targetShareStatus: ""
        property string targetSharePermission: ""
        property bool targetShareHasPassword: false
        property string targetShareExpiresAt: ""

        MenuItem {
            text: "📋 复制链接"
            onTriggered: {
                if (contextMenu.targetShareLink) {
                    clipboardText.text = contextMenu.targetShareLink
                    clipboardText.selectAll()
                    clipboardText.copy()
                    successTooltip.text = "分享链接已复制到剪贴板"
                    successTooltip.visible = true
                    successTooltipTimer.restart()
                }
            }
        }

        MenuItem {
            text: "✏️ 编辑"
            enabled: contextMenu.targetShareStatus === "active"
            onTriggered: {
                editShareDialog.openForShare(
                    contextMenu.targetShareId,
                    contextMenu.targetSharePermission,
                    contextMenu.targetShareHasPassword,
                    contextMenu.targetShareExpiresAt
                )
            }
        }

        MenuSeparator {}

        MenuItem {
            text: "❌ 取消分享"
            enabled: contextMenu.targetShareStatus === "active"
            onTriggered: {
                ShareViewModel.clearSelection()
                ShareViewModel.toggleSelection(contextMenu.targetShareId)
                cancelConfirmDialog.open()
            }
        }
    }

    // ==================== 创建分享对话框 ====================

    Dialog {
        id: createShareDialog
        title: "创建分享"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 400

        // 表单内容
        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Label {
                text: "分享设置"
                font.pixelSize: 14
                font.bold: true
                color: palette.windowText
            }

            // 文件ID输入（临时方案，后续应从文件列表选择）
            Label {
                text: "文件ID（多个用逗号分隔）："
                font.pixelSize: 12
                color: palette.windowText
            }

            TextField {
                id: fileIdsField
                Layout.fillWidth: true
                placeholderText: "例如: 1,2,3"
                font.pixelSize: 13
            }

            Label {
                text: "有效期："
                font.pixelSize: 12
                color: palette.windowText
            }

            ComboBox {
                id: expireDaysCombo
                Layout.fillWidth: true
                model: [
                    { text: "1 天", value: 1 },
                    { text: "7 天", value: 7 },
                    { text: "30 天", value: 30 },
                    { text: "永久", value: 365 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: 1  // 默认7天
            }

            Label {
                text: "访问密码（可选，4-8字符）："
                font.pixelSize: 12
                color: palette.windowText
            }

            TextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: "留空表示无密码"
                font.pixelSize: 13
                echoMode: TextInput.Password
                maximumLength: 8
            }

            Label {
                text: "权限："
                font.pixelSize: 12
                color: palette.windowText
            }

            ComboBox {
                id: permissionCombo
                Layout.fillWidth: true
                model: [
                    { text: "仅查看", value: "view" },
                    { text: "可下载", value: "download" }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: 1  // 默认可下载
            }
        }

        onAccepted: {
            // 解析文件ID
            var fileIdsText = fileIdsField.text.trim()
            if (!fileIdsText) {
                failTooltip.text = "请输入文件ID"
                failTooltip.visible = true
                failTooltipTimer.restart()
                return
            }

            var fileIdsArray = fileIdsText.split(",").map(function(id) {
                return parseInt(id.trim(), 10)
            }).filter(function(id) {
                return !isNaN(id) && id > 0
            })

            if (fileIdsArray.length === 0) {
                failTooltip.text = "请输入有效的文件ID"
                failTooltip.visible = true
                failTooltipTimer.restart()
                return
            }

            // 获取表单值
            var expireDays = expireDaysCombo.currentValue
            var password = passwordField.text.trim()
            var permission = permissionCombo.currentValue

            // 调用ViewModel
            ShareViewModel.createShare(fileIdsArray, expireDays, password, permission)

            // 清空表单
            fileIdsField.text = ""
            passwordField.text = ""
            expireDaysCombo.currentIndex = 1
            permissionCombo.currentIndex = 1
        }

        onRejected: {
            // 清空表单
            fileIdsField.text = ""
            passwordField.text = ""
            expireDaysCombo.currentIndex = 1
            permissionCombo.currentIndex = 1
        }
    }

    // ==================== 取消分享确认对话框 ====================

    Dialog {
        id: cancelConfirmDialog
        title: "确认取消分享"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        width: 320

        Label {
            text: "确定要取消选中的 " + ShareViewModel.selectionCount + " 个分享吗？\n取消后，分享链接将立即失效。"
            font.pixelSize: 13
            color: palette.windowText
            wrapMode: Text.Wrap
        }

        onAccepted: {
            ShareViewModel.cancelSelected()
        }
    }

    // ==================== 编辑分享对话框 ====================

    Dialog {
        id: editShareDialog
        title: "✏️ 编辑分享"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 400

        property string targetShareId: ""
        property string originalPermission: ""
        property bool originalHasPassword: false
        property string originalExpiresAt: ""

        function openForShare(shareId, permission, hasPassword, expiresAt) {
            targetShareId = shareId
            originalPermission = permission
            originalHasPassword = hasPassword
            originalExpiresAt = expiresAt

            // Set permission combo
            if (permission === "view") {
                editPermissionCombo.currentIndex = 0
            } else {
                editPermissionCombo.currentIndex = 1
            }

            // Clear password field (user can set new password)
            editPasswordField.text = ""
            editPasswordClearCheckbox.checked = false

            // Default to 7 days if editing
            editExpireDaysCombo.currentIndex = 1

            editShareDialog.open()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Label {
                text: "有效期："
                font.pixelSize: 12
                color: palette.windowText
            }

            ComboBox {
                id: editExpireDaysCombo
                Layout.fillWidth: true
                model: [
                    { text: "1 天", value: 1 },
                    { text: "7 天", value: 7 },
                    { text: "30 天", value: 30 },
                    { text: "永久", value: 365 }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: 1
            }

            Label {
                text: "访问密码（留空保持不变，勾选清除）："
                font.pixelSize: 12
                color: palette.windowText
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: editPasswordField
                    Layout.fillWidth: true
                    placeholderText: editShareDialog.originalHasPassword ? "当前已设置密码" : "当前无密码"
                    font.pixelSize: 13
                    echoMode: TextInput.Password
                    maximumLength: 8
                    enabled: !editPasswordClearCheckbox.checked
                }

                CheckBox {
                    id: editPasswordClearCheckbox
                    text: "清除密码"
                    visible: editShareDialog.originalHasPassword
                }
            }

            Label {
                text: "权限："
                font.pixelSize: 12
                color: palette.windowText
            }

            ComboBox {
                id: editPermissionCombo
                Layout.fillWidth: true
                model: [
                    { text: "仅查看", value: "view" },
                    { text: "可下载", value: "download" }
                ]
                textRole: "text"
                valueRole: "value"
            }
        }

        onAccepted: {
            var expireDays = editExpireDaysCombo.currentValue
            var permission = editPermissionCombo.currentValue

            // Handle password: clear, keep, or set new
            var password = ""
            if (editPasswordClearCheckbox.checked) {
                // Pass special marker to clear password
                password = "__CLEAR__"
            } else if (editPasswordField.text.trim() !== "") {
                // New password provided
                password = editPasswordField.text.trim()
            }
            // else: keep existing (empty string means no change)

            ShareViewModel.updateShare(editShareDialog.targetShareId, expireDays, password, permission)
        }

        onRejected: {
            editPasswordField.text = ""
            editPasswordClearCheckbox.checked = false
        }
    }

    // ==================== 分享详情对话框 ====================

    Dialog {
        id: shareDetailsDialog
        title: "分享创建成功"
        modal: true
        standardButtons: Dialog.Close
        width: 450

        property string shareLink: ""
        property string sharePassword: ""
        property string shareExpiresAt: ""

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Label {
                text: "分享链接："
                font.pixelSize: 12
                font.bold: true
                color: palette.windowText
            }

            TextField {
                id: shareLinkField
                Layout.fillWidth: true
                text: shareDetailsDialog.shareLink
                font.pixelSize: 12
                readOnly: true
                selectByMouse: true
            }

            Label {
                text: "访问密码："
                font.pixelSize: 12
                font.bold: true
                color: palette.windowText
                visible: shareDetailsDialog.sharePassword !== ""
            }

            TextField {
                id: sharePasswordField
                Layout.fillWidth: true
                text: shareDetailsDialog.sharePassword
                font.pixelSize: 12
                readOnly: true
                selectByMouse: true
                visible: shareDetailsDialog.sharePassword !== ""
            }

            Label {
                text: "过期时间：" + shareDetailsDialog.shareExpiresAt
                font.pixelSize: 12
                color: palette.placeholderText
            }

            Button {
                text: "复制链接" + (shareDetailsDialog.sharePassword ? "和密码" : "")
                Layout.alignment: Qt.AlignHCenter
                onClicked: {
                    var copyText = shareDetailsDialog.shareLink
                    if (shareDetailsDialog.sharePassword) {
                        copyText += "\n密码：" + shareDetailsDialog.sharePassword
                    }
                    clipboardText.text = copyText
                    clipboardText.selectAll()
                    clipboardText.copy()
                    successTooltip.text = "已复制到剪贴板"
                    successTooltip.visible = true
                    successTooltipTimer.restart()
                }
            }
        }
    }

    // ==================== 隐藏的剪贴板辅助对象 ====================

    TextEdit {
        id: clipboardText
        visible: false
    }

    // ==================== 操作结果提示 ====================

    Connections {
        target: ShareViewModel

        function onShareOperationSucceeded(message) {
            successTooltip.text = message
            successTooltip.visible = true
            successTooltipTimer.restart()
        }

        function onShareOperationFailed(message) {
            failTooltip.text = message
            failTooltip.visible = true
            failTooltipTimer.restart()
        }

        function onShareCreated(shareId, shareLink, password, expiresAt) {
            shareDetailsDialog.shareLink = shareLink
            shareDetailsDialog.sharePassword = password
            shareDetailsDialog.shareExpiresAt = expiresAt
            shareDetailsDialog.open()
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

}