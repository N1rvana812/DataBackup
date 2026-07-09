cmake_minimum_required(VERSION 3.14)
project(DataBackup VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 自动收集 src 目录下的所有 .cpp 文件
file(GLOB_RECURSE SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp")

# 生成可执行文件
add_executable(databackup ${SRC_FILES})

# 开启严格警告
target_compile_options(databackup PRIVATE -Wall -Wextra -Wpedantic)