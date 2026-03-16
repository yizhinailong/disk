/**
 * @file UserPage.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户设置页 - 个人信息、存储空间、密码修改
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0
import "../tokens"
import "../components/primitives"

Item {
    id: root

    // ==================== 保存成功/失败提示 ====================

    property bool profileSaved: false
    property bool passwordSaved: false
    property string lastError: ""

    // ==================== 内容 ====================

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 0

            // ==================== 页面标题 ====================

            Label {
                text: "👤 个人设置"
                font.pixelSize: StyleTokens.fontSizeH1
                font.weight: StyleTokens.fontWeightH1
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
            }

            // ==================== 成功提示横幅 ====================

            Rectangle {
                id: successBanner
                visible: root.profileSaved || root.passwordSaved
                Layout.fillWidth: true
                Layout.topMargin: StyleTokens.spacingMd
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.preferredHeight: 36
                radius: StyleTokens.radiusMedium
                color: "#E8F5E9"
                border.color: StyleTokens.colorSuccess
                border.width: 1

                Timer {
                    running: parent.visible
                    interval: 3000
                    onTriggered: {
                        root.profileSaved = false
                        root.passwordSaved = false
                    }
                }

                Label {
                    anchors.centerIn: parent
                    text: root.profileSaved ? "✓ 个人信息已保存" : "✓ 密码已修改"
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: "#2E7D32"
                }
            }

            // ==================== 错误提示 ====================

            Rectangle {
                visible: root.lastError !== ""
                Layout.fillWidth: true
                Layout.topMargin: StyleTokens.spacingMd
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.preferredHeight: 36
                radius: StyleTokens.radiusMedium
                color: "#FFEBEE"
                border.color: StyleTokens.colorError
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: root.lastError
                    font.pixelSize: StyleTokens.fontSizeBody
                    color: "#C62828"
                }
            }

            // ==================== 个人信息 ====================

            Label {
                text: "个人信息"
                font.pixelSize: StyleTokens.fontSizeH2
                font.weight: StyleTokens.fontWeightH2
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
            }

            Rectangle {
                Layout.topMargin: StyleTokens.spacingSm
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.fillWidth: true
                Layout.preferredHeight: profileCol.implicitHeight + StyleTokens.spacingLg
                color: StyleTokens.colorSurface
                radius: StyleTokens.radiusLarge
                border.color: StyleTokens.colorBorder
                border.width: 1

                ColumnLayout {
                    id: profileCol
                    anchors.fill: parent
                    anchors.margins: StyleTokens.spacingMd
                    spacing: StyleTokens.spacingMd

                    // 用户名（只读显示）
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "用户名"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                        }

                        AppTextInput {
                            id: usernameField
                            Layout.fillWidth: true
                            text: typeof UserViewModel !== 'undefined' ? UserViewModel.username : ""
                            readOnly: true
                            color: StyleTokens.colorTextTertiary
                            background: Rectangle {
                                color: StyleTokens.colorBackground
                                radius: StyleTokens.radiusMedium
                                border.color: "transparent"
                            }
                        }
                    }

                    // 邮箱（只读显示）
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "邮箱"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                        }

                        AppTextInput {
                            id: emailField
                            Layout.fillWidth: true
                            text: typeof UserViewModel !== 'undefined' ? UserViewModel.email : ""
                            readOnly: true
                            color: StyleTokens.colorTextTertiary
                            background: Rectangle {
                                color: StyleTokens.colorBackground
                                radius: StyleTokens.radiusMedium
                                border.color: "transparent"
                            }
                        }
                    }

                    // 昵称（可编辑）
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "昵称"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                        }

                        AppTextInput {
                            id: nicknameField
                            Layout.fillWidth: true
                            text: typeof UserViewModel !== 'undefined' ? UserViewModel.nickname : ""
                            placeholderText: "设置您的昵称"
                            onTextEdited: {
                                if (typeof UserViewModel !== 'undefined') {
                                    UserViewModel.nickname = text
                                }
                            }
                        }
                    }

                    // 保存按钮
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingMd

                        Item { Layout.fillWidth: true }

                        AppButton {
                            text: "保存个人信息"
                            variant: "primary"
                            enabled: typeof UserViewModel !== 'undefined' && UserViewModel.hasProfileChanges
                            onClicked: {
                                if (typeof UserViewModel !== 'undefined') {
                                    UserViewModel.updateProfile()
                                }
                            }
                        }
                    }
                }
            }

            // ==================== 存储空间 ====================

            Label {
                text: "存储空间"
                font.pixelSize: StyleTokens.fontSizeH2
                font.weight: StyleTokens.fontWeightH2
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
            }

            Rectangle {
                Layout.topMargin: StyleTokens.spacingSm
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.fillWidth: true
                Layout.preferredHeight: storageCol.implicitHeight + StyleTokens.spacingLg
                color: StyleTokens.colorSurface
                radius: StyleTokens.radiusLarge
                border.color: StyleTokens.colorBorder
                border.width: 1

                ColumnLayout {
                    id: storageCol
                    anchors.fill: parent
                    anchors.margins: StyleTokens.spacingMd
                    spacing: StyleTokens.spacingSm

                    // 使用量文本
                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            text: {
                                if (typeof UserViewModel !== 'undefined' && UserViewModel.storageLoaded) {
                                    return "已使用 " + UserViewModel.storageUsedFormatted
                                           + " / " + UserViewModel.storageTotalFormatted
                                }
                                return "正在加载..."
                            }
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                        }

                        Item { Layout.fillWidth: true }

                        Label {
                            visible: typeof UserViewModel !== 'undefined' && UserViewModel.storageLoaded
                            text: {
                                if (typeof UserViewModel !== 'undefined') {
                                    var pct = UserViewModel.storagePercentage;
                                    return pct.toFixed(1) + "%";
                                }
                                return ""
                            }
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: {
                                if (typeof UserViewModel === 'undefined') return StyleTokens.colorTextTertiary
                                var pct = UserViewModel.storagePercentage;
                                if (pct >= 100) return StyleTokens.colorError;
                                if (pct >= 80) return StyleTokens.colorWarning;
                                return StyleTokens.colorTextTertiary;
                            }
                        }
                    }

                    // 进度条
                    ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: typeof UserViewModel !== 'undefined' && UserViewModel.storageLoaded
                               ? UserViewModel.storagePercentage : 0

                        background: Rectangle {
                            implicitWidth: 200
                            implicitHeight: 8
                            radius: StyleTokens.radiusSmall
                            color: StyleTokens.colorBackground
                        }

                        contentItem: Item {
                            implicitWidth: 200
                            implicitHeight: 8

                            Rectangle {
                                width: parent.parent.visualPosition * parent.width
                                height: parent.height
                                radius: StyleTokens.radiusSmall
                                color: {
                                    if (typeof UserViewModel === 'undefined') return StyleTokens.colorPrimary
                                    var pct = UserViewModel.storagePercentage;
                                    if (pct >= 100) return StyleTokens.colorError;
                                    if (pct >= 80) return StyleTokens.colorWarning;
                                    return StyleTokens.colorPrimary;
                                }
                            }
                        }
                    }

                    // 警告提示
                    Label {
                        visible: typeof UserViewModel !== 'undefined'
                                 && UserViewModel.storageLoaded
                                 && UserViewModel.storagePercentage >= 80
                                 && UserViewModel.storagePercentage < 100
                        text: "⚠ 存储空间即将用尽，请清理文件"
                        font.pixelSize: StyleTokens.fontSizeSmall
                        color: StyleTokens.colorWarning
                    }

                    Label {
                        visible: typeof UserViewModel !== 'undefined'
                                 && UserViewModel.storageLoaded
                                 && UserViewModel.storagePercentage >= 100
                        text: "❌ 存储空间已满，无法上传新文件"
                        font.pixelSize: StyleTokens.fontSizeSmall
                        color: StyleTokens.colorError
                    }

                    // 刷新按钮
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingSm

                        Item { Layout.fillWidth: true }

                        AppButton {
                            text: "刷新"
                            variant: "secondary"
                            enabled: typeof UserViewModel !== 'undefined' && !UserViewModel.loading
                            onClicked: {
                                if (typeof UserViewModel !== 'undefined') {
                                    UserViewModel.loadStorage()
                                }
                            }
                        }
                    }
                }
            }

            // ==================== 修改密码 ====================

            Label {
                text: "修改密码"
                font.pixelSize: StyleTokens.fontSizeH2
                font.weight: StyleTokens.fontWeightH2
                color: StyleTokens.colorTextPrimary
                Layout.topMargin: StyleTokens.spacingLg
                Layout.leftMargin: StyleTokens.spacingLg
            }

            Rectangle {
                Layout.topMargin: StyleTokens.spacingSm
                Layout.leftMargin: StyleTokens.spacingLg
                Layout.rightMargin: StyleTokens.spacingLg
                Layout.fillWidth: true
                Layout.preferredHeight: passwordCol.implicitHeight + StyleTokens.spacingLg
                color: StyleTokens.colorSurface
                radius: StyleTokens.radiusLarge
                border.color: StyleTokens.colorBorder
                border.width: 1

                ColumnLayout {
                    id: passwordCol
                    anchors.fill: parent
                    anchors.margins: StyleTokens.spacingMd
                    spacing: StyleTokens.spacingMd

                    // 当前密码
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "当前密码"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                        }

                        AppTextInput {
                            id: currentPasswordField
                            Layout.fillWidth: true
                            placeholderText: "请输入当前密码"
                            echoMode: TextInput.Password
                        }
                    }

                    // 新密码
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "新密码"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                        }

                        AppTextInput {
                            id: newPasswordField
                            Layout.fillWidth: true
                            placeholderText: "8-64位，需包含大小写字母和数字"
                            echoMode: TextInput.Password
                        }
                    }

                    // 确认新密码
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingXs

                        Label {
                            text: "确认新密码"
                            font.pixelSize: StyleTokens.fontSizeBody
                            color: StyleTokens.colorTextSecondary
                        }

                        AppTextInput {
                            id: confirmPasswordField
                            Layout.fillWidth: true
                            placeholderText: "请再次输入新密码"
                            echoMode: TextInput.Password
                        }
                    }

                    // 密码强度提示
                    Label {
                        visible: newPasswordField.text.length > 0
                        text: {
                            var pwd = newPasswordField.text
                            if (pwd.length < 8) return "⚠ 密码长度至少8位"
                            if (!/[a-z]/.test(pwd)) return "⚠ 需包含小写字母"
                            if (!/[A-Z]/.test(pwd)) return "⚠ 需包含大写字母"
                            if (!/\d/.test(pwd)) return "⚠ 需包含数字"
                            if (pwd.length > 64) return "⚠ 密码长度不能超过64位"
                            return "✓ 密码格式正确"
                        }
                        font.pixelSize: StyleTokens.fontSizeSmall
                        color: {
                            var pwd = newPasswordField.text
                            if (pwd.length < 8 || !/[a-z]/.test(pwd) || !/[A-Z]/.test(pwd) || !/\d/.test(pwd) || pwd.length > 64) {
                                return StyleTokens.colorWarning
                            }
                            return StyleTokens.colorSuccess
                        }
                    }

                    // 密码匹配提示
                    Label {
                        visible: confirmPasswordField.text.length > 0
                        text: confirmPasswordField.text === newPasswordField.text ? "✓ 两次密码一致" : "⚠ 两次密码不一致"
                        font.pixelSize: StyleTokens.fontSizeSmall
                        color: confirmPasswordField.text === newPasswordField.text ? StyleTokens.colorSuccess : StyleTokens.colorWarning
                    }

                    // 修改密码按钮
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: StyleTokens.spacingMd

                        Item { Layout.fillWidth: true }

                        AppButton {
                            text: "修改密码"
                            variant: "primary"
                            enabled: {
                                if (typeof UserViewModel !== 'undefined' && UserViewModel.loading) return false
                                if (currentPasswordField.text.length === 0) return false
                                if (newPasswordField.text.length < 8) return false
                                if (confirmPasswordField.text !== newPasswordField.text) return false
                                return true
                            }
                            onClicked: {
                                // 客户端验证
                                if (newPasswordField.text !== confirmPasswordField.text) {
                                    root.lastError = "两次输入的密码不一致"
                                    return
                                }

                                var pwd = newPasswordField.text
                                if (!/^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)[a-zA-Z\d]{8,64}$/.test(pwd)) {
                                    root.lastError = "密码格式不正确：8-64位，需包含大小写字母和数字"
                                    return
                                }

                                root.lastError = ""

                                if (typeof UserViewModel !== 'undefined') {
                                    UserViewModel.changePassword(
                                        currentPasswordField.text,
                                        newPasswordField.text
                                    )
                                }
                            }
                        }
                    }
                }
            }

            // 底部留白
            Item { Layout.preferredHeight: StyleTokens.spacingLg }
        }
    }

    // ==================== UserViewModel 信号连接 ====================

    Connections {
        target: typeof UserViewModel !== 'undefined' ? UserViewModel : null
        enabled: typeof UserViewModel !== 'undefined'
        ignoreUnknownSignals: true

        function onProfileUpdated() {
            root.profileSaved = true
            root.lastError = ""
        }

        function onPasswordChanged() {
            root.passwordSaved = true
            root.lastError = ""
            // 清空密码字段
            currentPasswordField.text = ""
            newPasswordField.text = ""
            confirmPasswordField.text = ""
        }

        function onErrorMessageChanged() {
            if (typeof UserViewModel !== 'undefined' && UserViewModel.errorMessage !== "") {
                root.lastError = UserViewModel.errorMessage
            }
        }
    }

    // ==================== 页面加载时获取数据 ====================

    Component.onCompleted: {
        if (typeof UserViewModel !== 'undefined') {
            UserViewModel.loadProfile()
            UserViewModel.loadStorage()
        }
    }
}
