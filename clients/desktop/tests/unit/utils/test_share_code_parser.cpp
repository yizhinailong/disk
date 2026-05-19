#include <QTest>

#include "utils/ShareCodeParser.hpp"

using namespace disk::desktop::utils;

class TestShareCodeParser : public QObject {
    Q_OBJECT

private slots:

    void ParsePureCode() {
        QCOMPARE(ShareCodeParser::ParseShareInput("abc12345"), QString("abc12345"));
    }

    void ParseFullUrl() {
        QCOMPARE(
            ShareCodeParser::ParseShareInput("https://disk.example.com/s/abc12345"),
            QString("abc12345")
        );
    }

    void ParsePathFormat() {
        QCOMPARE(ShareCodeParser::ParseShareInput("/s/abc12345"), QString("abc12345"));
    }

    void ParseUrlWithTrailingSlash() {
        QCOMPARE(
            ShareCodeParser::ParseShareInput("https://disk.example.com/s/abc12345/"),
            QString("abc12345")
        );
    }

    void ParseEmptyInput() {
        QCOMPARE(ShareCodeParser::ParseShareInput(""), QString(""));
    }

    void ParseInvalidFormat() {
        QCOMPARE(ShareCodeParser::ParseShareInput("/s/"), QString(""));
    }

    void ParseWhitespaceTrimming() {
        QCOMPARE(ShareCodeParser::ParseShareInput("  abc12345  "), QString("abc12345"));
    }

    void ParseMixedCaseUrl() {
        QCOMPARE(
            ShareCodeParser::ParseShareInput("HTTPS://DISK.EXAMPLE.COM/s/AbC12345"),
            QString("AbC12345")
        );
    }
};

int run_TestShareCodeParser(int argc, char* argv[]) {
    TestShareCodeParser test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_share_code_parser.moc"
