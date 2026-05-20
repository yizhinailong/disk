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
        adminManager.GetGlobalStorageStats()
        adminManager.GetSystemInfo()
        healthManager.checkHealth()
    }

    function healthStatusText(status) {
        if (status === "healthy") return qsTr("健康")
        if (status === "degraded") return qsTr("降级")
        if (status === "unhealthy") return qsTr("异常")
        return status || qsTr("未检测")
    }

    function healthStatusColor(status) {
        if (status === "healthy") return theme.successTextColor
        if (status === "unhealthy") return theme.errorTextColor
        return theme.secondaryTextColor
    }

    function formatUptime(seconds) {
        var totalSeconds = Number(seconds || 0)
        var days = Math.floor(totalSeconds / 86400)
        var hours = Math.floor((totalSeconds % 86400) / 3600)
        var minutes = Math.floor((totalSeconds % 3600) / 60)
        if (days > 0) {
            return days + "d " + hours + "h " + minutes + "m"
        }
        if (hours > 0) {
            return hours + "h " + minutes + "m"
        }
        return minutes + "m " + Math.floor(totalSeconds % 60) + "s"
    }

    Component.onCompleted: root.refreshStats()

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

            Rectangle {
                Layout.fillWidth: true
                color: theme.panelBackgroundColor
                radius: theme.panelRadius
                border.color: theme.panelBorderColor
                implicitHeight: globalStorageLayout.implicitHeight + 24

                ColumnLayout {
                    id: globalStorageLayout
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Label {
                        text: qsTr("全局存储统计")
                        font.bold: true
                        font.pixelSize: 14
                        color: theme.strongTextColor
                    }

                    GridLayout {
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 24

                        Label { text: qsTr("用户总数:"); font.bold: true }
                        Label { text: String(adminManager.globalStorageStats.totalUsers || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("文件总数:"); font.bold: true }
                        Label { text: String(adminManager.globalStorageStats.totalFiles || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("存储用量:"); font.bold: true }
                        Label { text: FormatUtils.formatStorageSize(adminManager.globalStorageStats.storageUsed || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("存储配额:"); font.bold: true }
                        Label { text: FormatUtils.formatStorageSize(adminManager.globalStorageStats.storageQuota || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("活跃分享:"); font.bold: true }
                        Label { text: String(adminManager.globalStorageStats.activeShares || 0); color: theme.secondaryTextColor }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                color: theme.panelBackgroundColor
                radius: theme.panelRadius
                border.color: theme.panelBorderColor
                implicitHeight: systemInfoLayout.implicitHeight + 24

                ColumnLayout {
                    id: systemInfoLayout
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Label {
                        text: qsTr("系统信息")
                        font.bold: true
                        font.pixelSize: 14
                        color: theme.strongTextColor
                    }

                    GridLayout {
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 24

                        Label { text: qsTr("版本:"); font.bold: true }
                        Label { text: adminManager.systemInfo.version || "—"; color: theme.secondaryTextColor }

                        Label { text: qsTr("Drogon 版本:"); font.bold: true }
                        Label { text: adminManager.systemInfo.drogonVersion || "—"; color: theme.secondaryTextColor }

                        Label { text: qsTr("构建时间:"); font.bold: true }
                        Label { text: adminManager.systemInfo.buildTime || "—"; color: theme.secondaryTextColor }

                        Label { text: qsTr("运行时间:"); font.bold: true }
                        Label { text: root.formatUptime(adminManager.systemInfo.uptime || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("当前连接:"); font.bold: true }
                        Label { text: String(adminManager.systemInfo.currentConnections || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("峰值连接:"); font.bold: true }
                        Label { text: String(adminManager.systemInfo.peakConnections || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("数据库连接池:"); font.bold: true }
                        Label { text: String(adminManager.systemInfo.dbPoolSize || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("Redis 连接池:"); font.bold: true }
                        Label { text: String(adminManager.systemInfo.redisPoolSize || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("文件夹总数:"); font.bold: true }
                        Label { text: String(adminManager.systemInfo.totalFolders || 0); color: theme.secondaryTextColor }

                        Label { text: qsTr("总文件大小:"); font.bold: true }
                        Label { text: FormatUtils.formatStorageSize(adminManager.systemInfo.totalSize || 0); color: theme.secondaryTextColor }
                    }
                }
            }

            // System status section
            Rectangle {
                Layout.fillWidth: true
                color: theme.panelBackgroundColor
                radius: theme.panelRadius
                border.color: theme.panelBorderColor
                implicitHeight: healthLayout.implicitHeight + 24

                ColumnLayout {
                    id: healthLayout
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12

                    Label {
                        text: qsTr("服务健康检查")
                        font.bold: true
                        font.pixelSize: 14
                        color: theme.strongTextColor
                    }

                    GridLayout {
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 24

                        Label { text: qsTr("总体状态:"); font.bold: true }
                        Label {
                            text: root.healthStatusText(healthManager.health.overallStatus)
                            color: root.healthStatusColor(healthManager.health.overallStatus)
                        }

                        Label { text: qsTr("版本:"); font.bold: true }
                        Label { text: healthManager.health.version || "—"; color: theme.secondaryTextColor }

                        Label { text: qsTr("运行时间:"); font.bold: true }
                        Label { text: healthManager.health.uptime || "—"; color: theme.secondaryTextColor }

                        Label { text: qsTr("检测耗时:"); font.bold: true }
                        Label { text: String(healthManager.health.totalCheckMs || 0) + " ms"; color: theme.secondaryTextColor }

                        Label { text: qsTr("检测时间:"); font.bold: true }
                        Label { text: healthManager.health.timestamp || "—"; color: theme.secondaryTextColor }

                        Label { text: qsTr("数据库健康:"); font.bold: true }
                        Label {
                            text: root.healthStatusText(healthManager.health.databaseStatus) + " / " + String(healthManager.health.databaseLatencyMs || 0) + " ms"
                            color: root.healthStatusColor(healthManager.health.databaseStatus)
                        }

                        Label { text: qsTr("数据库信息:"); font.bold: true; visible: healthManager.health.databaseMessage }
                        Label {
                            text: healthManager.health.databaseMessage || ""
                            color: theme.secondaryTextColor
                            visible: healthManager.health.databaseMessage
                        }

                        Label { text: qsTr("Redis 健康:"); font.bold: true }
                        Label {
                            text: root.healthStatusText(healthManager.health.redisStatus) + " / " + String(healthManager.health.redisLatencyMs || 0) + " ms"
                            color: root.healthStatusColor(healthManager.health.redisStatus)
                        }

                        Label { text: qsTr("Redis 信息:"); font.bold: true; visible: healthManager.health.redisMessage }
                        Label {
                            text: healthManager.health.redisMessage || ""
                            color: theme.secondaryTextColor
                            visible: healthManager.health.redisMessage
                        }
                    }
                }
            }

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
