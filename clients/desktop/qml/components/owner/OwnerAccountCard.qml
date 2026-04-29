import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property color backgroundColor
    required property color borderColor
    required property color primaryTextColor
    required property color secondaryTextColor
    required property string primaryText
    required property string secondaryText

    Layout.alignment: Qt.AlignVCenter
    Layout.preferredWidth: 188
    Layout.preferredHeight: 56
    color: root.backgroundColor
    radius: 10
    border.color: root.borderColor

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 2

        Label {
            text: root.primaryText
            color: root.primaryTextColor
            font.pixelSize: 14
            font.bold: true
        }

        Label {
            text: root.secondaryText
            color: root.secondaryTextColor
            font.pixelSize: 11
        }
    }
}
