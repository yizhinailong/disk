cmake_minimum_required(VERSION 3.16)

project(DiskDesktop VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Quick QuickControls2 Test QuickTest)

qt_standard_project_setup(REQUIRES 6.8)

# ---------------------------------------------------------------------------
# Advisory warning helper (desktop-only, target-scoped, never -Werror)
# ---------------------------------------------------------------------------
include(CheckCXXCompilerFlag)

# Internal: append to a per-target flag list if the compiler accepts the flag.
# Usage: _desktop_check_flag(<flag> <out-list-var>)
function(_desktop_check_flag FLAG OUT_LIST_VAR)
  string(MAKE_C_IDENTIFIER "has_flag_${FLAG}" _varname)
  check_cxx_compiler_flag("${FLAG}" ${_varname})
  if(${_varname})
    list(APPEND ${OUT_LIST_VAR} "${FLAG}")
    set(${OUT_LIST_VAR} ${${OUT_LIST_VAR}} PARENT_SCOPE)
  endif()
endfunction()

# desktop_add_advisory_warnings(<target>)
# Adds advisory (non-fatal) warning flags appropriate for the current compiler.
# Override-specific flags are feature-gated via CheckCXXCompilerFlag.
function(desktop_add_advisory_warnings TARGET)
  set(_flags "")

  # ── GCC / Clang baseline ──────────────────────────────────────────────
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    list(APPEND _flags
      -Wall
      -Wextra
      -Wpedantic
    )
    # Optional: -Winconsistent-missing-override (Clang / AppleClang only)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
      _desktop_check_flag("-Winconsistent-missing-override" _flags)
    endif()
    # Optional: -Wsuggest-override (GCC only)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      _desktop_check_flag("-Wsuggest-override" _flags)
    endif()
  endif()

  # ── MSVC baseline ─────────────────────────────────────────────────────
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    list(APPEND _flags
      /W4
    )
  endif()

  if(_flags)
    target_compile_options("${TARGET}" PRIVATE ${_flags})
  endif()
endfunction()

