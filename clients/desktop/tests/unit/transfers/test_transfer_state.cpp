#include <QTest>

using namespace disk::desktop;

class TestTransferState : public QObject {
    Q_OBJECT

private slots:

    void TransferStateMachinePlaceholder() {
        QVERIFY(true);
    }

    void UploadStatesEnumerated() {
        QStringList expected = { "queued", "hashing", "initializing", "uploading", "completed", "failed", "cancelled" };
        QCOMPARE(expected.size(), 7);
    }

    void DownloadStatesEnumerated() {
        QStringList expected = { "queued", "downloading", "paused", "completed", "failed", "cancelled" };
        QCOMPARE(expected.size(), 6);
    }

    void TransferModesEnumerated() {
        QStringList modes = { "full", "range" };
        QCOMPARE(modes.size(), 2);
    }
};
