/**
 * @file FilesPage.qml
 * @brief 我的文件页 - 文件列表(网格/列表)、面包屑、搜索
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
            text: "我的文件"
            font.pixelSize: 20
            font.bold: true
            color: palette.windowText
        }

        Label {
            text: "文件浏览功能开发中..."
            font.pixelSize: 14
            color: palette.placeholderText
        }

        Item { Layout.fillHeight: true }
    }
}
