/**
 * @file App.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 应用主窗口与页面栈（无边框 + 自定义标题栏）
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Disk 1.0
import "views"
import "tokens"

ApplicationWindow {
    id: root
    visible: true
    width: 1024
    height: 768
    minimumWidth: 800
    minimumHeight: 600
    title: qsTr("Disk - 云盘客户端")
    flags: Qt.Window
    color: StyleTokens.colorBackground

    property string prefillAccount: ""

    // 首次显示时居中窗口
    Component.onCompleted: {
        root.x = (Screen.width - root.width) / 2
        root.y = (Screen.height - root.height) / 2
    }

    // ==================== 页面栈 ====================
    StackView {
        id: pageStack
        objectName: "pageStack"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        initialItem: SessionViewModel.isLoggedIn ? homeView : loginView
    }

    // ==================== 页面组件 ====================
    Component {
        id: loginView
        LoginView {
            prefillAccount: root.prefillAccount
            onRegisterRequested: pageStack.push(registerView)
        }
    }

    Component {
        id: registerView
        RegisterView {
            onRegistered: function (username, email) {
                root.prefillAccount = username
                pageStack.pop()
            }
            onBackRequested: pageStack.pop()
        }
    }

    Component {
        id: homeView
        MainWindowView {
            onLogoutRequested: {
                pageStack.clear()
                pageStack.push(loginView)
            }
        }
    }

    // ==================== 登录状态同步 ====================
    Connections {
        target: SessionViewModel
        function onIsLoggedInChanged() {
            if (SessionViewModel.isLoggedIn) {
                pageStack.clear()
                pageStack.push(homeView)
            } else {
                pageStack.clear()
                pageStack.push(loginView)
            }
        }
    }
}
