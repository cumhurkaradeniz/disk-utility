# SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
# SPDX-License-Identifier: BSD-3-Clause

find_package(GTest REQUIRED)
include(GoogleTest)

# UDisks2 built in code, for every test: no bus is reached.
add_library(disk-utility-fake STATIC fake/fake_udisks.cpp)
target_include_directories(disk-utility-fake PUBLIC fake)
target_link_libraries(disk-utility-fake PUBLIC disk-utility-core)

# The model, the operations and the actions alone: no window can come in.
add_executable(disk-utility-logic-tests
    logic/main.cpp
    logic/actions_test.cpp
    logic/copy_test.cpp
    logic/describe_test.cpp
    logic/erase_plan_test.cpp
    logic/identity_test.cpp
    logic/images_test.cpp
    logic/model_test.cpp
    logic/operations_test.cpp
    logic/partition_plan_test.cpp
    logic/system_test.cpp
    logic/transfer_test.cpp
    logic/tree_test.cpp
    logic/volume_format_test.cpp
    logic/volumes_test.cpp
)
target_link_libraries(disk-utility-logic-tests PRIVATE disk-utility-fake GTest::gtest)
gtest_discover_tests(disk-utility-logic-tests PROPERTIES LABELS logic)

add_executable(disk-utility-scenario-tests
    scenarios/main.cpp
    scenarios/erase_test.cpp
    scenarios/safety_test.cpp
    scenarios/images_test.cpp
    scenarios/partition_test.cpp
    scenarios/window_test.cpp
    scenarios/sidebar_test.cpp
    scenarios/detail_test.cpp
    scenarios/volumetools_test.cpp
)
target_link_libraries(disk-utility-scenario-tests PRIVATE disk-utility-ui disk-utility-fake GTest::gtest)
# The application tests group every running copy of these binaries.
gtest_discover_tests(disk-utility-scenario-tests PROPERTIES LABELS scenario RESOURCE_LOCK disk-utility-ui)

# The insets and gaps of the window's own views, measured headless.
add_executable(disk-utility-layout-tests
    scenarios/main.cpp
    layout/layout_test.cpp
    layout/scroll_test.cpp
)
target_include_directories(disk-utility-layout-tests PRIVATE scenarios)
target_link_libraries(disk-utility-layout-tests PRIVATE disk-utility-ui disk-utility-fake GTest::gtest)
gtest_discover_tests(disk-utility-layout-tests PROPERTIES LABELS layout RESOURCE_LOCK disk-utility-ui)
