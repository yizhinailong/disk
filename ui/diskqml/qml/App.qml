/**
 * @file App.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 应用主窗口与页面栈
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Disk 1.0
import "views"

ApplicationWindow {
    id: root
    visible: true
    width: 960
    height: 600
    title: qsTr("Disk - 云盘客户端")

    Material.theme: Material.Light
    Material.accent: "#2196F3"

    property string prefillAccount: ""

    // ==================== 页面栈 ====================
    StackView {
        id: pageStack
        objectName: "pageStack"
        anchors.fill: parent
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
        HomePlaceholder {
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
