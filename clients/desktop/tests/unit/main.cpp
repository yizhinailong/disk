#include <QTest>

class DesktopStubTest : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        QVERIFY(true);
    }

    void stubTest() {
        QCOMPARE(1 + 1, 2);
    }

    void cleanupTestCase() {
        QVERIFY(true);
    }
};

QTEST_MAIN(DesktopStubTest)
#include "main.moc"
