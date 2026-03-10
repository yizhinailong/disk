/**
 * @file UserPage.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 用户设置页 - 个人信息、存储空间、密码修改
 * @version 0.1
 * @date 2026-03-10
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
                font.pixelSize: 22
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
                Layout.rightMargin: 24
            }

            // ==================== 成功提示横幅 ====================

            Rectangle {
                id: successBanner
                visible: root.profileSaved || root.passwordSaved
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 36
                radius: 6
                color: "#E8F5E9"
                border.color: "#4CAF50"
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
                    font.pixelSize: 13
                    color: "#2E7D32"
                }
            }

            // ==================== 错误提示 ====================

            Rectangle {
                visible: root.lastError !== ""
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 36
                radius: 6
                color: "#FFEBEE"
                border.color: "#EF5350"
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: root.lastError
                    font.pixelSize: 13
                    color: "#C62828"
                }
            }

            // ==================== 个人信息 ====================

            Label {
                text: "个人信息"
                font.pixelSize: 16
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
            }

            Rectangle {
                Layout.topMargin: 8
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.preferredHeight: profileCol.implicitHeight + 24
                color: palette.base
                radius: 8
                border.color: palette.mid
                border.width: 1

                ColumnLayout {
                    id: profileCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    // 用户名（只读显示）
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "用户名"
                            font.pixelSize: 13
                            color: palette.windowText
                        }

                        TextField {
                            id: usernameField
                            Layout.fillWidth: true
                            text: typeof UserViewModel !== 'undefined' ? UserViewModel.username : ""
                            font.pixelSize: 13
                            readOnly: true
                            color: palette.placeholderText
                            background: Rectangle {
                                color: palette.midlight
                                radius: 4
                            }
                        }
                    }

                    // 邮箱（只读显示）
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "邮箱"
                            font.pixelSize: 13
                            color: palette.windowText
                        }

                        TextField {
                            id: emailField
                            Layout.fillWidth: true
                            text: typeof UserViewModel !== 'undefined' ? UserViewModel.email : ""
                            font.pixelSize: 13
                            readOnly: true
                            color: palette.placeholderText
                            background: Rectangle {
                                color: palette.midlight
                                radius: 4
                            }
                        }
                    }

                    // 昵称（可编辑）
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "昵称"
                            font.pixelSize: 13
                            color: palette.windowText
                        }

                        TextField {
                            id: nicknameField
                            Layout.fillWidth: true
                            text: typeof UserViewModel !== 'undefined' ? UserViewModel.nickname : ""
                            placeholderText: "设置您的昵称"
                            font.pixelSize: 13
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
                        spacing: 12

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "保存个人信息"
                            font.pixelSize: 13
                            highlighted: true
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
                font.pixelSize: 16
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
            }

            Rectangle {
                Layout.topMargin: 8
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.preferredHeight: storageCol.implicitHeight + 24
                color: palette.base
                radius: 8
                border.color: palette.mid
                border.width: 1

                ColumnLayout {
                    id: storageCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

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
                            font.pixelSize: 13
                            color: palette.windowText
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
                            font.pixelSize: 13
                            color: {
                                if (typeof UserViewModel === 'undefined') return palette.placeholderText
                                var pct = UserViewModel.storagePercentage;
                                if (pct >= 100) return "#D32F2F";
                                if (pct >= 80) return "#F57C00";
                                return palette.placeholderText;
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
                            radius: 4
                            color: palette.midlight
                        }

                        contentItem: Item {
                            implicitWidth: 200
                            implicitHeight: 8

                            Rectangle {
                                width: parent.parent.visualPosition * parent.width
                                height: parent.height
                                radius: 4
                                color: {
                                    if (typeof UserViewModel === 'undefined') return "#1976D2"
                                    var pct = UserViewModel.storagePercentage;
                                    if (pct >= 100) return "#D32F2F";
                                    if (pct >= 80) return "#F57C00";
                                    return "#1976D2";
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
                        font.pixelSize: 12
                        color: "#F57C00"
                    }

                    Label {
                        visible: typeof UserViewModel !== 'undefined'
                                 && UserViewModel.storageLoaded
                                 && UserViewModel.storagePercentage >= 100
                        text: "❌ 存储空间已满，无法上传新文件"
                        font.pixelSize: 12
                        color: "#D32F2F"
                    }

                    // 刷新按钮
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "刷新"
                            font.pixelSize: 12
                            flat: true
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
                font.pixelSize: 16
                font.bold: true
                color: palette.windowText
                Layout.topMargin: 24
                Layout.leftMargin: 24
            }

            Rectangle {
                Layout.topMargin: 8
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.fillWidth: true
                Layout.preferredHeight: passwordCol.implicitHeight + 24
                color: palette.base
                radius: 8
                border.color: palette.mid
                border.width: 1

                ColumnLayout {
                    id: passwordCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    // 当前密码
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "当前密码"
                            font.pixelSize: 13
                            color: palette.windowText
                        }

                        TextField {
                            id: currentPasswordField
                            Layout.fillWidth: true
                            placeholderText: "请输入当前密码"
                            font.pixelSize: 13
                            echoMode: TextInput.Password
                        }
                    }

                    // 新密码
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "新密码"
                            font.pixelSize: 13
                            color: palette.windowText
                        }

                        TextField {
                            id: newPasswordField
                            Layout.fillWidth: true
                            placeholderText: "8-64位，需包含大小写字母和数字"
                            font.pixelSize: 13
                            echoMode: TextInput.Password
                        }
                    }

                    // 确认新密码
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label {
                            text: "确认新密码"
                            font.pixelSize: 13
                            color: palette.windowText
                        }

                        TextField {
                            id: confirmPasswordField
                            Layout.fillWidth: true
                            placeholderText: "请再次输入新密码"
                            font.pixelSize: 13
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
                        font.pixelSize: 12
                        color: {
                            var pwd = newPasswordField.text
                            if (pwd.length < 8 || !/[a-z]/.test(pwd) || !/[A-Z]/.test(pwd) || !/\d/.test(pwd) || pwd.length > 64) {
                                return "#F57C00"
                            }
                            return "#4CAF50"
                        }
                    }

                    // 密码匹配提示
                    Label {
                        visible: confirmPasswordField.text.length > 0
                        text: confirmPasswordField.text === newPasswordField.text ? "✓ 两次密码一致" : "⚠ 两次密码不一致"
                        font.pixelSize: 12
                        color: confirmPasswordField.text === newPasswordField.text ? "#4CAF50" : "#F57C00"
                    }

                    // 修改密码按钮
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "修改密码"
                            font.pixelSize: 13
                            highlighted: true
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
            Item { Layout.preferredHeight: 24 }
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
            // Clear password fields
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
