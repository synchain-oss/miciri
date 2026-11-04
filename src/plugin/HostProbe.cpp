// SPDX-License-Identifier: GPL-3.0-or-later
//
// 宿主探针的实现（只在测试构建里编进插件）。不依赖 JUCE。
// 环境变量名只在这个文件里出现：发布构建不编这个文件，二进制里也就没有这个字符串。
#include "HostProbe.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace miciri::probe
{
namespace
{
// 进程内所有写文件的动作共用一把锁：定时器、prepareToPlay、releaseResources、析构可能落在不同线程，
// 也可能是同一进程里的不同实例；串行化之后临时文件不会被两个写者同时占用。
// 只在消息线程与宿主的准备线程上用，音频线程永远不碰。
std::mutex& ioMutex()
{
    static std::mutex m;
    return m;
}
} // namespace

long long currentPid() noexcept
{
#if defined(_WIN32)
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(getpid());
#endif
}

std::string toJson(const Snapshot& s, long long pid)
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(15);
    const double rate = std::isfinite(s.sampleRate) ? s.sampleRate : 0.0;
    o << "{\n"
      << "  \"schema\": " << kSchemaVersion << ",\n"
      << "  \"pid\": " << pid << ",\n"
      << "  \"sampleRate\": " << rate << ",\n"
      << "  \"maxBlockSize\": " << s.maxBlockSize << ",\n"
      << "  \"numInputChannels\": " << s.numInputChannels << ",\n"
      << "  \"numOutputChannels\": " << s.numOutputChannels << ",\n"
      << "  \"prepareCount\": " << s.prepareCount << ",\n"
      << "  \"blocksTotal\": " << s.blocksTotal << ",\n"
      << "  \"blocksNonRealtime\": " << s.blocksNonRealtime << ",\n"
      << "  \"blocksRealtime\": " << s.blocksRealtime << ",\n"
      << "  \"blocksAllZero\": " << s.blocksAllZero << ",\n"
      << "  \"blocksNonZero\": " << s.blocksNonZero << ",\n"
      << "  \"editorOpens\": " << s.editorOpens << ",\n"
      << "  \"editorCloses\": " << s.editorCloses << ",\n"
      << "  \"analysisCount\": " << s.analysisCount << ",\n"
      << "  \"lastIsNonRealtime\": " << (s.lastIsNonRealtime ? "true" : "false") << "\n"
      << "}\n";
    return o.str();
}

bool writeFileAtomic(const std::filesystem::path& target, const std::string& content) noexcept
{
    try
    {
        namespace fs = std::filesystem;
        // 临时文件与目标同目录：改名不跨卷，才是原子的。名字带 pid，不同进程写同一个目标时互不踩。
        fs::path tmp = target;
        tmp += "." + std::to_string(currentPid()) + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            out.flush();
            if (!out)
            {
                out.close();
                std::error_code ignore;
                fs::remove(tmp, ignore);
                return false;
            }
        }
        std::error_code ec;
        fs::rename(tmp, target, ec);
        if (ec)
        {
            std::error_code ignore;
            fs::remove(tmp, ignore);
            return false;
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

Reporter::Reporter(std::filesystem::path target, const Counters& counters)
    : target_(std::move(target)), counters_(counters)
{
}

std::optional<std::filesystem::path> Reporter::pathFromEnvironment()
{
#if defined(_WIN32)
    // 宽字符版本：路径里有非 ASCII 字符时，窄字符版本会按系统代码页读成乱码。
    wchar_t* raw = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&raw, &len, L"MICIRI_HOST_PROBE_JSON") != 0 || raw == nullptr)
        return std::nullopt;
    std::unique_ptr<wchar_t, decltype(&std::free)> owner(raw, &std::free);
    if (raw[0] == L'\0')
        return std::nullopt;
    return std::filesystem::path(std::wstring(raw));
#else
    const char* raw = std::getenv("MICIRI_HOST_PROBE_JSON");
    if (raw == nullptr || raw[0] == '\0')
        return std::nullopt;
    return std::filesystem::path(raw);
#endif
}

std::unique_ptr<Reporter> Reporter::fromEnvironment(const Counters& counters) noexcept
{
    try
    {
        auto path = pathFromEnvironment();
        if (!path)
            return nullptr;
        return std::make_unique<Reporter>(std::move(*path), counters);
    }
    catch (...)
    {
        return nullptr;
    }
}

bool Reporter::write(bool onlyIfChanged) noexcept
{
    try
    {
        const std::lock_guard<std::mutex> lock(ioMutex());
        std::string json = toJson(counters_.snapshot(), currentPid());
        if (onlyIfChanged && json == last_)
            return false;
        if (!writeFileAtomic(target_, json))
            return false;
        last_ = std::move(json);
        writes_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool Reporter::tick() noexcept
{
    return write(true);
}

bool Reporter::flush() noexcept
{
    return write(false);
}
} // namespace miciri::probe
