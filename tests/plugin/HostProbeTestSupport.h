// SPDX-License-Identifier: GPL-3.0-or-later
//
// 宿主探针两个测试共用的小工具：临时目录、环境变量开关、读文件与解析 JSON。
#pragma once

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <juce_core/juce_core.h>

#include "plugin/HostProbe.h"

namespace miciri::probe_test
{
namespace fs = std::filesystem;

// 每个用例自己的临时目录：用 create_directory 新建（已存在就换名重试），只删自己放进去的文件，再删空目录。
class TempDir
{
public:
    TempDir()
    {
        static std::atomic<int> counter{0};
        const fs::path base = fs::temp_directory_path();
        for (int i = 0; i < 1000 && path_.empty(); ++i)
        {
            const fs::path candidate = base / ("miciri-host-probe-" + std::to_string(probe::currentPid()) + "-" +
                                               std::to_string(counter.fetch_add(1)));
            std::error_code ec;
            if (fs::create_directory(candidate, ec) && !ec)
                path_ = candidate;
        }
        REQUIRE_FALSE(path_.empty());
    }
    ~TempDir()
    {
        std::error_code ec;
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(path_, ec))
            files.push_back(entry.path());
        for (const auto& f : files)
            fs::remove(f, ec); // 只有文件；目录项若是子目录，非递归的 remove 也删不掉非空目录
        fs::remove(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }
    std::size_t entryCount() const
    {
        std::error_code ec;
        std::size_t n = 0;
        for (const auto& entry : fs::directory_iterator(path_, ec))
        {
            (void)entry;
            ++n;
        }
        return n;
    }

private:
    fs::path path_;
};

inline void setEnv(const fs::path& value)
{
#if defined(_WIN32)
    _wputenv_s(L"MICIRI_HOST_PROBE_JSON", value.c_str());
#else
    setenv("MICIRI_HOST_PROBE_JSON", value.c_str(), 1);
#endif
}

inline void unsetEnv()
{
#if defined(_WIN32)
    _putenv_s("MICIRI_HOST_PROBE_JSON", ""); // Windows 上把值设成空串即删除该变量
#else
    unsetenv("MICIRI_HOST_PROBE_JSON");
#endif
}

// 用例开始和结束（含失败）时都把环境变量清掉：不被外部已设的变量影响，也不影响后面的用例。
struct EnvCleanup
{
    EnvCleanup() { unsetEnv(); }
    ~EnvCleanup() { unsetEnv(); }
    EnvCleanup(const EnvCleanup&) = delete;
    EnvCleanup& operator=(const EnvCleanup&) = delete;
};

inline std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline juce::var parseJson(const std::string& text)
{
    return juce::JSON::parse(juce::String::fromUTF8(text.c_str(), static_cast<int>(text.size())));
}

inline juce::var parseFile(const fs::path& p)
{
    return parseJson(readFile(p));
}

inline juce::int64 intField(const juce::var& obj, const char* name)
{
    return static_cast<juce::int64>(obj.getProperty(name, juce::var(-1)));
}
} // namespace miciri::probe_test
