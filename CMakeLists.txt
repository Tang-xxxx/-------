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

add_executable(relief_height_field relief_height_field.cpp)

target_include_directories(relief_height_field PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(relief_height_field PRIVATE
    debug ${OpenCV_LIB_DIR}/opencv_world470d.lib
    optimized ${OpenCV_LIB_DIR}/opencv_world470.lib
)

# 可选：开启显示窗口
# target_compile_definitions(relief_height_field PRIVATE SHOW_RESULTS)
