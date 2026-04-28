import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "authCardRoot"

    AuthTheme {
        id: defaultTheme
    }

    property QtObject theme: defaultTheme
    property string eyebrowText: ""
    property string titleText: ""
    property string subtitleText: ""
    property int contentSpacing: theme.fieldSpacing
    readonly property int cardShadowInsetX: theme.cardShadowSpread
    readonly property real cardShadowInsetY: theme.cardShadowSpread + (theme.cardShadowOffsetY / 2)

    default property alias contentData: bodyColumn.data

    implicitWidth: theme.cardWidth + (cardShadowInsetX * 2)
    implicitHeight: cardSurface.implicitHeight + (cardShadowInsetY * 2)

    Layout.preferredWidth: implicitWidth

    Rectangle {
        id: shadowSurface
        objectName: "authCardShadowSurface"

        anchors.fill: parent

        radius: theme.cardRadius + theme.cardShadowSpread
        color: theme.cardShadowColor
        z: 0
    }

    Rectangle {
        id: cardSurface
        objectName: "authCardSurface"

        anchors.centerIn: parent
        width: theme.cardWidth
        height: implicitHeight

        implicitHeight: contentLayout.implicitHeight + (theme.contentVerticalPadding * 2)

        radius: theme.cardRadius
        color: theme.cardBackgroundColor
        border.color: theme.cardBorderColor
        clip: true
        z: 1

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: theme.cardAccentHeight

            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: theme.cardAccentStartColor
                }
                GradientStop {
                    position: 1.0
                    color: theme.cardAccentEndColor
                }
            }
        }

        ColumnLayout {
            id: contentLayout

            anchors.fill: parent
            anchors.leftMargin: theme.contentHorizontalPadding
            anchors.rightMargin: theme.contentHorizontalPadding
            anchors.topMargin: theme.contentVerticalPadding
            anchors.bottomMargin: theme.contentVerticalPadding
            spacing: theme.sectionSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.headerSpacing
                visible: root.eyebrowText !== "" || root.titleText !== "" || root.subtitleText !== ""

                Label {
                    Layout.fillWidth: true
                    visible: root.eyebrowText !== ""
                    text: root.eyebrowText
                    color: theme.secondaryCtaTextColor
                    font.pixelSize: 12
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.titleText !== ""
                    text: root.titleText
                    color: theme.titleTextColor
                    font.pixelSize: 30
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.subtitleText !== ""
                    text: root.subtitleText
                    color: theme.bodyTextColor
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }
            }

            ColumnLayout {
                id: bodyColumn

                Layout.fillWidth: true
                spacing: root.contentSpacing
            }
        }
    }
}
