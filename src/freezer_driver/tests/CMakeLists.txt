# Add support for GTest.
find_package(ament_cmake_gtest REQUIRED)
find_package(ament_cmake_gmock REQUIRED)

# GMock throws an error if we don't switch off this option in tests.
if(CMAKE_CXX_COMPILER_ID MATCHES "(GNU|Clang)")
    add_compile_options(-Wno-sign-conversion)
endif()

###############################################################################
# test_default_driver

ament_add_gmock(test_default_driver
    mock/mock_framed_serial.hpp
    test_default_driver.cpp
)
target_include_directories(test_default_driver
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_default_driver freezer_driver)

###############################################################################
# test_fake_driver

ament_add_gmock(test_fake_driver
    test_fake_driver.cpp
)
target_link_libraries(test_fake_driver freezer_driver)

###############################################################################
# test_sequence

ament_add_gmock(test_sequence
    test_sequence.cpp
)
target_link_libraries(test_sequence freezer_driver)

###############################################################################
# test_recipes

ament_add_gmock(test_recipes
    test_recipes.cpp
)
target_link_libraries(test_recipes freezer_driver)
