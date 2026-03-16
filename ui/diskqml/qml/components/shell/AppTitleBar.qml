import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "../tokens"
import "../primitives"

Rectangle {
    id: root
    height: StyleTokens.titleBarHeight
    color: StyleTokens.colorSurface

    property Window window: Window.window

    signal systemMoveRequested()

    // 底部分隔线
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: StyleTokens.colorBorder
    }

    // 拖拽区域：整个标题栏
    DragHandler {
        target: null
        onActiveChanged: {
            if (active) {
                root.systemMoveRequested()
            }
        }
    }

    // 双击最大化/还原
    TapHandler {
        onDoubleTapped: {
            if (root.window) {
                if (root.window.visibility === Window.Maximized) {
                    root.window.showNormal()
                } else {
                    root.window.showMaximized()
                }
            }
        }
    }

    // 左侧：标志
    Row {
        anchors.left: parent.left
        anchors.leftMargin: StyleTokens.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        spacing: StyleTokens.spacingSm

        Text {
            text: "Disk"
            font.pixelSize: StyleTokens.fontSizeH2
            font.weight: StyleTokens.fontWeightH2
            color: StyleTokens.colorTextPrimary
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // 中间：搜索框
    AppTextInput {
        anchors.centerIn: parent
        implicitWidth: 300
        implicitHeight: 32
        placeholderText: "🔍 搜索文件、文件夹...            ⌘ K"
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
            color: minimizeArea.containsMouse ? StyleTokens.colorHover : "transparent"

            Text {
                anchors.centerIn: parent
                text: "\u2013"  // 短划线作为最小化图标
                font.pixelSize: 14
                color: StyleTokens.colorTextPrimary
            }

            MouseArea {
                id: minimizeArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    if (root.window) root.window.showMinimized()
                }
            }
        }

        // 最大化/还原按钮
        Rectangle {
            width: 46
            height: parent.height
            color: maximizeArea.containsMouse ? StyleTokens.colorHover : "transparent"

            Text {
                anchors.centerIn: parent
                text: root.window && root.window.visibility === Window.Maximized ? "\u2752" : "\u25A1"  // 还原 / 最大化
                font.pixelSize: 14
                color: StyleTokens.colorTextPrimary
            }

            MouseArea {
                id: maximizeArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    if (root.window) {
                        if (root.window.visibility === Window.Maximized) {
                            root.window.showNormal()
                        } else {
                            root.window.showMaximized()
                        }
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
                text: "\u2715"  // × 关闭图标
                font.pixelSize: 14
                color: closeArea.containsMouse ? "white" : StyleTokens.colorTextPrimary
            }

            MouseArea {
                id: closeArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    if (root.window) root.window.close()
                }
            }
        }
    }
}
