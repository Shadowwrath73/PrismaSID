cmake_minimum_required(VERSION 3.16)
project(PrismaSID VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 finden
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Multimedia Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SIDPLAYFP REQUIRED IMPORTED_TARGET libsidplayfp)

# libresidfp ist seit libsidplayfp 3.x im Builder eingebettet (kein separates Paket nötig).
# Falls es als eigenes Paket existiert (ältere 2.x-Setups), wird es mitgelinkt.
pkg_check_modules(RESIDFP QUIET IMPORTED_TARGET libresidfp)

# WICHTIG (Windows/MinGW): pkg-config zeigt auf libsidplayfp.dll.a (Import-Lib),
# die die residfp-Builder-Symbole NICHT enthält → Link-Fehler "undefined reference
# to ReSIDfpBuilder::ReSIDfpBuilder". Die STATISCHE .a hat alle Builder-Symbole.
set(SIDPLAYFP_LIB "${SIDPLAYFP_LIBDIR}/libsidplayfp.a")
if(NOT EXISTS "${SIDPLAYFP_LIB}")
    set(SIDPLAYFP_LIB PkgConfig::SIDPLAYFP)
endif()

set(CMAKE_AUTOMOC ON)

# Windows: Icon-Resource (windres) — nötig fürs Desktop-Icon; CMake findet windres
# auf MinGW manchmal nicht automatisch
if(WIN32)
    find_program(WINDRES_EXECUTABLE windres)
    if(WINDRES_EXECUTABLE)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/prismasid_res.o
            COMMAND ${WINDRES_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src/prismasid.rc
                    -O coff -o ${CMAKE_CURRENT_BINARY_DIR}/prismasid_res.o
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/prismasid.rc
                    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sidplayer.ico
            COMMENT "windres: PrismaSID-Icon"
        )
    endif()
endif()

add_executable(prismasid
    src/main.cpp
    src/sidplayer_backend.h
    src/waveform_item.h
    ${CMAKE_CURRENT_BINARY_DIR}/prismasid_res.o
)

target_link_libraries(prismasid PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Multimedia Qt6::Concurrent
    ${SIDPLAYFP_LIB}
)

# Windows: GUI-Subsystem statt Konsole (kein schwarzes Konsolenfenster beim Start!)
if(WIN32)
    target_link_options(prismasid PRIVATE -mwindows)
endif()

if(RESIDFP_FOUND)
    target_link_libraries(prismasid PRIVATE PkgConfig::RESIDFP)
endif()

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
