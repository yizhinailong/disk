import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string cardObjectName: ""
    objectName: root.cardObjectName

    required property color panelColor
    required property color borderColor
    required property color titleTextColor
    required property color bodyTextColor
    required property color trackColor
    required property color accentColor
    required property string usageText
    required property real usageRatio
    required property real totalBytes

    Layout.fillWidth: true
    Layout.preferredHeight: 104
    color: root.panelColor
    radius: 10
    border.color: root.borderColor

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            Layout.fillWidth: true
            text: "Storage"
            color: root.titleTextColor
            font.pixelSize: 11
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            text: root.usageText
            color: root.bodyTextColor
            font.pixelSize: 14
            font.bold: true
            wrapMode: Text.WordWrap
        }

        ProgressBar {
            Layout.fillWidth: true
            value: root.usageRatio

            background: Rectangle {
                implicitHeight: 8
                radius: 4
                color: root.trackColor
            }

            contentItem: Item {
                implicitHeight: 8

                Rectangle {
                    width: parent.width * root.usageRatio
                    height: parent.height
                    radius: 4
                    color: root.accentColor
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.totalBytes > 0
                  ? Math.round(root.usageRatio * 100) + "% in use"
                  : "Loading account storage"
            color: root.titleTextColor
            font.pixelSize: 11
        }
    }
}
