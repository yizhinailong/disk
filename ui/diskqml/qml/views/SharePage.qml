/**
 * @file SharePage.qml
 * @brief 分享页 - 分享列表、创建/取消分享
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
            text: "分享"
            font.pixelSize: 20
            font.bold: true
            color: palette.windowText
        }

        Label {
            text: "分享功能开发中..."
            font.pixelSize: 14
            color: palette.placeholderText
        }

        Item { Layout.fillHeight: true }
    }
}
