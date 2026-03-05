/**
 * @file TrashPage.qml
 * @brief 回收站页 - 已删除文件列表、恢复、彻底删除
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
            text: "回收站"
            font.pixelSize: 20
            font.bold: true
            color: palette.windowText
        }

        Label {
            text: "回收站功能开发中..."
            font.pixelSize: 14
            color: palette.placeholderText
        }

        Item { Layout.fillHeight: true }
    }
}
