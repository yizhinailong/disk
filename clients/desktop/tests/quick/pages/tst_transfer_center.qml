import QtQuick 2.15
import QtTest 1.15

TestCase {
    name: "DesktopTransferCenter"
    id: testTransferCenter

    function readTransferCenterSource() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", Qt.resolvedUrl("../../../qml/pages/TransferCenterPage.qml"), false)
        xhr.send()
        verify(xhr.responseText.length > 0, "TransferCenterPage.qml was read")
        return xhr.responseText
    }

    function test_transfer_center_has_tab_bar() {
        var source = readTransferCenterSource()

        verify(source.indexOf("TabBar") !== -1, "Has TabBar for upload/download tabs")
        verify(source.indexOf("TabButton") !== -1, "Has TabButtons")
    }

    function test_transfer_center_has_upload_list() {
        var source = readTransferCenterSource()

        verify(source.indexOf("model: transferManager.uploadModel") !== -1,
               "Upload ListView uses transferManager.uploadModel")
    }

    function test_transfer_center_has_download_list() {
        var source = readTransferCenterSource()

        verify(source.indexOf("model: transferManager.downloadModel") !== -1,
               "Download ListView uses transferManager.downloadModel")
    }

    function test_transfer_center_has_clear_buttons() {
        var source = readTransferCenterSource()

        verify(source.indexOf("ClearCompletedUploads") !== -1,
               "Upload clear button uses PascalCase TransferManager invokable")
        verify(source.indexOf("ClearCompletedDownloads") !== -1,
               "Download clear button uses PascalCase TransferManager invokable")
    }

    function test_transfer_center_shows_upload_status() {
        var source = readTransferCenterSource()

        // Upload delegate should show status text with mapping
        verify(source.indexOf("model.status") !== -1, "References model.status")
        verify(source.indexOf("model.filename") !== -1, "References model.filename")
    }

    function test_transfer_center_has_progress_bar() {
        var source = readTransferCenterSource()

        verify(source.indexOf("ProgressBar") !== -1, "Has ProgressBar for transfers")
    }

    function test_transfer_center_aligns_upload_controls_to_pascal_case_transfer_manager_api() {
        var source = readTransferCenterSource()

        verify(source.indexOf("CancelUpload") !== -1,
               "Has cancel upload button wired to PascalCase invokable")
        verify(source.indexOf("RetryUpload") !== -1,
               "Has retry upload button wired to PascalCase invokable")
        verify(source.indexOf("ClearCompletedUploads") !== -1,
               "Upload clear actions use PascalCase invokable")
        verify(source.indexOf("cancelUpload") === -1,
               "Does not keep lower-case upload cancel wrapper calls")
        verify(source.indexOf("retryUpload") === -1,
               "Does not keep lower-case upload retry wrapper calls")
        verify(source.indexOf("clearCompletedUploads") === -1,
               "Does not keep lower-case upload clear wrapper calls")
        verify(source.indexOf("PauseDownload") !== -1,
               "Has pause download button wired to PascalCase invokable")
        verify(source.indexOf("ResumeDownload") !== -1,
               "Has resume download button wired to PascalCase invokable")
        verify(source.indexOf("CancelDownload") !== -1,
               "Has cancel download button wired to PascalCase invokable")
        verify(source.indexOf("RetryDownload") !== -1,
               "Has retry download button wired to PascalCase invokable")
        verify(source.indexOf("ClearCompletedDownloads") !== -1,
               "Download clear actions use PascalCase invokable")
        verify(source.indexOf("pauseDownload") === -1,
               "Does not keep lower-case download pause wrapper calls")
        verify(source.indexOf("resumeDownload") === -1,
               "Does not keep lower-case download resume wrapper calls")
        verify(source.indexOf("cancelDownload") === -1,
               "Does not keep lower-case download cancel wrapper calls")
        verify(source.indexOf("retryDownload") === -1,
               "Does not keep lower-case download retry wrapper calls")
        verify(source.indexOf("clearCompletedDownloads") === -1,
               "Does not keep lower-case download clear wrapper calls")
    }

    function test_transfer_center_has_pause_resume() {
        var source = readTransferCenterSource()

        verify(source.indexOf("PauseDownload") !== -1, "Has pause download button")
        verify(source.indexOf("ResumeDownload") !== -1, "Has resume download button")
    }

    function test_transfer_center_empty_state_labels() {
        var source = readTransferCenterSource()

        verify(source.indexOf("No uploads") !== -1, "Has empty uploads label")
        verify(source.indexOf("No downloads") !== -1, "Has empty downloads label")
    }
}
