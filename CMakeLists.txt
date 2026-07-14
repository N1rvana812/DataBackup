cmake_minimum_required(VERSION 3.14)
project(DataBackup VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 查找依赖库
find_package(ZLIB REQUIRED)
find_package(OpenSSL QUIET)

# 配置加密支持
if(OpenSSL_FOUND)
    message(STATUS "OpenSSL found — encryption support enabled")
    add_compile_definitions(HAS_OPENSSL)
    set(CRYPTO_LIBS OpenSSL::Crypto)
else()
    message(WARNING "OpenSSL not found — encryption support disabled. "
                    "Install libssl-dev for AES encryption support.")
    set(CRYPTO_LIBS "")
endif()

# 收集 src 目录下的源文件（排除 main.cpp，它作为可执行文件入口单独处理）
file(GLOB_RECURSE LIB_SRC_FILES "${PROJECT_SOURCE_DIR}/src/*.cpp")
list(FILTER LIB_SRC_FILES EXCLUDE REGEX "main\\.cpp$")

# 构建静态库，供可执行文件和测试共用
add_library(databackup_lib STATIC ${LIB_SRC_FILES})
target_link_libraries(databackup_lib PUBLIC ZLIB::ZLIB ${CRYPTO_LIBS})
target_compile_options(databackup_lib PRIVATE -Wall -Wextra -Wpedantic)

# 生成可执行文件
add_executable(databackup "${PROJECT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(databackup PRIVATE databackup_lib)

# 开启严格警告
target_compile_options(databackup PRIVATE -Wall -Wextra -Wpedantic)

# 启用测试
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
