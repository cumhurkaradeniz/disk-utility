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
