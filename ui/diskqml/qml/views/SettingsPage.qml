/**
 * @file SettingsPage.qml
 * @brief 设置页 - 服务器地址、下载目录、并发数、UI偏好
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Label {
            text: "设置"
            font.pixelSize: 20
            font.bold: true
            color: palette.windowText
        }

        Label {
            text: "设置功能开发中..."
            font.pixelSize: 14
            color: palette.placeholderText
        }

        Item { Layout.fillHeight: true }
    }
}
