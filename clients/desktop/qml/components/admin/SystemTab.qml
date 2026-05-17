import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import "../FormatUtils.js" as FormatUtils

Item {
    id: root

    WorkspaceTheme { id: theme }

    function refreshStats() {
        adminManager.GetOverviewStatsApi()
        adminManager.GetSystemStatusApi()
    }

    Component.onCompleted: {
        adminManager.GetOverviewStatsApi()
        adminManager.GetSystemStatusApi()
    }

    Flickable {
        anchors.fill: parent
        contentWidth: parent.width
        contentHeight: contentLayout.implicitHeight + 32
        clip: true

        ColumnLayout {
            id: contentLayout
            anchors.fill: parent
            anchors.margins: theme.pagePadding
            spacing: theme.panelSpacing

            // Header with refresh
            RowLayout {
                Layout.fillWidth: true

                Label {
                    text: qsTr("系统概览")
                    font.bold: true
                    font.pixelSize: 16
                    color: theme.strongTextColor
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("刷新")
                    highlighted: true
                    onClicked: root.refreshStats()
                }
            }

            // Overview stats cards
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                rowSpacing: theme.compactSpacing
                columnSpacing: theme.compactSpacing

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    color: theme.panelBackgroundColor
                    radius: theme.panelRadius
                    border.color: theme.panelBorderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Label {
                            text: qsTr("总用户数")
                            font.pixelSize: 12
                            color: theme.mutedTextColor
                        }

                        Label {
                            text: String(adminManager.overviewStats.totalUsers || 0)
                            font.pixelSize: 24
                            font.bold: true
                            color: theme.strongTextColor
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    color: theme.panelBackgroundColor
                    radius: theme.panelRadius
                    border.color: theme.panelBorderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Label {
                            text: qsTr("总文件数")
                            font.pixelSize: 12
                            color: theme.mutedTextColor
                        }

                        Label {
                            text: String(adminManager.overviewStats.totalFiles || 0)
                            font.pixelSize: 24
                            font.bold: true
                            color: theme.strongTextColor
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    color: theme.panelBackgroundColor
                    radius: theme.panelRadius
                    border.color: theme.panelBorderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Label {
                            text: qsTr("存储用量 / 配额")
                            font.pixelSize: 12
                            color: theme.mutedTextColor
                        }

                        Label {
                            text: {
                                var used = adminManager.overviewStats.storageUsed || 0
                                var quota = adminManager.overviewStats.storageQuota || 0
                                return FormatUtils.formatStorageSize(used) + " / " + FormatUtils.formatStorageSize(quota)
                            }
                            font.pixelSize: 18
                            font.bold: true
                            color: theme.strongTextColor
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    color: theme.panelBackgroundColor
                    radius: theme.panelRadius
                    border.color: theme.panelBorderColor

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 4

                        Label {
                            text: qsTr("活跃分享")
                            font.pixelSize: 12
                            color: theme.mutedTextColor
                        }

                        Label {
                            text: String(adminManager.overviewStats.activeShares || 0)
                            font.pixelSize: 24
                            font.bold: true
                            color: theme.strongTextColor
                        }
                    }
                }
            }

            // System status section
            Rectangle {
                Layout.fillWidth: true
                color: theme.panelBackgroundColor
                radius: theme.panelRadius
                border.color: theme.panelBorderColor
                implicitHeight: statusLayout.implicitHeight + 24

                ColumnLayout {
                    id: statusLayout
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Label {
                        text: qsTr("系统状态")
                        font.bold: true
                        font.pixelSize: 14
                        color: theme.strongTextColor
                    }

                    GridLayout {
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 24

                        Label {
                            text: qsTr("MySQL 连接:")
                            font.bold: true
                        }
                        Label {
                            text: adminManager.systemStatus.mysqlConnected === true ? qsTr("正常") : qsTr("异常")
                            color: adminManager.systemStatus.mysqlConnected === true ? theme.successTextColor : theme.errorTextColor
                        }

                        Label {
                            text: qsTr("Redis 连接:")
                            font.bold: true
                        }
                        Label {
                            text: adminManager.systemStatus.redisConnected === true ? qsTr("正常") : qsTr("异常")
                            color: adminManager.systemStatus.redisConnected === true ? theme.successTextColor : theme.errorTextColor
                        }

                        Label {
                            text: qsTr("磁盘使用率:")
                            font.bold: true
                        }
                        Label {
                            text: {
                                var usage = adminManager.systemStatus.diskUsage || 0
                                return usage + "%"
                            }
                            color: theme.secondaryTextColor
                        }

                        Label {
                            text: qsTr("运行时间:")
                            font.bold: true
                        }
                        Label {
                            text: adminManager.systemStatus.uptime || "—"
                            color: theme.secondaryTextColor
                        }
                    }
                }
            }

            Item {
                Layout.fillHeight: true
            }
        }
    }
}
