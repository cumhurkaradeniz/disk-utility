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
    src/udisks/parse.cpp
)
target_include_directories(disk-utility-core PUBLIC src)
target_link_libraries(disk-utility-core PUBLIC Cutefish::ToolkitBase)

cutefish_add_translations(cutefish-disk-utility SOURCES src DESKTOP_FILES cutefish-disk-utility.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cutefish-disk-utility.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
