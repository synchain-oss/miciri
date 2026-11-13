// SPDX-License-Identifier: GPL-3.0-or-later
#include "Wav.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace miciri::tools
{
namespace
{
std::uint32_t readU32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t readU16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;
// 分析针对 44.1 到 192 kHz 验证；放宽到 8 到 384 kHz，伪造的文件头不能让抽取核长失控
constexpr double kMinSampleRate = 8000.0;
constexpr double kMaxSampleRate = 384000.0;
constexpr std::size_t kExtensibleSubFormatOffset = 24; // fmt 块内：子格式 GUID 的前两个字节就是格式标签
} // namespace

bool parseWav(const std::vector<unsigned char>& bytes, WavData& out, std::string& error)
{
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }
    std::uint16_t format = 0;
    std::uint16_t channelCount = 0;
    std::uint16_t bits = 0;
    const unsigned char* data = nullptr;
    std::size_t dataSize = 0;
    for (std::size_t pos = 12; pos + 8 <= bytes.size();)
    {
        const std::size_t size = readU32(bytes.data() + pos + 4);
        const unsigned char* body = bytes.data() + pos + 8;
        const std::size_t available = bytes.size() - (pos + 8);
        if (std::memcmp(bytes.data() + pos, "fmt ", 4) == 0 && size >= 16 && available >= 16)
        {
            format = readU16(body);
            channelCount = readU16(body + 2);
            out.sampleRate = static_cast<double>(readU32(body + 4));
            bits = readU16(body + 14);
            if (format == kFormatExtensible && size >= kExtensibleSubFormatOffset + 2 && available >= size)
                format = readU16(body + kExtensibleSubFormatOffset);
        }
        else if (std::memcmp(bytes.data() + pos, "data", 4) == 0)
        {
            data = body;
            dataSize = size < available ? size : available; // 文件被截断时读到哪算哪
            break;
        }
        if (size > available)
            break; // 声明的长度超出文件：后面没有可信的块了，也避免 32 位上 pos 回绕
        pos += 8 + size + (size & 1U);
    }
    if (data == nullptr || channelCount == 0 || out.sampleRate <= 0.0)
    {
        error = "missing fmt or data chunk";
        return false;
    }
    if (out.sampleRate < kMinSampleRate || out.sampleRate > kMaxSampleRate)
    {
        error = "unsupported sample rate";
        return false;
    }
    const bool integer = format == kFormatPcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool floating = format == kFormatFloat && (bits == 32 || bits == 64);
    if (!integer && !floating)
    {
        error = "unsupported sample format";
        return false;
    }
    const std::size_t bytesPerSample = bits / 8U;
    const std::size_t frames = dataSize / (bytesPerSample * channelCount);
    out.channels.assign(channelCount, std::vector<double>(frames));
    for (std::size_t f = 0; f < frames; ++f)
    {
        for (std::size_t c = 0; c < channelCount; ++c)
        {
            const unsigned char* p = data + (f * channelCount + c) * bytesPerSample;
            double v = 0.0;
            if (floating && bits == 32)
            {
                const std::uint32_t raw = readU32(p);
                float x;
                std::memcpy(&x, &raw, sizeof x);
                v = static_cast<double>(x);
            }
            else if (floating)
            {
                const std::uint64_t raw =
                    static_cast<std::uint64_t>(readU32(p)) | (static_cast<std::uint64_t>(readU32(p + 4)) << 32);
                std::memcpy(&v, &raw, sizeof v);
            }
            else if (bits == 8)
            {
                v = (static_cast<double>(p[0]) - 128.0) / 128.0;
            }
            else if (bits == 16)
            {
                v = static_cast<double>(static_cast<std::int16_t>(readU16(p))) / 32768.0;
            }
            else if (bits == 24)
            {
                std::int32_t raw = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
                if (raw & 0x800000)
                    raw -= 0x1000000;
                v = static_cast<double>(raw) / 8388608.0;
            }
            else
            {
                v = static_cast<double>(static_cast<std::int32_t>(readU32(p))) / 2147483648.0;
            }
            out.channels[c][f] = v;
        }
    }
    return true;
}

void applyGainDb(WavData& wav, double db)
{
    const double factor = std::pow(10.0, db / 20.0);
    for (auto& channel : wav.channels)
        for (double& v : channel)
            v *= factor;
}
} // namespace miciri::tools
