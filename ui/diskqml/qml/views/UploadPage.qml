/**
 * @file UploadPage.qml
 * @brief 上传页 - 上传队列、进度、暂停/恢复/取消
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
            text: "上传"
            font.pixelSize: 20
            font.bold: true
            color: palette.windowText
        }

        Label {
            text: "上传功能开发中..."
            font.pixelSize: 14
            color: palette.placeholderText
        }

        Item { Layout.fillHeight: true }
    }
}
