cmake_minimum_required(VERSION 3.10)
project(ReliefHeightField VERSION 4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 强制使用 UTF-8 编码（源文件中包含中文注释）
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 使用当前目录下的 opencv
set(OpenCV_DIR ${CMAKE_SOURCE_DIR}/opencv)
set(OpenCV_INCLUDE_DIRS ${OpenCV_DIR}/include)
set(OpenCV_LIB_DIR ${OpenCV_DIR}/lib)

# 使用当前目录下的 libtiff（用于写出带 Photoshop 专色标签的 TIFF）
set(TIFF_DIR ${CMAKE_SOURCE_DIR}/libtiff)
set(TIFF_INCLUDE_DIRS ${TIFF_DIR}/include)
set(TIFF_LIB_DIR ${TIFF_DIR}/lib)

add_executable(relief_height_field relief_height_field_v2.cpp)

target_include_directories(relief_height_field PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${TIFF_INCLUDE_DIRS}
)
target_link_libraries(relief_height_field PRIVATE
    debug ${OpenCV_LIB_DIR}/opencv_world470d.lib
    optimized ${OpenCV_LIB_DIR}/opencv_world470.lib
    ${TIFF_LIB_DIR}/tiff.lib
)

# 复制 tiff.dll 到输出目录
add_custom_command(TARGET relief_height_field POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${TIFF_DIR}/bin/tiff.dll
        $<TARGET_FILE_DIR:relief_height_field>/tiff.dll
)

# 可选：开启显示窗口
# target_compile_definitions(relief_height_field PRIVATE SHOW_RESULTS)
