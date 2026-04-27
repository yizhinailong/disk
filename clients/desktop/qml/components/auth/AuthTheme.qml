import QtQuick

QtObject {
    id: root

    readonly property color pageBackgroundColor: "#f6f4ef"
    readonly property color heroGradientStartColor: "#dfe9ff"
    readonly property color heroGradientEndColor: "#f1ebff"
    readonly property color heroAccentColor: "#c6d9ff"
    readonly property color heroTextColor: "#153153"
    readonly property color heroMutedTextColor: "#5d7087"

    readonly property color cardBackgroundColor: "#ffffff"
    readonly property color cardBorderColor: "#dce4f0"
    readonly property color cardShadowColor: Qt.rgba(0.09, 0.20, 0.35, 0.10)
    readonly property color cardAccentStartColor: "#7fb8ff"
    readonly property color cardAccentEndColor: "#99a8ff"

    readonly property color titleTextColor: "#13233a"
    readonly property color bodyTextColor: "#5f6f82"
    readonly property color fieldBackgroundColor: "#f7f9fc"
    readonly property color fieldBorderColor: "#d4dde9"
    readonly property color fieldFocusBorderColor: "#3687ff"
    readonly property color fieldTextColor: "#10213a"
    readonly property color fieldPlaceholderColor: "#7b8aa0"
    readonly property color errorTextColor: "#cf3f5c"
    readonly property color successTextColor: "#248456"

    readonly property color primaryCtaStartColor: "#4fa5ff"
    readonly property color primaryCtaEndColor: "#1f73ff"
    readonly property color primaryCtaTextColor: "#ffffff"
    readonly property color secondaryCtaTextColor: "#3564ad"
    readonly property color secondaryCtaHoverColor: "#edf3ff"

    readonly property int heroWidth: 461
    readonly property int cardWidth: 400
    readonly property int cardRadius: 24
    readonly property int cardAccentHeight: 6
    readonly property int cardShadowSpread: 12
    readonly property int cardShadowOffsetY: 20

    readonly property int pageOuterPadding: 32
    readonly property int panelGap: 40
    readonly property int contentHorizontalPadding: 40
    readonly property int contentVerticalPadding: 36
    readonly property int headerSpacing: 10
    readonly property int sectionSpacing: 24
    readonly property int fieldSpacing: 16
    readonly property int helperSpacing: 12

    readonly property int primaryCtaHeight: 48
    readonly property int primaryCtaRadius: 14
    readonly property int primaryCtaBorderWidth: 1
}
