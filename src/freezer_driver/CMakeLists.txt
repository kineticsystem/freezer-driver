# See https://docs.ros.org/en/foxy/How-To-Guides/Ament-CMake-Documentation.html

cmake_minimum_required(VERSION 3.22.1)
project(freezer_driver LANGUAGES CXX)

# This module provides installation directories as per the GNU coding standards.
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

# All the dependencies required to build this library. These dependencies
# will also be exported using ament_export_dependencies to help a client
# software find all required transitive dependencies to use this library.
set(THIS_PACKAGE_INCLUDE_DEPENDS
    framed_serial
    rclcpp
)

find_package(ament_cmake REQUIRED)
find_package(framed_serial REQUIRED)
find_package(rclcpp REQUIRED)

# Build freezer_driver library.

# CMake does not need headers listed here, and it is generally recommended not
# to add them to avoid additional maintenance burdens.
# Unfortunately, QtCreator needs them to display them in the class view,
# for easily navigating between class and header files (F4) and for automatic
# code formatting.
# The time spent in the IDE is longer than the time spent maintaining this
# list, so we added the header files, too.
add_library(
    freezer_driver
    SHARED
    include/freezer_driver/default_driver.hpp
    include/freezer_driver/driver.hpp
    include/freezer_driver/fake/fake_driver.hpp
    include/freezer_driver/msgs/info_response.hpp
    include/freezer_driver/msgs/load_sequence_response.hpp
    include/freezer_driver/msgs/response.hpp
    include/freezer_driver/msgs/shoot_response.hpp
    include/freezer_driver/msgs/status_response.hpp
    include/freezer_driver/recipes.hpp
    include/freezer_driver/sequence.hpp
    include/freezer_driver/shot_runner.hpp
    src/default_driver.cpp
    src/fake/fake_driver.cpp
    src/recipes.cpp
    src/sequence.cpp
    src/shot_runner.cpp
)
target_include_directories(
    freezer_driver
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(
    freezer_driver
    ${THIS_PACKAGE_INCLUDE_DEPENDS}
)

###############################################################################
# EXPORTS

# This is necessary to allow this library's clients to use the syntax
# target_link_libraries(client freezer_driver::freezer_driver)
ament_export_targets(
    freezer_driver_targets # Must match the EXPORT label below.
)
# Help downstream packages to find transitive dependencies.
ament_export_dependencies(
    ${THIS_PACKAGE_INCLUDE_DEPENDS}
)
ament_export_include_directories(
    include
)
ament_export_libraries(
    freezer_driver
)

###############################################################################
# INSTALL

install(
    DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}          # include
)

install(
    TARGETS freezer_driver
    EXPORT freezer_driver_targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}      # lib
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}      # lib
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}      # bin
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} # include
)

###############################################################################
# TESTS

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

ament_package()
