// SPDX-License-Identifier: GPL-3.0-or-later
//
// miciri-analyze [--mode <名>] [--gain-db <dB>] [--floor-dbfs <dB>] <文件.wav>：把整个文件当成一次录音，
// 把数值 JSON 打到标准输出。参数与各模式的说明见 --help（Cli.cpp）。
// 反馈问题时只贴这份 JSON，不要贴音频。退出码：0 成功；2 参数或文件有问题。
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Cli.h"

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    const miciri::tools::CliOutput result =
        miciri::tools::runCli(args, [](const std::string& path, std::vector<unsigned char>& bytes) {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        });
    std::fwrite(result.out.data(), 1, result.out.size(), stdout);
    std::fwrite(result.err.data(), 1, result.err.size(), stderr);
    return result.exitCode;
}
