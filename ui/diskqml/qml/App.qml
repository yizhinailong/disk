/**
 * @file App.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 应用主窗口与页面栈（无边框 + 自定义标题栏）
 * @version 0.2
 * @date 2026-03-04
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Disk 1.0
import "views"

ApplicationWindow {
    id: root
    visible: true
    width: 960
    height: 600
    minimumWidth: 900
    title: qsTr("Disk - 云盘客户端")
    flags: Qt.Window | Qt.FramelessWindowHint
    color: palette.window

    property string prefillAccount: ""

    // Center window on first show
    Component.onCompleted: {
        root.x = (Screen.width - root.width) / 2
        root.y = (Screen.height - root.height) / 2
    }

    // ==================== 自定义标题栏 ====================
    Rectangle {
        id: titleBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 40
        color: palette.window

        // 底部分隔线
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: palette.mid
        }

        // 拖拽区域：整个标题栏
        DragHandler {
            target: null
            onActiveChanged: {
                if (active) {
                    root.startSystemMove()
                }
            }
        }

        // 双击最大化/还原
        TapHandler {
            onDoubleTapped: {
                if (root.visibility === Window.Maximized) {
                    root.showNormal()
                } else {
                    root.showMaximized()
                }
            }
        }

        // 左侧：Logo
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: "Disk"
            font.pixelSize: 14
            font.bold: true
            color: palette.windowText
        }

        // 右侧：窗口控制按钮 (Windows/Linux 风格)
        Row {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            // 最小化按钮
            Rectangle {
                width: 46
                height: parent.height
                color: minimizeArea.containsMouse ? palette.midlight : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "\u2013"  // en-dash as minimize icon
                    font.pixelSize: 14
                    color: palette.windowText
                }

                MouseArea {
                    id: minimizeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.showMinimized()
                }
            }

            // 最大化/还原按钮
            Rectangle {
                width: 46
                height: parent.height
                color: maximizeArea.containsMouse ? palette.midlight : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: root.visibility === Window.Maximized ? "\u2752" : "\u25A1"  // restore / maximize
                    font.pixelSize: 14
                    color: palette.windowText
                }

                MouseArea {
                    id: maximizeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (root.visibility === Window.Maximized) {
                            root.showNormal()
                        } else {
                            root.showMaximized()
                        }
                    }
                }
            }

            // 关闭按钮
            Rectangle {
                width: 46
                height: parent.height
                color: closeArea.containsMouse ? "#E81123" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "\u2715"  // × close icon
                    font.pixelSize: 14
                    color: closeArea.containsMouse ? "white" : palette.windowText
                }

                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.close()
                }
            }
        }
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
