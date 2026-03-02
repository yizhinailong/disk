cmake_minimum_required(VERSION 3.16)

project(diskqml VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Network Test)

qt_standard_project_setup(REQUIRES 6.8)

# --- diskqml_core static library (Wave 1: auth/api/storage/utils) ---
add_library(diskqml_core STATIC
    src/api/ApiClient.cpp
    src/api/AuthApi.cpp
    src/services/AuthService.cpp
    src/storage/TokenStore.cpp
    src/utils/ConfigStore.cpp
    src/viewmodels/LoginViewModel.cpp
    src/viewmodels/RegisterViewModel.cpp
    src/app/AppContext.cpp
)

target_include_directories(diskqml_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(diskqml_core
    PUBLIC Qt6::Core Qt6::Network
)

# --- Main executable ---
qt_add_executable(appdiskqml
    main.cpp
)

qt_add_qml_module(appdiskqml
    URI diskqml
    QML_FILES
        qml/Main.qml
        qml/App.qml
        qml/views/LoginView.qml
        qml/views/RegisterView.qml
        qml/views/HomePlaceholder.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appdiskqml PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appdiskqml
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appdiskqml
    PRIVATE diskqml_core Qt6::Quick Qt6::QuickControls2
)

include(GNUInstallDirs)
install(TARGETS appdiskqml
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# --- Testing infrastructure ---
enable_testing()

# Helper macro: add_executable + add_test + link
macro(diskqml_add_test TARGET SOURCE)
    add_executable(${TARGET} ${SOURCE})
    target_link_libraries(${TARGET} PRIVATE diskqml_core Qt6::Test)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
endmacro()

diskqml_add_test(tst_diskqml_errorcode   tests/cpp/tst_ErrorCode.cpp)
diskqml_add_test(tst_diskqml_tokenstore  tests/cpp/tst_TokenStore.cpp)
diskqml_add_test(tst_diskqml_authdtos    tests/cpp/tst_AuthDtos.cpp)
diskqml_add_test(tst_diskqml_authservice tests/cpp/tst_AuthService.cpp)
diskqml_add_test(tst_diskqml_viewmodels  tests/cpp/tst_ViewModels.cpp)

# Static enforcement: QML must not contain business logic
add_test(NAME diskqml-no-qml-logic
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/verify_no_qml_logic.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
