import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopAdminPage"
    id: testAdminPage

    function readAdminShellSource() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/shells/AdminShell.qml"), false)
        xhr.send()
        verify(xhr.responseText.length > 0, "AdminShell.qml was read")
        return xhr.responseText
    }

    function readAdminCompositeSource() {
        return [
            readAdminShellSource(),
            readQmlSource("components/admin/UserTab.qml"),
            readQmlSource("components/admin/ShareTab.qml"),
            readQmlSource("components/admin/OperationLogTab.qml"),
            readQmlSource("components/admin/SystemTab.qml"),
            readQmlSource("components/admin/UserDetailDialog.qml"),
            readQmlSource("components/admin/OperationLogDetailDialog.qml"),
            readQmlSource("components/admin/ConfirmDialog.qml")
        ].join("\n")
    }

    function readQmlSource(relPath) {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/" + relPath), false)
        xhr.send()
        verify(xhr.responseText.length > 0, relPath + " was read")
        return xhr.responseText
    }

    function test_admin_shell_has_three_page_destinations() {
        var source = readAdminShellSource()

        verify(source.indexOf("用户管理") !== -1,
               "Has user management nav label")
        verify(source.indexOf("分享管理") !== -1,
               "Has share management nav label")
        verify(source.indexOf("操作日志") !== -1,
               "Has operation log nav label")
        verify(source.indexOf("系统监控") !== -1,
               "Has system monitoring nav label")
    }

    function test_admin_shell_uses_persistent_page_loaders() {
        var source = readAdminShellSource()

        verify(source.indexOf("adminPageHost") !== -1,
               "Has persistent admin page host")
        verify(source.indexOf("adminUsersPageLoader") !== -1,
               "Has persistent users page loader")
        verify(source.indexOf("adminSharesPageLoader") !== -1,
               "Has persistent shares page loader")
        verify(source.indexOf("adminLogsPageLoader") !== -1,
               "Has persistent logs page loader")
        verify(source.indexOf("adminSystemPageLoader") !== -1,
               "Has persistent system page loader")
        verify(source.indexOf("stackView.replace") === -1,
               "Does not destroy/recreate admin pages on navigation")
        verify(source.indexOf("TabBar") === -1,
               "Does NOT use TabBar")
        verify(source.indexOf("SwipeView") === -1,
               "Does NOT use SwipeView")
    }

    function test_admin_list_tabs_track_loading_state() {
        var source = readAdminCompositeSource()

        verify(source.indexOf("isLoadingUsers") !== -1, "User tab tracks loading state")
        verify(source.indexOf("hasLoadedUsers") !== -1, "User tab tracks first load")
        verify(source.indexOf("isLoadingShares") !== -1, "Share tab tracks loading state")
        verify(source.indexOf("hasLoadedShares") !== -1, "Share tab tracks first load")
        verify(source.indexOf("isLoadingOperationLogs") !== -1, "Operation log tab tracks loading state")
        verify(source.indexOf("hasLoadedOperationLogs") !== -1, "Operation log tab tracks first load")
    }

    function test_admin_page_references_admin_manager() {
        var source = readAdminCompositeSource()

        verify(source.indexOf("adminManager") !== -1, "References adminManager")
    }

    function test_admin_page_uses_confirm_dialog_for_destructive_actions() {
        var source = readAdminCompositeSource()

        verify(source.indexOf("ConfirmDialog") !== -1, "Uses ConfirmDialog")
        verify(source.indexOf("confirmed()") !== -1, "ConfirmDialog emits confirmed signal")
        verify(source.indexOf("cancelled()") !== -1, "ConfirmDialog emits cancelled signal")
    }

    function test_admin_page_calls_list_users() {
        var source = readAdminCompositeSource()

        verify(source.indexOf("ListUsers") !== -1, "Calls adminManager.ListUsers")
    }

    function test_user_tab_has_filter_controls() {
        var source = readQmlSource("components/admin/UserTab.qml")

        verify(source.indexOf("搜索用户名") !== -1, "Has username search field")
        verify(source.indexOf("正常") !== -1, "Has status filter options")
        verify(source.indexOf("用户") !== -1, "Has role filter options")
        verify(source.indexOf("管理员") !== -1, "Has admin role filter option")
    }

    function test_user_tab_has_table_columns() {
        var source = readQmlSource("components/admin/UserTab.qml")

        verify(source.indexOf("ID") !== -1, "Has ID column")
        verify(source.indexOf("用户名") !== -1, "Has username column")
        verify(source.indexOf("邮箱") !== -1, "Has email column")
        verify(source.indexOf("角色") !== -1, "Has role column")
        verify(source.indexOf("状态") !== -1, "Has status column")
        verify(source.indexOf("存储用量") !== -1, "Has storage column")
        verify(source.indexOf("操作") !== -1, "Has action column")
    }

    function test_user_tab_has_pagination() {
        var source = readQmlSource("components/admin/UserTab.qml")

        verify(source.indexOf("上一页") !== -1, "Has previous page button")
        verify(source.indexOf("下一页") !== -1, "Has next page button")
    }

    function test_user_tab_has_action_buttons() {
        var source = readQmlSource("components/admin/UserTab.qml")

        verify(source.indexOf("修改状态") !== -1, "Has change status action")
        verify(source.indexOf("修改角色") !== -1, "Has change role action")
        verify(source.indexOf("删除") !== -1, "Has delete action")
    }

    function test_user_tab_opens_detail_dialog() {
        var source = readQmlSource("components/admin/UserTab.qml")

        verify(source.indexOf("UserDetailDialog") !== -1, "Uses UserDetailDialog")
        verify(source.indexOf("GetUserDetail") !== -1, "Calls user detail API")
        verify(source.indexOf("onUserDetailLoaded") !== -1, "Handles user detail API response")
        verify(source.indexOf("userDetailDialogRef.open()") !== -1, "Opens user detail dialog")
    }

    function test_user_detail_dialog_has_available_space_editor() {
        var source = readQmlSource("components/admin/UserDetailDialog.qml")

        verify(source.indexOf("预留空间") !== -1, "Shows reserved storage")
        verify(source.indexOf("可用空间") !== -1, "Shows available storage")
        verify(source.indexOf("修改可用空间") !== -1, "Has available space edit label")
        verify(source.indexOf("availableSpaceSpinBox") !== -1, "Has G unit input")
        verify(source.indexOf("changeAvailableSpaceRequested") !== -1, "Emits available space change request")
        verify(source.indexOf("qsTr(\"G\")") !== -1, "Displays G unit")
    }

    function test_user_tab_wires_available_space_update() {
        var source = readQmlSource("components/admin/UserTab.qml")

        verify(source.indexOf("storage_reserved") !== -1, "Passes storage_reserved to detail dialog")
        verify(source.indexOf("ChangeUserAvailableSpace") !== -1, "Calls available space update API")
        verify(source.indexOf("确定将该用户可用空间修改为 %1 G 吗？") !== -1,
               "Confirms available space update with G unit")
    }

    function test_share_tab_has_filter_and_table() {
        var source = readQmlSource("components/admin/ShareTab.qml")

        verify(source.indexOf("搜索分享者") !== -1, "Has sharer search field")
        verify(source.indexOf("搜索") !== -1, "Has search button")
        verify(source.indexOf("searchUsername") !== -1, "Keeps sharer search state")
        verify(source.indexOf("ListShares(root.currentPage, root.pageSize, root.filterStatus, -1, root.searchUsername)") !== -1,
               "Passes sharer search to ListShares")
        verify(source.indexOf("有效") !== -1, "Has active status filter")
        verify(source.indexOf("已过期") !== -1, "Has expired status filter")
        verify(source.indexOf("已取消") !== -1, "Has cancelled status filter")
        verify(source.indexOf("分享者") !== -1, "Has sharer column")
        verify(source.indexOf("文件名") !== -1, "Has file name column")
        verify(source.indexOf("分享码") !== -1, "Has share code column")
    }

    function test_share_tab_has_actions() {
        var source = readQmlSource("components/admin/ShareTab.qml")

        verify(source.indexOf("查看详情") !== -1, "Has view detail action")
        verify(source.indexOf("强制取消") !== -1, "Has force cancel action")
        verify(source.indexOf("ForceCancelShare") !== -1, "Calls ForceCancelShare")
        verify(source.indexOf("enabled: model.status !== 0") !== -1,
               "Disables force cancel for already cancelled shares")
        verify(source.indexOf("model.shareCode || \"\"") !== -1,
               "Uses AdminShareListModel shareCode role in confirmation")
    }

    function test_system_tab_has_stats_and_refresh() {
        var source = readQmlSource("components/admin/SystemTab.qml")

        verify(source.indexOf("总用户数") !== -1, "Shows total users stat")
        verify(source.indexOf("总文件数") !== -1, "Shows total files stat")
        verify(source.indexOf("存储用量") !== -1, "Shows storage usage stat")
        verify(source.indexOf("活跃分享") !== -1, "Shows active shares stat")
        verify(source.indexOf("刷新") !== -1, "Has refresh button")
        verify(source.indexOf("GetOverviewStatsApi") !== -1, "Calls GetOverviewStatsApi")
        verify(source.indexOf("GetSystemStatusApi") !== -1, "Calls GetSystemStatusApi")
        verify(source.indexOf("GetGlobalStorageStats") !== -1, "Calls GetGlobalStorageStats")
        verify(source.indexOf("GetSystemInfo") !== -1, "Calls GetSystemInfo")
        verify(source.indexOf("healthManager.checkHealth()") !== -1, "Calls health endpoint")
        verify(source.indexOf("服务健康检查") !== -1, "Shows service health section")
    }

    function test_operation_log_tab_is_wired() {
        var source = readAdminCompositeSource()

        verify(source.indexOf("OperationLogTab") !== -1, "Has operation log tab")
        verify(source.indexOf("adminNavLogsButton") !== -1, "Has operation log navigation button")
        verify(source.indexOf("ListOperationLogs") !== -1, "Calls ListOperationLogs")
        verify(source.indexOf("operationLogModel") !== -1, "Binds operationLogModel")
        verify(source.indexOf("onOperationLogPaginationLoaded") !== -1, "Handles log pagination")
        verify(source.indexOf("OperationLogDetailDialog") !== -1, "Has operation log detail dialog")
        verify(source.indexOf("查看详情") !== -1, "Has view detail action")
        verify(source.indexOf("openLogDetail") !== -1, "Opens operation log detail dialog")
        verify(source.indexOf("details = details || \"\"") !== -1, "Passes full detail text to dialog")
    }

    function test_system_tab_shows_connection_status() {
        var source = readQmlSource("components/admin/SystemTab.qml")

        verify(source.indexOf("MySQL") !== -1, "Shows MySQL status")
        verify(source.indexOf("Redis") !== -1, "Shows Redis status")
        verify(source.indexOf("磁盘使用率") !== -1, "Shows disk usage")
        verify(source.indexOf("运行时间") !== -1, "Shows uptime")
        verify(source.indexOf("系统信息") !== -1, "Shows system info section")
        verify(source.indexOf("Drogon 版本") !== -1, "Shows Drogon version")
        verify(source.indexOf("全局存储统计") !== -1, "Shows global storage section")
    }

    function test_admin_shell_uses_workspace_theme() {
        var source = readAdminShellSource()

        verify(source.indexOf("WorkspaceTheme") !== -1, "Uses WorkspaceTheme")
    }

    function test_admin_shell_keeps_logout_visible_below_navigation() {
        var source = readAdminShellSource()

        verify(source.indexOf("objectName: \"adminSidebarSpacer\"") !== -1,
               "Sidebar has flexible spacer before session card")
        verify(source.indexOf("Layout.preferredHeight: adminNavigationContent.implicitHeight + 24") !== -1,
               "Navigation panel sizes to content instead of filling the rail")
        verify(source.indexOf("Layout.preferredHeight: sessionContent.implicitHeight + 16") !== -1,
               "Session card reserves explicit height for logout")
    }
}
