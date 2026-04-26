#include <QTest>

#include "managers/TransferManager.hpp"

using namespace disk::desktop::managers;

class TestTransferState : public QObject {
    Q_OBJECT

private slots:

    void UploadStateQueuedString() {
        QCOMPARE(ToString(UploadState::Queued), QString("queued"));
    }

    void UploadStateHashingString() {
        QCOMPARE(ToString(UploadState::Hashing), QString("hashing"));
    }

    void UploadStateInitializingString() {
        QCOMPARE(ToString(UploadState::Initializing), QString("initializing"));
    }

    void UploadStateUploadingString() {
        QCOMPARE(ToString(UploadState::Uploading), QString("uploading"));
    }

    void UploadStateCompletingString() {
        QCOMPARE(ToString(UploadState::Completing), QString("completing"));
    }

    void UploadStateCompletedString() {
        QCOMPARE(ToString(UploadState::Completed), QString("completed"));
    }

    void UploadStateCancelledString() {
        QCOMPARE(ToString(UploadState::Cancelled), QString("cancelled"));
    }

    void UploadStateExpiredString() {
        QCOMPARE(ToString(UploadState::Expired), QString("expired"));
    }

    void UploadStateFailedString() {
        QCOMPARE(ToString(UploadState::Failed), QString("failed"));
    }

    void UploadStateInstantUploadedMapsToCompleted() {
        QCOMPARE(ToString(UploadState::InstantUploaded), QString("completed"));
    }

    void UploadStateResumingMapsToUploading() {
        QCOMPARE(ToString(UploadState::Resuming), QString("uploading"));
    }

    void UploadStateCancelPendingString() {
        QCOMPARE(ToString(UploadState::CancelPending), QString("cancelling"));
    }

    void DownloadStateIdleString() {
        QCOMPARE(ToString(DownloadState::Idle), QString("idle"));
    }

    void DownloadStateFetchingMetadataString() {
        QCOMPARE(ToString(DownloadState::FetchingMetadata), QString("fetching_metadata"));
    }

    void DownloadStateReadyString() {
        QCOMPARE(ToString(DownloadState::Ready), QString("ready"));
    }

    void DownloadStateTransferringFullMapsToDownloading() {
        QCOMPARE(ToString(DownloadState::TransferringFull), QString("downloading"));
    }

    void DownloadStateTransferringRangeMapsToDownloading() {
        QCOMPARE(ToString(DownloadState::TransferringRange), QString("downloading"));
    }

    void DownloadStatePausedString() {
        QCOMPARE(ToString(DownloadState::Paused), QString("paused"));
    }

    void DownloadStateRetryWaitingString() {
        QCOMPARE(ToString(DownloadState::RetryWaiting), QString("retry_waiting"));
    }

    void DownloadStateCompletedString() {
        QCOMPARE(ToString(DownloadState::Completed), QString("completed"));
    }

    void DownloadStateCancelledString() {
        QCOMPARE(ToString(DownloadState::Cancelled), QString("cancelled"));
    }

    void DownloadStateFailedString() {
        QCOMPARE(ToString(DownloadState::Failed), QString("failed"));
    }

    void UploadStatesAreDistinct() {
        QSet<QString> states;
        auto add_if_unique = [&](UploadState s) {
            auto str = ToString(s);
            QVERIFY2(!states.contains(str), qPrintable(QString("Duplicate upload state: %1").arg(str)));
            states.insert(str);
        };
        add_if_unique(UploadState::Queued);
        add_if_unique(UploadState::Hashing);
        add_if_unique(UploadState::Initializing);
        add_if_unique(UploadState::Uploading);
        add_if_unique(UploadState::Completing);
        add_if_unique(UploadState::CancelPending);
        add_if_unique(UploadState::Completed);
        add_if_unique(UploadState::Cancelled);
        add_if_unique(UploadState::Expired);
        add_if_unique(UploadState::Failed);
    }

    void DownloadStatesAreDistinct() {
        QSet<QString> states;
        auto add_if_unique = [&](DownloadState s) {
            auto str = ToString(s);
            QVERIFY2(!states.contains(str), qPrintable(QString("Duplicate download state: %1").arg(str)));
            states.insert(str);
        };
        add_if_unique(DownloadState::Idle);
        add_if_unique(DownloadState::FetchingMetadata);
        add_if_unique(DownloadState::Ready);
        add_if_unique(DownloadState::Paused);
        add_if_unique(DownloadState::RetryWaiting);
        add_if_unique(DownloadState::Completed);
        add_if_unique(DownloadState::Cancelled);
        add_if_unique(DownloadState::Failed);
    }

    void DownloadTransferringStatesShareString() {
        QCOMPARE(ToString(DownloadState::TransferringFull), QString("downloading"));
        QCOMPARE(ToString(DownloadState::TransferringRange), QString("downloading"));
    }
};

int run_TestTransferState(int argc, char* argv[]) {
    TestTransferState test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_transfer_state.moc"
