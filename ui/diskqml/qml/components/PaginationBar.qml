/**
 * @file PaginationBar.qml
 * @brief 分页栏组件
 * @author LiuFeng (liufeng.code@outlook.com)
 * @copyright Copyright (c) 2026
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../tokens"

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
        anchors.leftMargin: StyleTokens.spacingMd
        anchors.rightMargin: StyleTokens.spacingMd
        spacing: StyleTokens.spacingSm

        Item { Layout.fillWidth: true }

        Label {
            text: "第 " + root.currentPage + " / " + root.totalPages + " 页"
            font.pixelSize: StyleTokens.fontSizeSmall
            font.weight: StyleTokens.fontWeightSmall
            color: StyleTokens.colorTextSecondary
        }

        Button {
            text: "上一页"
            font.pixelSize: StyleTokens.fontSizeSmall
            font.weight: StyleTokens.fontWeightSmall
            enabled: root.currentPage > 1 && !root.loading
            onClicked: root.pageRequested(root.currentPage - 1)
        }

        Button {
            text: "下一页"
            font.pixelSize: StyleTokens.fontSizeSmall
            font.weight: StyleTokens.fontWeightSmall
            enabled: root.currentPage < root.totalPages && !root.loading
            onClicked: root.pageRequested(root.currentPage + 1)
        }

        Label {
            text: "共 " + root.totalItems + " 项"
            font.pixelSize: StyleTokens.fontSizeSmall
            font.weight: StyleTokens.fontWeightSmall
            color: StyleTokens.colorTextSecondary
        }

        Item { Layout.fillWidth: true }
    }
}
