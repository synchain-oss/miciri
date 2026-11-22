// SPDX-License-Identifier: GPL-3.0-or-later
//
// 命令行的全部逻辑：解析参数、读文件、选模式、出 JSON。读文件由调用方注入，测试不碰文件系统。
// 退出码：0 成功（含 --help）；2 参数或文件有问题。
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace miciri::tools
{
// 读出整个文件；读不了返回 false
using FileReader = std::function<bool(const std::string& path, std::vector<unsigned char>& bytes)>;

struct CliOutput
{
    int exitCode = 0;
    std::string out; // 给标准输出：JSON
    std::string err; // 给标准错误：用法与错误说明
};

// args 不含程序名
CliOutput runCli(const std::vector<std::string>& args, const FileReader& read);
} // namespace miciri::tools
