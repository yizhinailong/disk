import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopSettingsPage"

    function readSettingsSource() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/SettingsPage.qml"), false)
        xhr.send()
        verify(xhr.responseText.length > 0, "SettingsPage.qml was read")
        return xhr.responseText
    }

    function test_settings_page_has_server_connection_settings() {
        var source = readSettingsSource()

        verify(source.indexOf("服务器设置") !== -1,
               "Has server settings section")
        verify(source.indexOf("networkSettingsManager") !== -1,
               "Uses network settings manager")
        verify(source.indexOf("saveServerUrl") !== -1,
               "Can save server URL")
        verify(source.indexOf("resetServerUrl") !== -1,
               "Can reset server URL")
        verify(source.indexOf("healthManager.checkHealth()") !== -1,
               "Can test server health")
        verify(source.indexOf("测试连接") !== -1,
               "Has test connection button")
    }

    function test_settings_page_has_stable_page_background() {
        var source = readSettingsSource()

        verify(source.indexOf("background: Rectangle { color: theme.pageBackgroundColor }") !== -1,
               "Settings page uses the workspace page background")
    }

    function test_settings_page_does_not_add_avatar_ui() {
        var source = readSettingsSource()

        verify(source.indexOf("头像") === -1,
               "Does not add avatar UI")
        verify(source.indexOf("avatar") === -1,
               "Does not add avatar fields")
    }
}
