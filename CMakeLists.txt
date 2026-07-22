cmake_minimum_required(VERSION 3.10)
project(ReliefHeightField VERSION 4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 OpenCV
find_package(OpenCV REQUIRED)

add_executable(relief_height_field relief_height_field.cpp)

target_link_libraries(relief_height_field PRIVATE ${OpenCV_LIBS})

# 可选：开启显示窗口
# target_compile_definitions(relief_height_field PRIVATE SHOW_RESULTS)
