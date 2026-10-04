# See https://docs.ros.org/en/foxy/How-To-Guides/Ament-CMake-Documentation.html

cmake_minimum_required(VERSION 3.22.1)
project(freezer_node LANGUAGES CXX)

include(GNUInstallDirs)

# Enable output of compile commands during generation.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "(GNU|Clang)")
    add_compile_options(
        -Werror
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wold-style-cast
    )
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THIS_PACKAGE_INCLUDE_DEPENDS
    framed_serial
    freezer_driver
    freezer_msgs
    rclcpp
    rclcpp_action
    std_srvs
)

find_package(ament_cmake REQUIRED)
foreach(dependency IN ITEMS ${THIS_PACKAGE_INCLUDE_DEPENDS})
    find_package(${dependency} REQUIRED)
endforeach()

# The node as a library, so that the tests can create it with a fake driver.
add_library(
    freezer_node_lib
    SHARED
    include/freezer_node/freezer_node.hpp
    src/freezer_node.cpp
)
target_include_directories(
    freezer_node_lib
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(
    freezer_node_lib
    ${THIS_PACKAGE_INCLUDE_DEPENDS}
)

add_executable(freezer_node src/main.cpp)
target_link_libraries(freezer_node freezer_node_lib)

###############################################################################
# INSTALL

install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
    TARGETS freezer_node_lib
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(
    TARGETS freezer_node
    DESTINATION lib/${PROJECT_NAME}
)
install(
    DIRECTORY config launch
    DESTINATION share/${PROJECT_NAME}
)

###############################################################################
# TESTS

include(CTest)
if(BUILD_TESTING)
    find_package(ament_cmake_gmock REQUIRED)
    # GMock throws an error if we don't switch off this option in tests. It
    # must come before ament_add_gmock, which also builds the gmock library.
    add_compile_options(-Wno-sign-conversion)
    ament_add_gmock(test_freezer_node tests/test_freezer_node.cpp)
    target_link_libraries(test_freezer_node freezer_node_lib)
endif()

ament_package()
