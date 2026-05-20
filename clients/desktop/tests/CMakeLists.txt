# ─── Unit tests (C++ with Qt Test) ────────────────────────────────────────────

set(UNIT_TEST_SOURCES
    unit/main.cpp
    unit/app/test_visitor_flow.cpp
    unit/auth/test_owner_session.cpp
    unit/auth/test_visitor_session.cpp
    unit/auth/test_auth_service_logout.cpp
    unit/auth/test_auth_service_refresh.cpp
    unit/auth/test_session_store.cpp
    unit/models/test_drive_item.cpp
    unit/models/test_drive_list_model.cpp
    unit/models/test_admin_share_list_model.cpp
    unit/models/test_error_adapter.cpp
    unit/models/test_folder_tree_model.cpp
    unit/models/test_upload_task_model.cpp
    unit/models/test_download_task_model.cpp
    unit/models/test_admin_user_list_model.cpp
    unit/navigation/test_shell_controller.cpp
    unit/network/test_request_factory.cpp
    unit/managers/test_drive_manager.cpp
    unit/managers/test_admin_manager.cpp
    unit/transfers/test_transfer_state.cpp
    unit/transfers/test_transfer_manager.cpp
    unit/utils/test_share_code_parser.cpp
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
    ${DESKTOP_SRC_DIR}/models/AdminShareListModel.cpp
    ${DESKTOP_SRC_DIR}/models/OperationLogListModel.cpp
    ${DESKTOP_SRC_DIR}/models/FolderTreeModel.cpp
    ${DESKTOP_SRC_DIR}/models/UploadTaskModel.cpp
    ${DESKTOP_SRC_DIR}/models/DownloadTaskModel.cpp
    ${DESKTOP_SRC_DIR}/models/AdminUserListModel.cpp
    ${DESKTOP_SRC_DIR}/network/NetworkClient.cpp
    ${DESKTOP_SRC_DIR}/network/RequestFactory.cpp
    ${DESKTOP_SRC_DIR}/network/ErrorAdapter.cpp
    ${DESKTOP_SRC_DIR}/managers/AdminManager.cpp
    ${DESKTOP_SRC_DIR}/managers/DriveManager.cpp
    ${DESKTOP_SRC_DIR}/managers/TransferManager.cpp
    ${DESKTOP_SRC_DIR}/app/ShellController.cpp
    ${DESKTOP_SRC_DIR}/utils/ShareCodeParser.cpp
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

desktop_add_advisory_warnings(desktop-unit-tests)

# Copy fixtures to build directory for runtime access
file(COPY
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(
    NAME    desktop-unit-tests
    COMMAND desktop-unit-tests
)

# Derive Qt bin directory for ctest PATH (Windows needs Qt DLLs on PATH).
# e.g. D:/TOOL/Qt/6.11.0/llvm-mingw_64/lib/cmake/Qt6Core -> D:/TOOL/Qt/6.11.0/llvm-mingw_64/bin
get_filename_component(QT_BIN_DIR "${Qt6Core_DIR}/../../../bin" ABSOLUTE)

if(WIN32)
    set_tests_properties(desktop-unit-tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}"
    )
endif()

# ─── Quick tests (QML with Qt Quick Test) ─────────────────────────────────────

qt_add_executable(desktop-quick-tests
    quick/quick_test_main.cpp
    ${DESKTOP_SRC_DIR}/models/DriveListModel.cpp
    ${DESKTOP_SRC_DIR}/models/FolderTreeModel.cpp
)

qt_add_qml_module(desktop-quick-tests
    URI DesktopQuickTests
    VERSION 1.0
    QML_FILES
        quick/navigation/tst_navigation.qml
        quick/shells/tst_owner_shell.qml
        quick/shells/tst_visitor_shell.qml
        quick/shells/tst_auth_shell.qml
        quick/auth/tst_auth_runtime.qml
        quick/pages/tst_drive_browser.qml
        quick/pages/tst_transfer_center.qml
        quick/pages/tst_login_page.qml
        quick/pages/tst_register_page.qml
        quick/components/tst_folder_tree_panel.qml
        quick/pages/tst_drive_browser_navigation.qml
        quick/navigation/tst_route_taxonomy.qml
        quick/pages/tst_drive_interactions.qml
        quick/pages/tst_admin_page.qml
        quick/pages/tst_drive_shared_view.qml
        quick/pages/tst_share_create_dialog.qml
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
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickTest
    Qt6::Test
)

desktop_add_advisory_warnings(desktop-quick-tests)

set_target_properties(desktop-quick-tests PROPERTIES
    QUICK_TEST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/quick"
)

# Derive Qt QML import directory from Qt6QuickTest_DIR
# e.g. D:/TOOL/Qt/6.11.0/llvm-mingw_64/lib/cmake/Qt6QuickTest -> D:/TOOL/Qt/6.11.0/llvm-mingw_64/qml
get_filename_component(QT_QML_IMPORT_DIR "${Qt6QuickTest_DIR}/../../../qml" ABSOLUTE)

add_test(
    NAME    desktop-quick-tests
    COMMAND desktop-quick-tests
            -platform offscreen
            -input ${CMAKE_CURRENT_SOURCE_DIR}/quick
            -import ${QT_QML_IMPORT_DIR}
)

# Create a default evidence output directory so that screenshot tests
# work out-of-the-box without requiring the user to export the env var.
set(DESKTOP_QML_EVIDENCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/qml-evidence")
file(MAKE_DIRECTORY "${DESKTOP_QML_EVIDENCE_DIR}")

# Qt 6.11+ blocks local file reads via XMLHttpRequest by default.
# Our quick tests use XHR to read QML source files for contract verification.
if(WIN32)
    set_tests_properties(desktop-quick-tests PROPERTIES
        ENVIRONMENT "QML_XHR_ALLOW_FILE_READ=1;DESKTOP_QML_EVIDENCE_DIR=${DESKTOP_QML_EVIDENCE_DIR}"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}"
    )
else()
    set_tests_properties(desktop-quick-tests PROPERTIES
        ENVIRONMENT "QML_XHR_ALLOW_FILE_READ=1;DESKTOP_QML_EVIDENCE_DIR=${DESKTOP_QML_EVIDENCE_DIR}"
    )
endif()
