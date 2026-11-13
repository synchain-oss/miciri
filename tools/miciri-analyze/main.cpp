// SPDX-License-Identifier: GPL-3.0-or-later
//
// miciri-analyze [--gain-db <dB>] [--floor-dbfs <dB>] <文件.wav>：把整个文件当成一次录音，把数值 JSON 打到标准输出。
// --gain-db 先给所有样本加这么多 dB 再分析（补回导出时的衰减）；--floor-dbfs 是接通步会读到的底噪，
// 像插件那样放宽模拟平顶的容差（不给就只用相对容差）。
// 反馈问题时只贴这份 JSON，不要贴音频。退出码：0 成功；2 参数或文件有问题。
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "Report.h"
#include "Wav.h"

int main(int argc, char** argv)
{
    double gainDb = 0.0;
    std::optional<double> floorDbfs;
    int fileArg = 1;
    while (fileArg + 2 < argc)
    {
        const bool gain = std::strcmp(argv[fileArg], "--gain-db") == 0;
        if (!gain && std::strcmp(argv[fileArg], "--floor-dbfs") != 0)
            break;
        char* end = nullptr;
        const double v = std::strtod(argv[fileArg + 1], &end);
        if (end == argv[fileArg + 1] || *end != '\0' || !std::isfinite(v))
        {
            std::fprintf(stderr, "miciri-analyze: %s needs a number\n", argv[fileArg]);
            return 2;
        }
        if (gain)
            gainDb = v;
        else
            floorDbfs = v;
        fileArg += 2;
    }
    if (argc != fileArg + 1 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)
    {
        std::fprintf(stderr,
                     "usage: miciri-analyze [--gain-db <dB>] [--floor-dbfs <dB>] <file.wav>\n"
                     "Reads a wav file as one recording and prints the level, noise, hum and device-processing\n"
                     "measures as JSON on stdout. Share this JSON when reporting a problem, never the audio.\n"
                     "Meant for recordings of seconds to a few minutes: the hum check keeps one spectrum per\n"
                     "second of audio in memory. snrDb is null when no speech was found.\n"
                     "--gain-db adds that many dB to every sample first, e.g. 3.01 to undo a -3 dB pan law\n"
                     "applied when a mono track was exported as two identical channels.\n"
                     "--floor-dbfs is the noise floor (RMS, dBFS) a connect step would have read; like the plugin,\n"
                     "it widens the flat-top tolerance. Without it the flat-top check uses the relative tolerance.\n");
        return argc == 2 ? 0 : 2;
    }
    std::ifstream in(argv[fileArg], std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "miciri-analyze: cannot open the input file\n");
        return 2;
    }
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    miciri::tools::WavData wav;
    std::string error;
    if (!miciri::tools::parseWav(bytes, wav, error) || wav.channels.empty() || wav.channels[0].empty())
    {
        std::fprintf(stderr, "miciri-analyze: %s\n", error.empty() ? "no audio frames" : error.c_str());
        return 2;
    }
    miciri::tools::applyGainDb(wav, gainDb);
    const std::string json = miciri::tools::analyseToJson(wav, floorDbfs);
    std::fwrite(json.data(), 1, json.size(), stdout);
    return 0;
}
