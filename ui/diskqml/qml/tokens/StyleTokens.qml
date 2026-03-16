pragma Singleton
import QtQuick

QtObject {
    id: root

    // --- Colors ---
    // Primary
    readonly property color colorPrimary: "#06A7FF"
    readonly property color colorPrimaryHover: "#0091E0"
    readonly property color colorPrimaryLight: "#E6F6FF"

    // Functional
    readonly property color colorSuccess: "#52C41A"
    readonly property color colorWarning: "#FAAD14"
    readonly property color colorError: "#F5222D"
    readonly property color colorInfo: "#06A7FF"

    // Neutral
    readonly property color colorTextPrimary: "#1F2329"
    readonly property color colorTextSecondary: "#5F6B7A"
    readonly property color colorTextTertiary: "#8F959E"
    readonly property color colorBorder: "#DEE0E3"
    readonly property color colorBackground: "#F5F6F7"
    readonly property color colorSurface: "#FFFFFF"
    readonly property color colorHover: "#F5F6F7"

    // --- Typography ---
    readonly property int fontSizeH1: 20
    readonly property int fontWeightH1: Font.DemiBold
    
    readonly property int fontSizeH2: 16
    readonly property int fontWeightH2: Font.DemiBold
    
    readonly property int fontSizeH3: 14
    readonly property int fontWeightH3: Font.DemiBold
    
    readonly property int fontSizeBody: 14
    readonly property int fontWeightBody: Font.Normal
    
    readonly property int fontSizeSmall: 12
    readonly property int fontWeightSmall: Font.Normal

    // --- Spacing ---
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 16
    readonly property int spacingLg: 24
    readonly property int spacingXl: 32

    // --- Radius ---
    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12
    readonly property int radiusXl: 16
    readonly property int radiusFull: 999

    // --- Layout Metrics ---
    readonly property int titleBarHeight: 48
    readonly property int navBarWidth: 220
    readonly property int navBarCollapsedWidth: 64
    readonly property int toolBarHeight: 48
    readonly property int statusBarHeight: 36
    
    // --- Shadows ---
    // Note: QML DropShadow requires QtGraphicalEffects or Qt5Compat.GraphicalEffects
    // We provide the color and offset values here for use with DropShadow or custom rectangles
    readonly property color shadowColorSm: Qt.rgba(0, 0, 0, 0.05)
    readonly property int shadowOffsetYSm: 1
    readonly property int shadowRadiusSm: 2
    
    readonly property color shadowColorMd: Qt.rgba(0, 0, 0, 0.08)
    readonly property int shadowOffsetYMd: 2
    readonly property int shadowRadiusMd: 8
    
    readonly property color shadowColorLg: Qt.rgba(0, 0, 0, 0.12)
    readonly property int shadowOffsetYLg: 4
    readonly property int shadowRadiusLg: 16
    
    readonly property color shadowColorXl: Qt.rgba(0, 0, 0, 0.16)
    readonly property int shadowOffsetYXl: 8
    readonly property int shadowRadiusXl: 32
}
