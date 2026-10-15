#pragma once
// Gecko cheat codes from Dolphin-style lists ([Gecko] sections of RMHP08.ini
// files), run once per game frame like the Gecko code handler on a Wii.
//
// Only data codes can work in a static recompilation: RAM writes (00-07
// types), serial writes (08), conditions (20-2F, with E0/E2 endifs), base
// and pointer loads (40-4F) and the terminators (E0, F0). Codes that patch or
// insert PowerPC instructions (C0-C2, D0-DE branches, writes into the game's
// executable range) cannot change translated code; such codes are marked
// unsupported and never run. The port's own 257 cheats (cheats.md) are the
// native versions of the published code lists.
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Gecko {

struct Code {
    std::string name;
    std::string author;
    std::string notes;          // the code's * comment lines
    std::vector<uint32_t> words; // pairs: code word, value word
    std::string unsupported;    // why it can't run here; empty when it can
};

// The [Gecko] codes of an ini file's text. Codes in [Gecko_Enabled] are
// listed in `enabled` (Dolphin's per-game files).
std::vector<Code> Parse(std::string_view text, std::vector<std::string>* enabled = nullptr);

// Guest memory access for the engine (tests substitute their own).
struct Memory {
    std::function<bool(uint32_t address, uint32_t bytes)> valid; // a data address the codes may touch
    std::function<uint32_t(uint32_t address, uint32_t bytes)> read;
    std::function<void(uint32_t address, uint32_t bytes, uint32_t value)> write;
};

// Runs one code once. Returns false (and stops) on a malformed code.
bool Run(const Code& code, const Memory& memory);

// The game's memory: MEM1 and MEM2 data, outside the executable range
// 0x80004000-0x8056FAA0.
Memory GameMemory();

// Codes loaded from <data>/codes/*.ini and *.txt and enabled through
// PcSettings::geckoCodes; RunEnabled runs them (frame pacer).
const std::vector<Code>& Loaded();
void LoadFiles();
void RunEnabled();

} // namespace Gecko
