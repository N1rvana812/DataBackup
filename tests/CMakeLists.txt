# ============================================================================
# 测试模块 CMake 配置
# ============================================================================
# 使用 FetchContent 自动下载 Google Test，无需手动安装
# ============================================================================

include(FetchContent)

# 下载 Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

# 抑制 gtest 自身的警告
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# ============================================================================
# 测试源文件列表
# ============================================================================

set(TEST_SOURCES
    test_file_filter.cpp
    test_archive_format.cpp
    test_stream_compressor.cpp
)

# 如果 OpenSSL 可用，添加加密相关测试
if(OpenSSL_FOUND)
    list(APPEND TEST_SOURCES
        test_stream_encryptor.cpp
        test_key_derivation.cpp
    )
endif()

# ============================================================================
# 创建测试可执行文件
# ============================================================================

add_executable(databackup_tests ${TEST_SOURCES})

target_link_libraries(databackup_tests PRIVATE
    databackup_lib
    gtest_main
    gtest
)

target_compile_options(databackup_tests PRIVATE -Wall -Wextra -Wpedantic)

# 让 gtest 发现并注册测试
include(GoogleTest)
gtest_discover_tests(databackup_tests
    DISCOVERY_MODE PRE_TEST
)
