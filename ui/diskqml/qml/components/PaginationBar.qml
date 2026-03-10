import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property int currentPage: 1
    property int totalPages: 1
    property int totalItems: 0
    property bool loading: false

    signal pageRequested(int page)

    Layout.fillWidth: true
    Layout.preferredHeight: 36
    color: "transparent"
    visible: totalPages > 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 8

        Item { Layout.fillWidth: true }

        Label {
            text: "第 " + root.currentPage + " / " + root.totalPages + " 页"
            font.pixelSize: 12
            color: palette.placeholderText
        }

        Button {
            text: "上一页"
            font.pixelSize: 12
            enabled: root.currentPage > 1 && !root.loading
            onClicked: root.pageRequested(root.currentPage - 1)
        }

        Button {
            text: "下一页"
            font.pixelSize: 12
            enabled: root.currentPage < root.totalPages && !root.loading
            onClicked: root.pageRequested(root.currentPage + 1)
        }

        Label {
            text: "共 " + root.totalItems + " 项"
            font.pixelSize: 12
            color: palette.placeholderText
        }

        Item { Layout.fillWidth: true }
    }
}
