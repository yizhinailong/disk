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
            readQmlSource("components/admin/SystemTab.qml"),
            readQmlSource("components/admin/UserDetailDialog.qml"),
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
        verify(source.indexOf("系统监控") !== -1,
               "Has system monitoring nav label")
    }

    function test_admin_shell_uses_stackview_not_tabs() {
        var source = readAdminShellSource()

        verify(source.indexOf("StackView") !== -1, "Has StackView")
        verify(source.indexOf("TabBar") === -1,
               "Does NOT use TabBar (uses StackView pages)")
        verify(source.indexOf("SwipeView") === -1,
               "Does NOT use SwipeView (uses StackView pages)")
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
        verify(source.indexOf("userDetailDialog.open()") !== -1, "Opens user detail dialog")
    }

    function test_share_tab_has_filter_and_table() {
        var source = readQmlSource("components/admin/ShareTab.qml")

        verify(source.indexOf("活跃") !== -1, "Has active status filter")
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
    }

    function test_system_tab_shows_connection_status() {
        var source = readQmlSource("components/admin/SystemTab.qml")

        verify(source.indexOf("MySQL") !== -1, "Shows MySQL status")
        verify(source.indexOf("Redis") !== -1, "Shows Redis status")
        verify(source.indexOf("磁盘使用率") !== -1, "Shows disk usage")
        verify(source.indexOf("运行时间") !== -1, "Shows uptime")
    }

    function test_admin_shell_uses_workspace_theme() {
        var source = readAdminShellSource()

        verify(source.indexOf("WorkspaceTheme") !== -1, "Uses WorkspaceTheme")
    }
}
