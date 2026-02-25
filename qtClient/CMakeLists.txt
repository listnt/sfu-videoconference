cmake_minimum_required(VERSION 3.16)

project(qtClient VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Quick Multimedia)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appqtClient
    main.cpp
)

qt_add_qml_module(appqtClient
    URI qtClient
    VERSION 1.0
    QML_FILES
        Main.qml
        SOURCES conferenceclient.h conferenceclient.cpp
        SOURCES videoplayer.h videoplayer.cpp
        QML_FILES SmallVideoRect.qml
        SOURCES
        SOURCES utils.h utils.cpp
        SOURCES constants.h
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appqtClient PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appqtClient
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

add_subdirectory(third_party/libdatachannel)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)

target_link_libraries(appqtClient
    PRIVATE Qt6::Quick
    Qt6::Multimedia
    datachannel
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
)

target_include_directories(appqtClient PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libdatachannel/include"
)

target_compile_definitions(appqtClient PRIVATE WEBRTC_POSIX WEBRTC_LINUX)


include(GNUInstallDirs)
install(TARGETS appqtClient
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
