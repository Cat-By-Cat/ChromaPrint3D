cmake_minimum_required(VERSION 3.25)

project(ChromaPrint3D VERSION 1.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(CHROMAPRINT3D_BUILD_APPS "Build application executables" ON)
option(CHROMAPRINT3D_BUILD_TESTS "Build unit tests" OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(OpenCV 4.5 REQUIRED COMPONENTS core imgproc imgcodecs)
message(STATUS "Found OpenCV ${OpenCV_VERSION}: ${OpenCV_INSTALL_PATH}")

set(LIB3MF_TESTS OFF CACHE BOOL "Disable lib3mf tests" FORCE)
set(LIB3MF_BUILD_SHARED OFF CACHE BOOL "Build lib3mf shared library" FORCE)
set(USE_INCLUDED_ZLIB ON CACHE BOOL "Use included zlib" FORCE)
set(USE_INCLUDED_LIBZIP ON CACHE BOOL "Use included libzip" FORCE)
set(USE_INCLUDED_SSL ON CACHE BOOL "Use included libressl" FORCE)
add_subdirectory(3dparty/lib3mf)
if(TARGET lib3mf)
    if(MSVC)
        target_compile_options(lib3mf PRIVATE /W0)
    else()
        target_compile_options(lib3mf PRIVATE -w)
    endif()
endif()

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Disable spdlog examples" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable spdlog tests" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "Disable spdlog benchmarks" FORCE)
add_subdirectory(3dparty/spdlog)

option(CHROMAPRINT3D_BUILD_INFER "Build inference engine module" ON)
if(CHROMAPRINT3D_BUILD_INFER)
    add_subdirectory(infer)
endif()

add_subdirectory(core)

if(CHROMAPRINT3D_BUILD_APPS)
    add_subdirectory(apps)
endif()

if(CHROMAPRINT3D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
    if(CHROMAPRINT3D_BUILD_INFER)
        add_subdirectory(infer/tests)
    endif()
endif()