set(DESKTOP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(DESKTOP_APP_SOURCES
    ${DESKTOP_SRC_DIR}/main.cpp
    ${DESKTOP_SRC_DIR}/app/Application.cpp
    ${DESKTOP_SRC_DIR}/app/ShellController.cpp
    ${DESKTOP_SRC_DIR}/auth/AuthService.cpp
    ${DESKTOP_SRC_DIR}/auth/OwnerSessionManager.cpp
    ${DESKTOP_SRC_DIR}/auth/SessionStore.cpp
    ${DESKTOP_SRC_DIR}/auth/VisitorSessionManager.cpp
    ${DESKTOP_SRC_DIR}/managers/DriveManager.cpp
    ${DESKTOP_SRC_DIR}/managers/ProfileManager.cpp
    ${DESKTOP_SRC_DIR}/managers/ShareManager.cpp
    ${DESKTOP_SRC_DIR}/managers/TransferManager.cpp
    ${DESKTOP_SRC_DIR}/managers/TrashManager.cpp
    ${DESKTOP_SRC_DIR}/models/BatchResultModel.cpp
    ${DESKTOP_SRC_DIR}/models/DownloadTaskModel.cpp
    ${DESKTOP_SRC_DIR}/models/DriveItem.cpp
    ${DESKTOP_SRC_DIR}/models/DriveListModel.cpp
    ${DESKTOP_SRC_DIR}/models/FolderTreeModel.cpp
    ${DESKTOP_SRC_DIR}/models/ShareListModel.cpp
    ${DESKTOP_SRC_DIR}/models/TrashListModel.cpp
    ${DESKTOP_SRC_DIR}/models/UploadTaskModel.cpp
    ${DESKTOP_SRC_DIR}/network/ErrorAdapter.cpp
    ${DESKTOP_SRC_DIR}/network/NetworkClient.cpp
    ${DESKTOP_SRC_DIR}/network/RequestFactory.cpp
)

set(DESKTOP_APP_HEADERS
    ${DESKTOP_SRC_DIR}/app/Application.hpp
    ${DESKTOP_SRC_DIR}/app/ShellController.hpp
    ${DESKTOP_SRC_DIR}/auth/AuthService.hpp
    ${DESKTOP_SRC_DIR}/auth/OwnerSessionManager.hpp
    ${DESKTOP_SRC_DIR}/auth/SessionStore.hpp
    ${DESKTOP_SRC_DIR}/auth/VisitorSessionManager.hpp
    ${DESKTOP_SRC_DIR}/managers/DriveManager.hpp
    ${DESKTOP_SRC_DIR}/managers/ProfileManager.hpp
    ${DESKTOP_SRC_DIR}/managers/ShareManager.hpp
    ${DESKTOP_SRC_DIR}/managers/TransferManager.hpp
    ${DESKTOP_SRC_DIR}/managers/TrashManager.hpp
    ${DESKTOP_SRC_DIR}/models/BatchResultModel.hpp
    ${DESKTOP_SRC_DIR}/models/DownloadTaskModel.hpp
    ${DESKTOP_SRC_DIR}/models/DriveItem.hpp
    ${DESKTOP_SRC_DIR}/models/DriveListModel.hpp
    ${DESKTOP_SRC_DIR}/models/FolderTreeModel.hpp
    ${DESKTOP_SRC_DIR}/models/ShareListModel.hpp
    ${DESKTOP_SRC_DIR}/models/TrashListModel.hpp
    ${DESKTOP_SRC_DIR}/models/UploadTaskModel.hpp
    ${DESKTOP_SRC_DIR}/network/ErrorAdapter.hpp
    ${DESKTOP_SRC_DIR}/network/NetworkClient.hpp
    ${DESKTOP_SRC_DIR}/network/RequestFactory.hpp
)

set(DESKTOP_QML_FILES
    qml/Main.qml
    qml/components/BreadcrumbBar.qml
    qml/components/FolderTreePanel.qml
    qml/components/FormatUtils.js
    qml/components/OwnerSidebarNavButton.qml
    qml/components/OwnerSidebarSection.qml
    qml/components/PageStateView.qml
    qml/components/WorkspaceTheme.qml
    qml/components/drive/DriveMyFilesView.qml
    qml/components/drive/DriveContextMenu.qml
    qml/components/drive/DriveSharedView.qml
    qml/components/drive/DriveStatusCard.qml
    qml/components/drive/DriveToolbarCard.qml
    qml/components/drive/DriveTrashView.qml
    qml/components/auth/AuthCard.qml
    qml/components/auth/AuthTheme.qml
    qml/components/owner/OwnerAccountCard.qml
    qml/components/owner/OwnerStorageCard.qml
    qml/pages/DriveBrowserPage.qml
    qml/pages/LoginPage.qml
    qml/pages/RegisterPage.qml
    qml/pages/SettingsPage.qml
    qml/pages/ShareBrowsePage.qml
    qml/pages/ShareVerifyPage.qml
    qml/pages/SplashPage.qml
    qml/pages/TransferCenterPage.qml
    qml/shells/AuthShell.qml
    qml/shells/OwnerShell.qml
    qml/shells/VisitorShell.qml
)

qt_add_executable(disk-desktop
    ${DESKTOP_APP_SOURCES}
    ${DESKTOP_APP_HEADERS}
)

target_include_directories(disk-desktop PRIVATE
    ${DESKTOP_SRC_DIR}
)

qt_add_qml_module(disk-desktop
    URI DiskDesktop
    VERSION 1.0
    QML_FILES
    ${DESKTOP_QML_FILES}
    RESOURCE_PREFIX /qt/qml
)

set_target_properties(disk-desktop PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(disk-desktop PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Quick
    Qt6::QuickControls2
)

include(GNUInstallDirs)
install(TARGETS disk-desktop
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Advisory warnings for the desktop app target (PRIVATE scope, never -Werror)
desktop_add_advisory_warnings(disk-desktop)

enable_testing()
add_subdirectory(tests)
