/**
 * @file HomePage.qml
 * @brief 首页 - 最近文件、快捷操作、存储概览
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
            text: "首页"
            font.pixelSize: 20
            font.bold: true
            color: palette.windowText
        }

        Label {
            text: "欢迎使用 Disk 云盘"
            font.pixelSize: 14
            color: palette.placeholderText
        }

        Item { Layout.fillHeight: true }
    }
}
