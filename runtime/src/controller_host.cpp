#include "controller_host.h"
#include "fatal.h"
#include "dev_options.h"
#include "../native/mh3_classic_map.h"
#if defined(MH3_HAS_AURORA) && !defined(__vita__)
#include "pc_input.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <vector>
#if defined(MH3_HAS_AURORA) && !defined(__vita__)
#include <SDL3/SDL.h>
#endif
namespace {
std::optional<ClassicControllerSample> testSample;
uint64_t inputFrame = 0;
bool playbackHeld = false;

// MH3_INPUT=[file=PATH][,record=PATH][,playback=PATH][,camera=N]: scripted controller
// input. file: a one-sample control file, read every frame, replaces the host
// controller. record and playback: frame-indexed input logs for repeatable
// runs; each line is "frame buttons(hex) lx ly rx ry", the sample in effect
// from that paced frame on. record writes one line per change of the host
// input; playback replaces the host input with a log. camera=N[:D]
// (development builds): the field camera looks at monster N, from D units
// away (field_camera.cpp).
const DevOptions& InputOptions() {
    static const DevOptions options("MH3_INPUT", {"file", "record", "playback", "camera"});
    return options;
}

bool SameInput(const ClassicControllerSample& a, const ClassicControllerSample& b) {
    return a.buttons == b.buttons && a.leftX == b.leftX && a.leftY == b.leftY && a.rightX == b.rightX && a.rightY == b.rightY;
}

void RecordInput(const ClassicControllerSample& sample) {
    static const char* path = InputOptions().Value("record");

    if (!path)
        return;

    static std::ofstream log(path);
    static std::optional<ClassicControllerSample> last;

    if (last && SameInput(*last, sample))
        return;

    last = sample;
    log << inputFrame << ' ' << std::hex << sample.buttons << std::dec << ' ' << sample.leftX << ' ' << sample.leftY << ' ' << sample.rightX << ' ' << sample.rightY << std::endl;
}

std::optional<ClassicControllerSample> PlaybackInput() {
    static const char* path = InputOptions().Value("playback");

    if (!path)
        return std::nullopt;

    struct Entry {
        uint64_t frame;
        ClassicControllerSample sample;
    };

    static const std::vector<Entry> entries = [] {
        std::vector<Entry> result;
        std::ifstream input(path);
        uint64_t frame;
        unsigned buttons;
        int lx, ly, rx, ry;

        while (input >> frame >> std::hex >> buttons >> std::dec >> lx >> ly >> rx >> ry) {
            if (!result.empty() && frame < result.back().frame)
                MH3_THROW(std::runtime_error("MH3_INPUT playback frames must not decrease"));

            result.push_back({frame, {true, uint16_t(buttons), int16_t(lx), int16_t(ly), int16_t(rx), int16_t(ry)}});
        }

        if (!input.eof() || result.empty())
            MH3_THROW(std::runtime_error("Invalid MH3_INPUT playback log"));

        return result;
    }();
    static size_t next = 0;

    // While held (the village shortcut still navigating), the log does not
    // reach its next entry; it resumes from there, later by the time held.
    static uint64_t delay = 0;
    uint64_t frame = inputFrame - delay;

    if (playbackHeld && next < entries.size() && entries[next].frame != 0 && entries[next].frame <= frame) {
        delay += frame - (entries[next].frame - 1);
        frame = entries[next].frame - 1;
    }

    static uint64_t reportedDelay = 0;

    if (!playbackHeld && delay != reportedDelay) {
        std::fprintf(stderr, "MH3 input playback: held %llu frames for the village shortcut\n", (unsigned long long)(delay - reportedDelay));
        reportedDelay = delay;
    }
    while (next < entries.size() && entries[next].frame <= frame)
        ++next;

    if (next == entries.size()) {
        static bool reported = false;

        if (!reported)
            std::fprintf(stderr, "MH3 input playback: finished at frame %llu\n", (unsigned long long)frame);

        reported = true;
    }

    if (next == 0)
        return ClassicControllerSample{true};

    return entries[next - 1].sample;
}
} // namespace
#ifdef MH3_DEVELOPMENT
int DebugCameraMonster(float* distance) {
    static float eyeDistance = 0.0f;
    static const int monster = [] {
        const char* value = InputOptions().Value("camera");

        if (!value)
            return -1;

        if (const char* colon = std::strchr(value, ':'))
            eyeDistance = float(std::atof(colon + 1));

        return std::atoi(value);
    }();

    if (distance)
        *distance = eyeDistance;

    return monster;
}
#endif

void SetControllerTestSample(std::optional<ClassicControllerSample> sample) {
    testSample = sample;
}

void SetInputFrame(uint64_t frame) {
    inputFrame = frame;
}

void HoldInputPlayback(bool hold) {
    playbackHeld = hold;
}

ClassicControllerSample ReadHostController() {
    if (testSample)
        return *testSample;

    if (auto played = PlaybackInput()) {
        RecordInput(*played);
        return *played;
    }

    const ClassicControllerSample sample = ReadLiveController();
    RecordInput(sample);
    return sample;
}

ClassicControllerSample ReadLiveController() {
    ClassicControllerSample sample;

    if (const char* path = InputOptions().Value("file")) {
        // Explicit diagnostic controller; the normal source is SDL. This lets
        // unattended runs exercise the same guest input path while macOS is locked.
        std::ifstream input(path);
        unsigned buttons;
        int lx, ly, rx, ry;

        if (!(input >> std::hex >> buttons >> std::dec >> lx >> ly >> rx >> ry) || buttons > 0xFFFF || lx < -512 || lx > 511 || ly < -512 || ly > 511 || rx < -512 || rx > 511 || ry < -512 || ry > 511)
            MH3_THROW(std::runtime_error("Invalid MH3_INPUT file: expected hex buttons and four signed axes (-512..511)"));

        sample = {true, uint16_t(buttons), int16_t(lx), int16_t(ly), int16_t(rx), int16_t(ry)};
        return sample;
    }
#if defined(__vita__)
    (void)sample;
    return ReadVitaController();
#elif defined(MH3_HAS_AURORA)
    // The bound keyboard keys, mouse buttons and mouse look, and the chosen
    // SDL gamepad, as the Classic Controller on channel 0 (pc_input.cpp,
    // PcSettings; docs/optimizations/system/controller-input.md).
    (void)sample;
    return PcInput::Read();
#endif
    return sample;
}
