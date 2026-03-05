/**
 * @file DownloadPage.qml
 * @brief 下载页 - 下载队列、进度、暂停/恢复/取消
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
            text: "下载"
            font.pixelSize: 20
            font.bold: true
            color: palette.windowText
        }

        Label {
            text: "下载功能开发中..."
            font.pixelSize: 14
            color: palette.placeholderText
        }

        Item { Layout.fillHeight: true }
    }
}
