/**
 * @file HomePlaceholder.qml
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 已登录主页占位页面
 * @version 0.1
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Disk 1.0

Item {
    id: root

    signal logoutRequested

    // ==================== 页面布局 ====================
    Rectangle {
        anchors.fill: parent
        color: "#FAFAFA"

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 24

            Label {
                text: qsTr("欢迎, %1").arg(SessionViewModel.loggedInUserName)
                font.pixelSize: 24
                font.bold: true
                color: "#212121"
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: qsTr("您已成功登录")
                font.pixelSize: 16
                color: "#757575"
                Layout.alignment: Qt.AlignHCenter
            }

            Button {
                id: logoutButton
                objectName: "logoutButton"
                text: qsTr("退出登录")
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 32
                
                Material.background: "#F44336"
                Material.foreground: "#FFFFFF"
                
                onClicked: {
                    SessionViewModel.logout()
                }
            }
        }
    }

    // ==================== 登录状态监听 ====================
    Connections {
        target: SessionViewModel
        function onIsLoggedInChanged() {
            if (!SessionViewModel.isLoggedIn) {
                root.logoutRequested()
            }
        }
    }
}
