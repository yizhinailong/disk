/**
 * @file SharePage.qml
 * @brief 分享页 - 分享列表、创建/取消分享
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "../tokens"
import "../components/primitives"
import "../components"

Item {
    id: root

    // ==================== 初始化 ====================

    Component.onCompleted: {
        ShareViewModel.refresh()
    }

    // ==================== 辅助函数 ====================

    function statusColor(status: string) : color {
        if (status === "active") return StyleTokens.colorSuccess
        if (status === "expired") return StyleTokens.colorTextTertiary
        if (status === "cancelled") return StyleTokens.colorTextTertiary
        return StyleTokens.colorTextPrimary
    }

    function statusBadgeType(status: string) : string {
        if (status === "active") return "success"
        if (status === "expired") return "warning"
        if (status === "cancelled") return "error"
        return "info"
    }

    // ==================== 主布局 ====================

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== 标题栏 + 创建按钮 ====================

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
                    text: "分享"
                    font.pixelSize: StyleTokens.fontSizeH1
                    font.weight: StyleTokens.fontWeightH1
                    color: StyleTokens.colorTextPrimary
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "➕ 创建分享"
                    variant: "primary"
                    enabled: !ShareViewModel.loading
                    onClicked: createShareDialog.open()
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
            visible: ShareViewModel.hasSelection

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: StyleTokens.spacingLg
                anchors.rightMargin: StyleTokens.spacingLg
                spacing: StyleTokens.spacingSm

                Label {
                    text: "已选择 " + ShareViewModel.selectionCount + " 项"
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorPrimary
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "取消分享"
                    variant: "secondary"
                    enabled: !ShareViewModel.loading && ShareViewModel.hasSelection
                    onClicked: cancelConfirmDialog.open()
                }

                AppButton {
                    text: "取消选择"
                    variant: "secondary"
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
                spacing: StyleTokens.spacingMd
                visible: !ShareViewModel.loading && ShareViewModel.errorMessage !== ""

                Label {
                    text: "⚠️ " + ShareViewModel.errorMessage
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
                    onClicked: ShareViewModel.refresh()
                }
            }

            // --- 空状态 ---
            ColumnLayout {
                anchors.centerIn: parent
                spacing: StyleTokens.spacingSm
                visible: !ShareViewModel.loading
                         && ShareViewModel.errorMessage === ""
                         && ShareViewModel.shareListModel.count === 0

                Label {
                    text: "📭 暂无分享"
                    font.pixelSize: StyleTokens.fontSizeH1
                    color: StyleTokens.colorTextTertiary
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: "点击「创建分享」按钮分享您的文件"
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: StyleTokens.colorTextTertiary
                    Layout.alignment: Qt.AlignHCenter
                }
            }

            // --- 列表视图 ---
            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: StyleTokens.spacingMd
                visible: !ShareViewModel.loading
                         && ShareViewModel.errorMessage === ""
                         && ShareViewModel.shareListModel.count > 0
                clip: true
                model: ShareViewModel.shareListModel
                ScrollBar.vertical: ScrollBar {}

                // 列表头
                header: Rectangle {
                    width: listView.width
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
                            text: "文件名"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.fillWidth: true
                        }

                        Label {
                            text: "权限"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 70
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "密码"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "访问"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "下载"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "创建时间"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 120
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "过期时间"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 120
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Label {
                            text: "状态"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            font.weight: StyleTokens.fontWeightH3
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                delegate: Rectangle {
                    id: listRow
                    width: listView.width
                    height: 48
                    color: ShareViewModel.isSelected(model.shareId)
                           ? StyleTokens.colorPrimaryLight
                           : listRowMa.containsMouse ? StyleTokens.colorHover : "transparent"

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

                        // 文件名
                        Label {
                            text: model.shareFileCount > 1
                                  ? model.shareFileName + " (+" + (model.shareFileCount - 1) + ")"
                                  : model.shareFileName
                            font.pixelSize: StyleTokens.fontSizeBody
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            color: ShareViewModel.isSelected(model.shareId)
                                   ? StyleTokens.colorPrimary : StyleTokens.colorTextPrimary
                        }

                        // 权限
                        Label {
                            text: FormatUtils.permissionLabel(model.sharePermission)
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 70
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 密码
                        Label {
                            text: model.shareHasPassword ? "🔒" : "-"
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 访问次数
                        Label {
                            text: model.shareViewCount
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 下载次数
                        Label {
                            text: model.shareDownloadCount
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 50
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 创建时间
                        Label {
                            text: FormatUtils.formatDate(model.shareCreatedAt)
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 120
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 过期时间
                        Label {
                            text: FormatUtils.formatDate(model.shareExpiresAt)
                            font.pixelSize: StyleTokens.fontSizeSmall
                            color: StyleTokens.colorTextSecondary
                            Layout.preferredWidth: 120
                            horizontalAlignment: Text.AlignHCenter
                        }

                        // 状态
                        Item {
                            Layout.preferredWidth: 80
                            Layout.fillHeight: true
                            AppBadge {
                                anchors.centerIn: parent
                                text: FormatUtils.statusLabel(model.shareStatus)
                                status: root.statusBadgeType(model.shareStatus)
                            }
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
                                // 注意：shareFileId 和 shareToken 无法从列表中获取
                                // 通过分享访问下载需要单独的流程

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
        }

        // ==================== 分页栏 ====================

        PaginationBar {
            currentPage: ShareViewModel.currentPage
            totalPages: ShareViewModel.totalPages
            totalItems: ShareViewModel.totalItems
            loading: ShareViewModel.loading
            onPageRequested: function(page) { ShareViewModel.goToPage(page) }
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
        // 注意：移除了 targetFileId 和 targetShareToken - 无法从分享列表中获取
        // 要下载分享的文件，请使用分享访问流程（未在列表视图中实现）

        MenuItem {
            id: downloadShareMenuItem
            text: "📥 下载"
            enabled: false  // 禁用：无法从列表中获取分享令牌；使用文件列表下载
            onTriggered: {
                // 此功能需要分享访问令牌，而该令牌无法从分享列表中获取。
                // 请使用文件列表下载文件。
            }
        }

        MenuItem {
            text: "📋 复制链接"
            onTriggered: {
                if (contextMenu.targetShareLink) {
                    clipboardText.text = contextMenu.targetShareLink
                    clipboardText.selectAll()
                    clipboardText.copy()
                    notificationToast.showSuccess("分享链接已复制到剪贴板")
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
            spacing: StyleTokens.spacingMd

            Label {
                text: "分享设置"
                font.pixelSize: StyleTokens.fontSizeH3
                font.weight: StyleTokens.fontWeightH3
                color: StyleTokens.colorTextPrimary
            }

            // 文件ID输入（高级选项，建议从文件列表右键创建分享）
            Label {
                text: "文件ID（高级选项）："
                font.pixelSize: StyleTokens.fontSizeSmall
                color: StyleTokens.colorTextSecondary
            }

            AppTextInput {
                id: fileIdsField
                Layout.fillWidth: true
                placeholderText: "建议从文件列表右键分享"
            }

            Label {
                text: "💡 提示：在文件列表中右键点击文件即可快速创建分享"
                font.pixelSize: StyleTokens.fontSizeSmall
                color: StyleTokens.colorTextTertiary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label {
                text: "有效期："
                font.pixelSize: StyleTokens.fontSizeBody
                color: StyleTokens.colorTextPrimary
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
                font.pixelSize: StyleTokens.fontSizeBody
                color: StyleTokens.colorTextPrimary
            }

            AppTextInput {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: "留空表示无密码"
                echoMode: TextInput.Password
                maximumLength: 8
            }

            Label {
                text: "权限："
                font.pixelSize: StyleTokens.fontSizeBody
                color: StyleTokens.colorTextPrimary
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
                notificationToast.showError("请输入文件ID")
                return
            }

            var fileIdsArray = fileIdsText.split(",").map(function(id) {
                return parseInt(id.trim(), 10)
            }).filter(function(id) {
                return !isNaN(id) && id > 0
            })

            if (fileIdsArray.length === 0) {
                notificationToast.showError("请输入有效的文件ID")
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
            font.pixelSize: StyleTokens.fontSizeBody
            color: StyleTokens.colorTextPrimary
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

            // 设置权限下拉框
            if (permission === "view") {
                editPermissionCombo.currentIndex = 0
            } else {
                editPermissionCombo.currentIndex = 1
            }

            // 清空密码字段（用户可以设置新密码）
            editPasswordField.text = ""
            editPasswordClearCheckbox.checked = false

            // 编辑时默认为7天
            editExpireDaysCombo.currentIndex = 1

            editShareDialog.open()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: StyleTokens.spacingMd

            Label {
                text: "有效期："
                font.pixelSize: StyleTokens.fontSizeBody
                color: StyleTokens.colorTextPrimary
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
                font.pixelSize: StyleTokens.fontSizeBody
                color: StyleTokens.colorTextPrimary
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: StyleTokens.spacingSm

                AppTextInput {
                    id: editPasswordField
                    Layout.fillWidth: true
                    placeholderText: editShareDialog.originalHasPassword ? "当前已设置密码" : "当前无密码"
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
                font.pixelSize: StyleTokens.fontSizeBody
                color: StyleTokens.colorTextPrimary
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

            // 根据 UI 状态确定密码操作：
            // - 勾选清除复选框 -> 清除（移除密码）
            // - 密码字段有文本 -> 设置（新密码）
            // - 否则 -> 保持（无变化）
            var passwordAction = ShareViewModel.PasswordAction.Keep
            var password = ""

            if (editPasswordClearCheckbox.checked) {
                passwordAction = ShareViewModel.PasswordAction.Clear
            } else if (editPasswordField.text.trim() !== "") {
                passwordAction = ShareViewModel.PasswordAction.Set
                password = editPasswordField.text.trim()
            }

            ShareViewModel.updateShare(editShareDialog.targetShareId, expireDays, passwordAction, password, permission)
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
            spacing: StyleTokens.spacingMd

            Label {
                text: "分享链接："
                font.pixelSize: StyleTokens.fontSizeBody
                font.weight: StyleTokens.fontWeightH3
                color: StyleTokens.colorTextPrimary
            }

            AppTextInput {
                id: shareLinkField
                Layout.fillWidth: true
                text: shareDetailsDialog.shareLink
                readOnly: true
                selectByMouse: true
            }

            Label {
                text: "访问密码："
                font.pixelSize: StyleTokens.fontSizeBody
                font.weight: StyleTokens.fontWeightH3
                color: StyleTokens.colorTextPrimary
                visible: shareDetailsDialog.sharePassword !== ""
            }

            AppTextInput {
                id: sharePasswordField
                Layout.fillWidth: true
                text: shareDetailsDialog.sharePassword
                readOnly: true
                selectByMouse: true
                visible: shareDetailsDialog.sharePassword !== ""
            }

            Label {
                text: "过期时间：" + shareDetailsDialog.shareExpiresAt
                font.pixelSize: StyleTokens.fontSizeBody
                color: StyleTokens.colorTextSecondary
            }

            AppButton {
                text: "复制链接" + (shareDetailsDialog.sharePassword ? "和密码" : "")
                variant: "primary"
                Layout.alignment: Qt.AlignHCenter
                onClicked: {
                    var copyText = shareDetailsDialog.shareLink
                    if (shareDetailsDialog.sharePassword) {
                        copyText += "\n密码：" + shareDetailsDialog.sharePassword
                    }
                    clipboardText.text = copyText
                    clipboardText.selectAll()
                    clipboardText.copy()
                    notificationToast.showSuccess("已复制到剪贴板")
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
            notificationToast.showSuccess(message)
        }

        function onShareOperationFailed(message) {
            notificationToast.showError(message)
        }

        function onShareCreated(shareId, shareLink, password, expiresAt) {
            shareDetailsDialog.shareLink = shareLink
            shareDetailsDialog.sharePassword = password
            shareDetailsDialog.shareExpiresAt = expiresAt
            shareDetailsDialog.open()
        }
    }

    NotificationToast {
        id: notificationToast
    }
}
