cmake_minimum_required(VERSION 4.0)
project(desktop-client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

# Qt 6 Desktop Client
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Test QuickTest)

qt_standard_project_setup()

# ─── Main executable ───────────────────────────────────────────────────────────

qt_add_executable(desktop-client
    src/main.cpp

    src/app/Application.hpp
    src/app/Application.cpp
    src/app/ShellController.hpp
    src/app/ShellController.cpp

    src/network/NetworkClient.hpp
    src/network/NetworkClient.cpp
    src/network/RequestFactory.hpp
    src/network/RequestFactory.cpp
    src/network/ErrorAdapter.hpp
    src/network/ErrorAdapter.cpp

    src/auth/AuthService.hpp
    src/auth/AuthService.cpp
    src/auth/SessionStore.hpp
    src/auth/SessionStore.cpp
    src/auth/OwnerSessionManager.hpp
    src/auth/OwnerSessionManager.cpp
    src/auth/VisitorSessionManager.hpp
    src/auth/VisitorSessionManager.cpp

    src/models/DriveItem.hpp
    src/models/DriveItem.cpp
    src/models/DriveListModel.hpp
    src/models/DriveListModel.cpp
    src/models/FolderTreeModel.hpp
    src/models/FolderTreeModel.cpp
    src/models/UploadTaskModel.hpp
    src/models/UploadTaskModel.cpp
    src/models/DownloadTaskModel.hpp
    src/models/DownloadTaskModel.cpp
    src/models/ShareListModel.hpp
    src/models/ShareListModel.cpp
    src/models/TrashListModel.hpp
    src/models/TrashListModel.cpp
    src/models/BatchResultModel.hpp
    src/models/BatchResultModel.cpp

    src/managers/DriveManager.hpp
    src/managers/DriveManager.cpp
    src/managers/ProfileManager.hpp
    src/managers/ProfileManager.cpp
    src/managers/TransferManager.hpp
    src/managers/TransferManager.cpp
    src/managers/ShareManager.hpp
    src/managers/ShareManager.cpp
    src/managers/TrashManager.hpp
    src/managers/TrashManager.cpp
)

qt_add_qml_module(desktop-client
    URI        DiskDesktop
    VERSION    1.0
    QML_FILES
        qml/Main.qml
        qml/shells/OwnerShell.qml
        qml/shells/VisitorShell.qml
        qml/pages/SplashPage.qml
        qml/pages/LoginPage.qml
        qml/pages/DriveBrowserPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/TransferCenterPage.qml
        qml/pages/ShareManagementPage.qml
        qml/pages/TrashPage.qml
        qml/pages/ShareVerifyPage.qml
        qml/pages/ShareBrowsePage.qml
        qml/components/PageStateView.qml
        qml/components/BreadcrumbBar.qml
        qml/components/FolderTreePanel.qml
    RESOURCE_PREFIX /qt/qml
)

target_link_libraries(desktop-client PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
)

target_include_directories(desktop-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(desktop-client PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE    ON
)

# ─── Tests ─────────────────────────────────────────────────────────────────────

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
