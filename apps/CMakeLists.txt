set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(gen_calibration_board gen_calibration_board.cpp)
add_executable(raster_to_3mf raster_to_3mf.cpp)
add_executable(build_colordb build_colordb.cpp)
add_executable(gen_stage gen_stage.cpp)
add_executable(gen_representative_board gen_representative_board.cpp)
add_executable(svg_to_3mf svg_to_3mf.cpp)
add_executable(raster_to_svg raster_to_svg.cpp)

target_link_libraries(gen_calibration_board PRIVATE ChromaPrint3D::core)
target_link_libraries(raster_to_3mf PRIVATE ChromaPrint3D::core)
target_link_libraries(build_colordb PRIVATE ChromaPrint3D::core)
target_link_libraries(gen_stage PRIVATE ChromaPrint3D::core)
target_link_libraries(gen_representative_board PRIVATE ChromaPrint3D::core)
target_link_libraries(svg_to_3mf PRIVATE ChromaPrint3D::core)
target_link_libraries(raster_to_svg PRIVATE ChromaPrint3D::core)

find_package(Threads REQUIRED)
add_executable(chromaprint3d_server chromaprint3d_server.cpp)
target_include_directories(chromaprint3d_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chromaprint3d_server PRIVATE ChromaPrint3D::core Threads::Threads)
# CHROMAPRINT3D_HAS_INFER and infer headers are propagated via core's PUBLIC link
