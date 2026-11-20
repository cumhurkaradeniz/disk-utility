# SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.16)

project(cutefish-disk-utility VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)

find_package(CutefishToolkit REQUIRED)

add_library(disk-utility-core STATIC
    src/actions.cpp
    src/application.cpp
    src/dialogs/volume_format.cpp
    src/images/formats.cpp
    src/images/imagefile.cpp
    src/images/size.cpp
    src/udisks/bus.cpp
    src/udisks/describe.cpp
    src/udisks/errors.cpp
    src/udisks/holders.cpp
    src/udisks/identity.cpp
    src/udisks/model.cpp
    src/udisks/operations.cpp
    src/udisks/parse.cpp
    src/udisks/properties.cpp
    src/udisks/smart.cpp
    src/udisks/system.cpp
    src/udisks/tree.cpp
)
target_include_directories(disk-utility-core PUBLIC src)
target_link_libraries(disk-utility-core PUBLIC Cutefish::ToolkitBase)

cutefish_add_translations(cutefish-disk-utility SOURCES src DESKTOP_FILES cutefish-disk-utility.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cutefish-disk-utility.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
