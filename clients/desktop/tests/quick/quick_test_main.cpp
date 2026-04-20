#include <QQuickTest>

int main(int argc, char** argv) {
    QTEST_SET_MAIN_SOURCE_PATH
    return quick_test_main(argc, argv, "desktop-quick-tests", QUICK_TEST_SOURCE_DIR);
}
