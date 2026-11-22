// SPDX-License-Identifier: GPL-3.0-or-later
#include "Cli.h"

#include <cmath>
#include <cstdlib>
#include <optional>

#include "Modes.h"
#include "Report.h"
#include "Wav.h"

namespace miciri::tools
{
namespace
{
std::string usage()
{
    std::string names;
    for (const auto& entry : kModes)
        names += (names.empty() ? "" : "|") + std::string(entry.first);
    return "usage: miciri-analyze [--mode <" + names +
           ">] [--gain-db <dB>] [--floor-dbfs <dB>] <file.wav>\n"
           "Reads a wav file as one recording and prints numbers (measures, verdicts, counts) as JSON on stdout.\n"
           "Share this JSON when reporting a problem, never the audio. Every mode prints numbers only.\n"
           "--mode picks what the recording is analysed as (default level):\n"
           "  level      the whole file as one recording: level, noise, hum and device-processing measures.\n"
           "             Meant for recordings of seconds to a few minutes: the hum check keeps one spectrum per\n"
           "             second of audio in memory. snrDb is null when no speech was found.\n"
           "  room       claps recorded in the room: reverb time (T20) per band, Tmid, how many claps stayed valid\n"
           "             per band, gating, and every clap's per-band fit.\n"
           "  dr         a few light claps at the singing position: direct-to-reverberant ratio (drDb), whether it\n"
           "             is a lower bound only, and every clap's tail SNR.\n"
           "  plosive    one pass of the plosive word list: transient ratio of every syllable, the maximum, events.\n"
           "  sibilance  one pass of the sibilance word list: 3-6 kHz and 6-12 kHz levels, high band over the\n"
           "             following vowel, vowel references, effective bandwidth, whether the high band is off.\n"
           "  clicks     natural reading: mouth clicks per minute, the loudest relative to speech, count.\n"
           "  direction  one direction take: median K-weighted level of the active frames and brightness.\n"
           "--gain-db adds that many dB to every sample first (all modes), e.g. 3.01 to undo a -3 dB pan law\n"
           "applied when a mono track was exported as two identical channels.\n"
           "--floor-dbfs (level only) is the noise floor (RMS, dBFS) a connect step would have read; like the\n"
           "plugin, it widens the flat-top tolerance. Without it the flat-top check uses the relative tolerance.\n"
           "Stereo files: a channel check runs first and every mode analyses the channel it picks (the louder\n"
           "one, the left when they are identical or opposite). Only the first two channels are read.\n";
}

bool parseNumber(const std::string& text, double& out)
{
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(out);
}
} // namespace

CliOutput runCli(const std::vector<std::string>& args, const FileReader& read)
{
    CliOutput result;
    const auto fail = [&result](const std::string& why) {
        result.exitCode = 2;
        result.err = "miciri-analyze: " + why + "\n";
        return result;
    };

    double gainDb = 0.0;
    std::optional<double> floorDbfs;
    Mode mode = Mode::Level;
    std::vector<std::string> files;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string& arg = args[i];
        if (arg == "--help" || arg == "-h")
        {
            result.err = usage(); // 要帮助是 0，用法照旧写在标准错误
            return result;
        }
        if (arg == "--gain-db" || arg == "--floor-dbfs" || arg == "--mode")
        {
            if (i + 1 >= args.size())
                return fail(arg + " needs a value");
            const std::string& value = args[++i];
            if (arg == "--mode")
            {
                const std::optional<Mode> m = parseMode(value);
                if (!m)
                    return fail("unknown mode '" + value + "', see --help");
                mode = *m;
                continue;
            }
            double v = 0.0;
            if (!parseNumber(value, v))
                return fail(arg + " needs a number");
            if (arg == "--gain-db")
                gainDb = v;
            else
                floorDbfs = v;
        }
        else if (arg.rfind("--", 0) == 0)
        {
            return fail("unknown option " + arg);
        }
        else
        {
            files.push_back(arg);
        }
    }
    if (files.size() != 1)
    {
        result.exitCode = 2;
        result.err = usage();
        return result;
    }
    if (floorDbfs && mode != Mode::Level)
        return fail("--floor-dbfs applies to the level mode only");

    std::vector<unsigned char> bytes;
    if (!read(files[0], bytes))
        return fail("cannot open the input file");
    WavData wav;
    std::string error;
    if (!parseWav(bytes, wav, error) || wav.channels.empty() || wav.channels[0].empty())
        return fail(error.empty() ? "no audio frames" : error);
    applyGainDb(wav, gainDb);
    result.out = mode == Mode::Level ? analyseToJson(wav, floorDbfs) : analyseModeToJson(mode, wav);
    return result;
}
} // namespace miciri::tools
