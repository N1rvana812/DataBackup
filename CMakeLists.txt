cmake_minimum_required(VERSION 3.14)
project(DataBackup VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/tests)

# 自动收集 src 目录下的所有 .cpp 文件
file(GLOB_RECURSE SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp")

# 生成可执行文件
add_executable(databackup ${SRC_FILES})

# 测试执行文件
add_executable(monitor_tests
    tests/MonitorTests.cpp
    src/monitor/Monitor.cpp
)

if (MSVC)
    target_compile_options(databackup PRIVATE /W4)
    target_compile_options(monitor_tests PRIVATE /W4)
else()
    target_compile_options(databackup PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(monitor_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME MonitorTests COMMAND monitor_tests)