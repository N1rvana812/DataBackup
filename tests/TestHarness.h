#pragma once

#include <functional>
#include <iostream>
#include <string>

inline bool runTest(const std::string& name, const std::function<bool()>& callback) {
    std::cout << "[ RUN      ] " << name << std::endl;
    bool passed = callback();
    std::cout << "[       " << (passed ? "OK" : "FAILED") << "] " << name << std::endl;
    return passed;
}

#define EXPECT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "EXPECT_TRUE failed: " #expr << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

#define EXPECT_FALSE(expr) \
    do { \
        if (expr) { \
            std::cerr << "EXPECT_FALSE failed: " #expr << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

#define EXPECT_EQ(lhs, rhs) \
    do { \
        if (!((lhs) == (rhs))) { \
            std::cerr << "EXPECT_EQ failed: " #lhs " == " #rhs << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)
