# ─── Unit tests (C++ with Qt Test) ────────────────────────────────────────────

set(UNIT_TEST_SOURCES
    unit/main.cpp
    unit/auth/test_owner_session.cpp
    unit/auth/test_visitor_session.cpp
    unit/models/test_drive_item.cpp
    unit/models/test_drive_list_model.cpp
    unit/models/test_error_adapter.cpp
    unit/navigation/test_shell_controller.cpp
    unit/transfers/test_transfer_state.cpp
)

set(DESKTOP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)

qt_add_executable(desktop-unit-tests
    ${UNIT_TEST_SOURCES}
    helpers/MockNetworkAccessManager.hpp
    helpers/MockReplyFactory.hpp
    helpers/TestJsonLoader.hpp

    # Source files under test (linked directly, same as backend test pattern)
    ${DESKTOP_SRC_DIR}/auth/OwnerSessionManager.cpp
    ${DESKTOP_SRC_DIR}/auth/AuthService.cpp
    ${DESKTOP_SRC_DIR}/auth/VisitorSessionManager.cpp
    ${DESKTOP_SRC_DIR}/auth/SessionStore.cpp
    ${DESKTOP_SRC_DIR}/models/DriveItem.cpp
    ${DESKTOP_SRC_DIR}/models/DriveListModel.cpp
    ${DESKTOP_SRC_DIR}/models/FolderTreeModel.cpp
    ${DESKTOP_SRC_DIR}/network/NetworkClient.cpp
    ${DESKTOP_SRC_DIR}/network/RequestFactory.cpp
    ${DESKTOP_SRC_DIR}/network/ErrorAdapter.cpp
    ${DESKTOP_SRC_DIR}/managers/DriveManager.cpp
    ${DESKTOP_SRC_DIR}/app/ShellController.cpp
)

target_include_directories(desktop-unit-tests PRIVATE
    ${DESKTOP_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(desktop-unit-tests PRIVATE
    QT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    DESKTOP_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../qml"
)

target_link_libraries(desktop-unit-tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Network
    Qt6::Test
)

# Copy fixtures to build directory for runtime access
file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(
    NAME    desktop-unit-tests
    COMMAND desktop-unit-tests
)

# ─── Quick tests (QML with Qt Quick Test) ─────────────────────────────────────

qt_add_executable(desktop-quick-tests
    quick/quick_test_main.cpp
)

qt_add_qml_module(desktop-quick-tests
    URI DesktopQuickTests
    VERSION 1.0
    QML_FILES
        quick/navigation/tst_navigation.qml
        quick/shells/tst_owner_shell.qml
        quick/shells/tst_visitor_shell.qml
    RESOURCE_PREFIX /qt/qml
)

target_include_directories(desktop-quick-tests PRIVATE
    ${DESKTOP_SRC_DIR}
)

target_compile_definitions(desktop-quick-tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/quick"
)

target_link_libraries(desktop-quick-tests PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickTest
    Qt6::Test
)

set_target_properties(desktop-quick-tests PROPERTIES
    QUICK_TEST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/quick"
)

add_test(
    NAME    desktop-quick-tests
    COMMAND desktop-quick-tests -platform offscreen
)
