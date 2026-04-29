import QtQuick

QtObject {
    id: root

    // Background colors
    readonly property color pageBackgroundColor: "#f8fafb"
    readonly property color panelBackgroundColor: "#ffffff"
    readonly property color panelBorderColor: "#dfe5eb"
    readonly property color panelMutedFillColor: "#f3f5f7"

    // Text colors
    readonly property color mutedTextColor: "#5f6b76"
    readonly property color secondaryTextColor: "#666666"
    readonly property color tertiaryTextColor: "#888888"
    readonly property color strongTextColor: "#1f2933"
    readonly property color tableHeaderTextColor: "#4d5c6b"
    readonly property color tableBodyPrimaryTextColor: "#1b2a38"
    readonly property color tableBodySecondaryTextColor: "#4d5c6b"
    readonly property color tableBodyTertiaryTextColor: "#667585"

    // Accent colors
    readonly property color accentFillColor: "#dce8f5"
    readonly property color accentTextColor: "#4f6b8a"

    // Semantic colors
    readonly property color errorTextColor: "#f44336"
    readonly property color successTextColor: "#2e7d32"
    readonly property color successChipColor: "#4caf50"
    readonly property color warningChipColor: "#ff9800"
    readonly property color disabledChipColor: "#9e9e9e"

    // Spacing
    readonly property int pagePadding: 16
    readonly property int compactSpacing: 8
    readonly property int panelSpacing: 12
    readonly property int panelInset: 12
    readonly property int contentInset: 14

    // Radii
    readonly property int panelRadius: 10
    readonly property int innerPanelRadius: 8

    // Table column layout
    readonly property int fileRowHeight: 56
    readonly property int tableColumnSpacing: 12
    readonly property int fileTypeColumnWidth: 160
    readonly property int fileSizeColumnWidth: 120
    readonly property int fileUpdatedColumnWidth: 152
    readonly property int fileActionColumnWidth: 76

    // Rail (sidebar) colors
    readonly property color railBackgroundColor: "#f3f5f7"
    readonly property color railBorderColor: "#d6dde5"
    readonly property color railPanelColor: "#ffffff"
    readonly property color railTextColor: "#1f2933"
    readonly property color railMutedTextColor: "#6b7785"
    readonly property color railHoverColor: "#eef2f6"
    readonly property color railActiveColor: "#dce8f5"
    readonly property color railActiveStripeColor: "#4f6b8a"
    readonly property color railLogoutColor: "#8a4f4f"
    readonly property color headerMutedTextColor: "#5f6b76"
    readonly property color contentBorderColor: "#dfe5eb"
    readonly property color storageTrackColor: "#e5ebf1"

    // Rail dimensions
    readonly property int railWidth: 220
    readonly property int railSectionSpacing: 12
    readonly property int railOuterPadding: 16
}
