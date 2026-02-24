import QtQuick

QtObject {
    id: root

    // Colors
    property color primary: "#2196F3"
    property color primaryDark: "#1976D2"
    property color error: "#F44336"
    property color success: "#4CAF50"
    property color warning: "#FF9800"
    property color info: "#2196F3"
    property color border: "#E0E0E0"
    property color background: "#FAFAFA"
    property color surface: "#FFFFFF"
    property color textPrimary: "#212121"
    property color textSecondary: "#757575"

    // Spacing
    property int xs: 4
    property int sm: 8
    property int md: 16
    property int lg: 24
    property int xl: 32

    // Font sizes
    property int h1: 24
    property int body: 14
    property int caption: 12

    // Element sizes
    property int inputHeight: 36
    property int buttonHeight: 40

    // Border radius
    property int radiusSmall: 4
    property int radiusMedium: 8
}
