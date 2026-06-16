#pragma once
#include <iostream>
#include <string>

inline int g_passes   = 0;
inline int g_failures = 0;

#define CHECK(cond) do { \
    if (cond) { \
        ++g_passes; \
    } else { \
        std::cerr << "FAIL: " #cond "\n"; \
        std::cerr << "  at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failures; \
    } \
} while(0)

#define CHECK_EQ(a, b) do { \
    auto _a = (a); auto _b = (b); \
    if (_a == _b) { \
        ++g_passes; \
    } else { \
        std::cerr << "FAIL: " #a " == " #b "\n"; \
        std::cerr << "  left:  " << _a << "\n"; \
        std::cerr << "  right: " << _b << "\n"; \
        std::cerr << "  at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failures; \
    } \
} while(0)

#define TEST(name) void name()

#define RUN(name) do { \
    std::cout << "  " #name "\n"; \
    name(); \
} while(0)

#define TEST_RESULTS() do { \
    std::cout << g_passes << " passed, " << g_failures << " failed\n"; \
    return g_failures > 0 ? 1 : 0; \
} while(0)
