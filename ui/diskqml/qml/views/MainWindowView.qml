import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    // File Mode vs Transfer Mode
    property bool isTransferMode: false
    // Current navigation selected
    property string currentNav: isTransferMode ? "首页" : "首页"

    // Optional: propagate logout to App.qml if needed
    signal logoutRequested

    // Keep track of logged out state
    Connections {
        target: SessionViewModel
        function onIsLoggedInChanged() {
            if (!SessionViewModel.isLoggedIn) {
                root.logoutRequested()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== 侧边栏 ====================
        Rectangle {
            width: 200
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: "#F5F5F5" // 简单的占位背景色
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                // 动态生成导航项
                Repeater {
                    model: root.isTransferMode ? ["首页", "上传", "下载", "分享"] : ["首页", "我的文件", "回收站"]
                    delegate: Button {
                        text: modelData
                        Layout.fillWidth: true
                        highlighted: root.currentNav === modelData
                        onClicked: root.currentNav = modelData
                    }
                }

                Item { Layout.fillHeight: true } // 占位填充，把底部按钮推到最下

                // 底部模式切换按钮
                Button {
                    text: root.isTransferMode ? "文件" : "传输"
                    Layout.fillWidth: true
                    onClicked: {
                        root.isTransferMode = !root.isTransferMode
                        root.currentNav = "首页" // 切换模式时重置为首页
                    }
                }
            }
        }

        // ==================== 右侧主区域 ====================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ==================== 工具栏 ====================
            Rectangle {
                Layout.fillWidth: true
                height: 48
                Layout.preferredHeight: 48
                color: "#FFFFFF"
                
                // 底部边框线
                Rectangle {
                    width: parent.width
                    height: 1
                    color: "#E0E0E0"
                    anchors.bottom: parent.bottom
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 12

                    // 左侧：主要操作
                    ToolButton { text: "上传" }
                    ToolButton { text: "新建" }

                    // 垂直分割线
                    Rectangle {
                        width: 1
                        height: 24
                        color: "#E0E0E0"
                    }

                    // 中左：导航控制
                    ToolButton { text: "返回" }
                    ToolButton { text: "前进" }
                    ToolButton { text: "刷新" }

                    // 中心弹簧
                    Item { Layout.fillWidth: true }

                    // 中右：搜索框
                    TextField {
                        placeholderText: "搜索..."
                        width: 200
            Layout.preferredWidth: 200
                    }

                    // 垂直分割线
                    Rectangle {
                        width: 1
                        height: 24
                        color: "#E0E0E0"
                    }

                    // 右侧：更多、设置、退出登录
                    ToolButton { text: "更多" }
                    ToolButton { text: "设置" }
                    ToolButton { 
                        text: "退出登录"
                        onClicked: SessionViewModel.logout()
                    }
                }
            }

            // ==================== 主内容区 ====================
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#FFFFFF"

                Label {
                    anchors.centerIn: parent
                    text: root.currentNav + " 占位页面"
                    font.pixelSize: 24
                    color: "#9E9E9E"
                }
            }

            // ==================== 状态栏 ====================
            Rectangle {
                Layout.fillWidth: true
                height: 32
                Layout.preferredHeight: 32
                color: "#F5F5F5"
                
                // 顶部边框线
                Rectangle {
                    width: parent.width
                    height: 1
                    color: "#E0E0E0"
                    anchors.top: parent.top
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16

                    // 左侧：项目数量
                    Label {
                        text: "15 个项目"
                        color: "#616161"
                        font.pixelSize: 12
                    }

                    // 占位
                    Item { Layout.fillWidth: true }

                    // 右侧：容量使用情况
                    Label {
                        text: "已用 2.5 GB / 10 GB"
                        color: "#616161"
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
