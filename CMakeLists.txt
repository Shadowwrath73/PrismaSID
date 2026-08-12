cmake_minimum_required(VERSION 3.16)
project(PrismaSID VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 finden
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Multimedia Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SIDPLAYFP REQUIRED IMPORTED_TARGET libsidplayfp)
pkg_check_modules(RESIDFP REQUIRED IMPORTED_TARGET libresidfp)

set(CMAKE_AUTOMOC ON)

add_executable(prismasid
    src/main.cpp
    src/sidplayer_backend.h
    src/waveform_item.h
)

target_link_libraries(prismasid PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Multimedia Qt6::Concurrent
    PkgConfig::SIDPLAYFP
    PkgConfig::RESIDFP
)

target_include_directories(prismasid PRIVATE src)

# QML-Dateien ins Build-Verzeichnis kopieren
add_custom_command(TARGET prismasid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/qml
        $<TARGET_FILE_DIR:prismasid>/qml
)

install(TARGETS prismasid RUNTIME DESTINATION bin)
install(DIRECTORY qml DESTINATION share/prismasid)
install(DIRECTORY assets DESTINATION share/prismasid)
