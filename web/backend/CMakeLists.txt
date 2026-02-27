set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Threads REQUIRED)
add_executable(chromaprint3d_server chromaprint3d_server.cpp)
target_include_directories(chromaprint3d_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chromaprint3d_server PRIVATE ChromaPrint3D::core Threads::Threads)
