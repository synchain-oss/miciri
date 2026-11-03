// SPDX-License-Identifier: GPL-3.0-or-later
//
// miciri-analyze <文件.wav>：把整个文件当成一次录音，把数值 JSON 打到标准输出。
// 反馈问题时只贴这份 JSON，不要贴音频。退出码：0 成功；2 参数或文件有问题。
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Report.h"
#include "Wav.h"

int main(int argc, char** argv)
{
    if (argc != 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)
    {
        std::fprintf(stderr,
                     "usage: miciri-analyze <file.wav>\n"
                     "Reads a wav file as one recording and prints the level, noise, hum and device-processing\n"
                     "measures as JSON on stdout. Share this JSON when reporting a problem, never the audio.\n"
                     "Meant for recordings of seconds to a few minutes: the hum check keeps one spectrum per\n"
                     "second of audio in memory. snrDb is null when no speech was found.\n");
        return argc == 2 ? 0 : 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
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
    const std::string json = miciri::tools::analyseToJson(wav);
    std::fwrite(json.data(), 1, json.size(), stdout);
    return 0;
}
