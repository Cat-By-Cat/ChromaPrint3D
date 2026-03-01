# ── ChromaPrint3D inference engine ─────────────────────────────────────────────

option(CHROMAPRINT3D_INFER_ONNXRUNTIME "Enable ONNX Runtime backend" ON)

add_library(chromaprint3d_infer STATIC)
add_library(ChromaPrint3D::infer ALIAS chromaprint3d_infer)

set_target_properties(chromaprint3d_infer PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME chromaprint3d_infer)

target_compile_features(chromaprint3d_infer PUBLIC cxx_std_20)

# ── Public / private include paths ────────────────────────────────────────────

target_include_directories(chromaprint3d_infer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_include_directories(chromaprint3d_infer PRIVATE src)

# ── Core sources (backend-agnostic) ──────────────────────────────────────────

target_sources(chromaprint3d_infer PRIVATE
    src/data_type.cpp
    src/device.cpp
    src/tensor.cpp
    src/engine.cpp
    src/preprocess.cpp)

# ── Shared dependencies (OpenCV & spdlog from parent project) ────────────────

target_link_libraries(chromaprint3d_infer PUBLIC opencv_core opencv_imgproc)
target_link_libraries(chromaprint3d_infer PUBLIC spdlog::spdlog_header_only)

# Include 3dparty dir for spdlog/nlohmann headers
target_include_directories(chromaprint3d_infer SYSTEM PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/3dparty>)

# ── Compiler flags ────────────────────────────────────────────────────────────

target_compile_options(chromaprint3d_infer PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/O2>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/Zi>
    $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release>>:-O3>
    $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:RelWithDebInfo>>:-O2>
    $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:RelWithDebInfo>>:-g>)

# ── ONNX Runtime backend (optional) ──────────────────────────────────────────

if(CHROMAPRINT3D_INFER_ONNXRUNTIME)
    set(ONNXRUNTIME_VERSION "1.20.1" CACHE STRING "ONNX Runtime version to download")

    # --- Locate or download ONNX Runtime ---
    find_package(onnxruntime ${ONNXRUNTIME_VERSION} QUIET)

    if(onnxruntime_FOUND)
        message(STATUS "Found system ONNX Runtime ${onnxruntime_VERSION}")
    else()
        message(STATUS "Downloading ONNX Runtime ${ONNXRUNTIME_VERSION} pre-built SDK ...")

        include(FetchContent)

        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
                set(_ort_arch "aarch64")
            else()
                set(_ort_arch "x64")
            endif()
            set(_ort_url
                "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-linux-${_ort_arch}-${ONNXRUNTIME_VERSION}.tgz")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(_ort_url
                "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-universal2-${ONNXRUNTIME_VERSION}.tgz")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(_ort_url
                "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip")
        else()
            message(FATAL_ERROR "Unsupported platform for ONNX Runtime auto-download")
        endif()

        FetchContent_Declare(onnxruntime
            URL "${_ort_url}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(onnxruntime)

        set(_ort_root "${onnxruntime_SOURCE_DIR}")

        # Create imported shared-library target
        add_library(onnxruntime SHARED IMPORTED GLOBAL)

        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set_target_properties(onnxruntime PROPERTIES
                IMPORTED_IMPLIB "${_ort_root}/lib/onnxruntime.lib"
                IMPORTED_LOCATION "${_ort_root}/lib/onnxruntime.dll")
        else()
            file(GLOB _ort_so "${_ort_root}/lib/libonnxruntime.so*"
                              "${_ort_root}/lib/libonnxruntime.dylib*")
            list(GET _ort_so 0 _ort_so_path)
            set_target_properties(onnxruntime PROPERTIES
                IMPORTED_LOCATION "${_ort_so_path}"
                IMPORTED_NO_SONAME TRUE)
        endif()

        target_include_directories(onnxruntime INTERFACE "${_ort_root}/include")
    endif()

    target_sources(chromaprint3d_infer PRIVATE
        src/backends/onnxruntime/ort_backend.cpp
        src/backends/onnxruntime/ort_session.cpp)

    target_compile_definitions(chromaprint3d_infer PRIVATE CHROMAPRINT3D_HAS_ONNXRUNTIME=1)
    target_link_libraries(chromaprint3d_infer PRIVATE onnxruntime)
endif()

# ── Install rules ─────────────────────────────────────────────────────────────

include(GNUInstallDirs)

install(TARGETS chromaprint3d_infer
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/chromaprint3d/infer
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/chromaprint3d
    FILES_MATCHING PATTERN "*.h")
