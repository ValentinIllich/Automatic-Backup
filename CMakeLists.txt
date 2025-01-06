cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME backup)

project(${PROJECT_NAME} VERSION 1.2.5.456 LANGUAGES CXX)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

qt_standard_project_setup()

set(PROJECT_SOURCES

        src/Utilities.h
        src/authexec.cpp

        src/backupConfigData.h
        src/backupDirStruct.cpp
        src/backupDirStruct.h
        src/backupEngine.cpp
        src/backupEngine.h
        src/backupExecuter.cpp
        src/backupExecuter.h
        src/backupMain.cpp
        src/backupMain.h
        src/backupSplash.cpp
        src/backupSplash.h
        src/cleanupDialog.cpp
        src/cleanupDialog.h
        src/crc32.cpp
        src/crc32.h
        src/filterSettings.cpp
        src/filterSettings.h
        src/main.cpp
        src/utilities.cpp

        forms/backupwindow.ui
        forms/backupwindow_.ui
        forms/cleanupDialog.ui
        forms/filterSettings.ui
        forms/mainwindow.ui

        ressources/backup.qrc
)

configure_file( ressources/version.h.in version.h )
include_directories( ${CMAKE_CURRENT_BINARY_DIR} )

if(APPLE)
  set_source_files_properties(ressources/backup.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  set_source_files_properties(ressources/AutomaticBackup.pdf PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  set(MACOSX_BUNDLE_ICON_FILE "backup.icns")
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

  qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES} ressources/backup.icns )
else()
  qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES} ressources/backup.rc )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CMAKE_INCLUDE_CURRENT_DIR ON
    CMAKE_CXX_STANDARD 11
    CMAKE_CXX_STANDARD_REQUIRED ON
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    AUTORCC ON
)

if(CMAKE_BUILD_TYPE STREQUAL DEBUG)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy
                        ${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json
                        ${CMAKE_SOURCE_DIR}/compile_commands.json
    )
endif()

##############################################################################

set(CPACK_PACKAGE_NAME "Automatic Backup")

set(CPACK_PACKAGE_VENDOR "valentins-qtsolutions.de")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Automatic Backup")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Automatic Backup")

install(TARGETS ${PROJECT_NAME}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Destination paths below are relative to ${CMAKE_INSTALL_PREFIX}
#install(TARGETS ${PROJECT_NAME}
#    BUNDLE DESTINATION . COMPONENT Runtime
#    RUNTIME DESTINATION bin COMPONENT Runtime
#    )

qt_generate_deploy_app_script(
  TARGET ${PROJECT_NAME}
  FILENAME_VARIABLE deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR
  )

install(SCRIPT ${deploy_script})

set(CPACK_PACKAGE_FILE_NAME "Automatic-Backup-${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}-Setup")

if(WIN32)
    set(CPACK_GENERATOR NSIS)
    # add/remove icon
    set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_INSTALL_BINDIR}/${PROJECT_NAME}.exe")
    # start menu entry
    set(CPACK_PACKAGE_EXECUTABLES "${PROJECT_NAME}" "Automatic Backup ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH} Setup" )
else()
    set(CPACK_GENERATOR DragNDrop)
endif()

include(CPack)
