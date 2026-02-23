# GoogleTest is already fetched by core/tests — reuse if available,
# otherwise fetch it ourselves.
if(NOT TARGET GTest::gtest_main)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(googletest)
endif()

set(TEST_SOURCES
    test_tensor.cpp
    test_preprocess.cpp)

add_executable(chromaprint3d_infer_tests ${TEST_SOURCES})
target_link_libraries(chromaprint3d_infer_tests PRIVATE
    ChromaPrint3D::infer GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(chromaprint3d_infer_tests)
