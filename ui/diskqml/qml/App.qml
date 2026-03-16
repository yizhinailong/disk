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
import "components/shell"

ApplicationWindow {
    id: root
    visible: true
    width: 960
    height: 600
    minimumWidth: 600
    title: qsTr("Disk - 云盘客户端")
    flags: Qt.Window | Qt.FramelessWindowHint
    color: palette.window

    property string prefillAccount: ""

    // 首次显示时居中窗口
    Component.onCompleted: {
        root.x = (Screen.width - root.width) / 2
        root.y = (Screen.height - root.height) / 2
    }

    // ==================== 自定义标题栏 ====================
    AppTitleBar {
        id: titleBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        onSystemMoveRequested: root.startSystemMove()
    }

    // ==================== 窗口缩放手柄 ====================
    // 缩放边距
    readonly property int resizeMargin: 5

    // 上边
    MouseArea {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.resizeMargin
        anchors.rightMargin: root.resizeMargin
        height: root.resizeMargin
        cursorShape: Qt.SizeVerCursor
        hoverEnabled: true
        onPressed: root.startSystemResize(Qt.TopEdge)
    }

    // 下边
    MouseArea {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.resizeMargin
        anchors.rightMargin: root.resizeMargin
        height: root.resizeMargin
        cursorShape: Qt.SizeVerCursor
        hoverEnabled: true
        onPressed: root.startSystemResize(Qt.BottomEdge)
    }

    // 左边
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.resizeMargin
        anchors.bottomMargin: root.resizeMargin
        width: root.resizeMargin
        cursorShape: Qt.SizeHorCursor
        hoverEnabled: true
        onPressed: root.startSystemResize(Qt.LeftEdge)
    }

    // 右边
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: root.resizeMargin
        anchors.bottomMargin: root.resizeMargin
        width: root.resizeMargin
        cursorShape: Qt.SizeHorCursor
        hoverEnabled: true
        onPressed: root.startSystemResize(Qt.RightEdge)
    }

    // 左上角
    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        width: root.resizeMargin
        height: root.resizeMargin
        cursorShape: Qt.SizeFDiagCursor
        hoverEnabled: true
        onPressed: root.startSystemResize(Qt.LeftEdge | Qt.TopEdge)
    }

    // 右上角
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        width: root.resizeMargin
        height: root.resizeMargin
        cursorShape: Qt.SizeBDiagCursor
        hoverEnabled: true
        onPressed: root.startSystemResize(Qt.RightEdge | Qt.TopEdge)
    }

    // 左下角
    MouseArea {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: root.resizeMargin
        height: root.resizeMargin
        cursorShape: Qt.SizeBDiagCursor
        hoverEnabled: true
        onPressed: root.startSystemResize(Qt.LeftEdge | Qt.BottomEdge)
    }

    // 右下角
    MouseArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: root.resizeMargin
        height: root.resizeMargin
        cursorShape: Qt.SizeFDiagCursor
        hoverEnabled: true
        onPressed: root.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
    }

    // ==================== 页面栈 ====================
    StackView {
        id: pageStack
        objectName: "pageStack"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
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
