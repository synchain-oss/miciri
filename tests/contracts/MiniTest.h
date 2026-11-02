// SPDX-License-Identifier: GPL-3.0-or-later
//
// 最小的测试工具：CHECK 失败只记数并打印位置，测试函数跑完后由 main 用 TEST_EXIT 汇总成退出码。
// CHECK 走函数调用而不是 if (!(cond))：条件是编译期常量时不会触发“条件表达式是常量”告警。
//
// 其他测试目录可以直接 #include "contracts/MiniTest.h"。
#pragma once

#include <cstdio>

namespace miciri::test
{
inline int& failures()
{
    static int count = 0;
    return count;
}

inline void check(bool ok, const char* expr, const char* file, int line)
{
    if (ok)
        return;
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
    ++failures();
}
} // namespace miciri::test

#define CHECK(cond) ::miciri::test::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

#define TEST_EXIT() return ::miciri::test::failures() == 0 ? 0 : 1
