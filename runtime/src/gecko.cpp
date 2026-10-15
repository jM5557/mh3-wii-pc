// Gecko data codes (gecko.h).
#include "gecko.h"
#include "pc_paths.h"
#include "pc_settings.h"
#ifndef MH3_GECKO_STANDALONE
#include "memory.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace Gecko {
namespace {

std::string Trim(std::string_view text) {
    size_t begin = 0, end = text.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        ++begin;

    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        --end;

    return std::string(text.substr(begin, end - begin));
}

bool ParseHexWord(std::string_view text, uint32_t& out) {
    if (text.size() != 8)
        return false;

    uint32_t value = 0;

    for (char c : text) {
        value <<= 4;

        if (c >= '0' && c <= '9')
            value |= uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= uint32_t(c - 'A' + 10);
        else
            return false;
    }

    out = value;
    return true;
}

// Why a code can't run here, from its code types; empty if it can.
std::string Check(const std::vector<uint32_t>& words) {
    for (size_t i = 0; i + 1 < words.size(); i += 2) {
        const uint32_t type = words[i] >> 24;
        const uint32_t kind = type & 0xFE;

        if (kind <= 0x07 || (kind >= 0x10 && kind <= 0x17)) {
            // Writes: 06/07 strings take (value + 7) / 8 more lines.
            if ((kind & 0xEE) == 0x06)
                i += ((words[i + 1] + 7) / 8) * 2;
            continue;
        }

        if (kind == 0x08 || kind == 0x18) {
            i += 2; // serial writes take one more line
            continue;
        }

        if ((kind >= 0x20 && kind <= 0x2E) || (kind >= 0x30 && kind <= 0x3E) || (kind >= 0x40 && kind <= 0x4E) || type == 0xE0 || type == 0xE2 || type == 0xF0)
            continue;

        if (type >= 0xC0 && type <= 0xC7)
            return "patches or inserts PowerPC code (C0-C7), which the recompiled game doesn't run";

        if (type >= 0xD0 && type <= 0xDE)
            return "uses Gecko register or branch codes (D0-DE)";

        if (type >= 0xF2)
            return "searches or patches code (F2-FF)";

        char text[64];
        std::snprintf(text, sizeof(text), "uses code type %02X, which this port doesn't run", type);
        return text;
    }

    return {};
}

} // namespace

std::vector<Code> Parse(std::string_view text, std::vector<std::string>* enabled) {
    std::vector<Code> codes;
    std::istringstream input{std::string(text)};
    std::string line;
    enum class Section { Other, Gecko, GeckoEnabled } section = Section::Other;

    while (std::getline(input, line)) {
        const auto stripped = Trim(line);

        if (stripped.empty())
            continue;

        if (stripped.front() == '[') {
            if (stripped == "[Gecko]")
                section = Section::Gecko;
            else if (stripped == "[Gecko_Enabled]")
                section = Section::GeckoEnabled;
            else
                section = Section::Other;

            continue;
        }

        if (section == Section::GeckoEnabled) {
            if (enabled && stripped.front() == '$')
                enabled->push_back(Trim(std::string_view(stripped).substr(1)));

            continue;
        }

        if (section != Section::Gecko)
            continue;

        if (stripped.front() == '$') {
            Code code;
            std::string name = Trim(std::string_view(stripped).substr(1));

            // "$Name [Author]"
            if (!name.empty() && name.back() == ']')
                if (const auto open = name.rfind('['); open != std::string::npos) {
                    code.author = Trim(std::string_view(name).substr(open + 1, name.size() - open - 2));
                    name = Trim(std::string_view(name).substr(0, open));
                }

            code.name = name;
            codes.push_back(std::move(code));
            continue;
        }

        if (codes.empty())
            continue;

        if (stripped.front() == '*') {
            auto& notes = codes.back().notes;

            if (!notes.empty())
                notes += '\n';

            notes += Trim(std::string_view(stripped).substr(1));
            continue;
        }

        // "XXXXXXXX YYYYYYYY"
        std::istringstream words(stripped);
        std::string first, second;
        uint32_t a = 0, b = 0;

        if (words >> first >> second && ParseHexWord(first, a) && ParseHexWord(second, b)) {
            codes.back().words.push_back(a);
            codes.back().words.push_back(b);
        } else if (codes.back().unsupported.empty())
            codes.back().unsupported = "has a line that isn't a code: " + stripped;
    }

    for (auto& code : codes)
        if (code.unsupported.empty())
            code.unsupported = code.words.empty() ? "has no code lines" : Check(code.words);

    return codes;
}

bool Run(const Code& code, const Memory& memory) {
    if (!code.unsupported.empty())
        return false;

    uint32_t ba = 0x80000000, po = 0x80000000;
    // Execution: each nested condition pushes whether it held; code runs
    // while every level holds.
    std::vector<bool> conditions;
    const auto active = [&] { return std::all_of(conditions.begin(), conditions.end(), [](bool held) { return held; }); };
    const auto& w = code.words;

    for (size_t i = 0; i + 1 < w.size(); i += 2) {
        const uint32_t word = w[i], value = w[i + 1];
        const uint32_t type = word >> 24, kind = type & 0xEE; // drop the po bit (0x10) and the address's top bit
        const bool usePo = type & 0x10;
        const uint32_t address = (usePo ? po : ba) + (word & 0x01FFFFFF);

        // 20-3F conditions: an address with bit 0 set first ends the previous condition.
        if (type >= 0x20 && type <= 0x3F) {
            if ((word & 1) && !conditions.empty())
                conditions.pop_back();

            const uint32_t at = address & ~1u;
            bool held = false;

            if (!active()) {
                conditions.push_back(false);
                continue;
            }

            switch (kind & 0x0E) {
            case 0x00: // 20 equal (32-bit)
            case 0x02: // 22 not equal
            case 0x04: // 24 greater
            case 0x06: // 26 less
            {
                if (!memory.valid(at, 4)) {
                    held = false;
                    break;
                }

                const uint32_t current = memory.read(at, 4);
                const uint32_t op = kind & 0x06;
                held = op == 0x00 ? current == value : op == 0x02 ? current != value : op == 0x04 ? current > value : current < value;
                break;
            }
            default: // 28-2E 16-bit masked: value = mask << 16 | data
            {
                if (!memory.valid(at, 2)) {
                    held = false;
                    break;
                }

                const uint32_t mask = value >> 16, data = value & 0xFFFF;
                const uint32_t current = memory.read(at, 2) & ~mask & 0xFFFF;
                const uint32_t op = kind & 0x06;
                held = op == 0x00 ? current == data : op == 0x02 ? current != data : op == 0x04 ? current > data : current < data;
                break;
            }
            }

            conditions.push_back(held);
            continue;
        }

        if (type == 0xE0) { // full terminator: end every condition, reset ba/po
            conditions.clear();
            if (value >> 16)
                ba = value & 0xFFFF0000;
            if (value & 0xFFFF)
                po = value << 16;
            continue;
        }

        if (type == 0xE2) { // end `count` conditions (low byte of the value)
            const uint32_t count = std::max<uint32_t>(1, word & 0xFF);

            for (uint32_t n = 0; n < count && !conditions.empty(); ++n)
                conditions.pop_back();
            continue;
        }

        if (type == 0xF0)
            break;

        // Multi-line codes skip their extra lines even when inactive.
        if ((type & 0xEE) == 0x06) {
            const uint32_t extra = ((value + 7) / 8) * 2;

            if (active()) {
                for (uint32_t n = 0; n < value; ++n) {
                    const size_t lineWord = i + 2 + (n / 8) * 2 + ((n % 8) >= 4 ? 1 : 0);

                    if (lineWord >= w.size())
                        return false;

                    const uint32_t byte = (w[lineWord] >> (24 - 8 * (n % 4))) & 0xFF;

                    if (memory.valid(address + n, 1))
                        memory.write(address + n, 1, byte);
                }
            }

            i += extra;
            continue;
        }

        if ((type & 0xEE) == 0x08) { // serial write: next line TNNNZZZZ VVVVVVVV
            if (i + 3 >= w.size())
                return false;

            const uint32_t line2 = w[i + 2], increment = w[i + 3];
            const uint32_t size = line2 >> 28, count = ((line2 >> 16) & 0xFFF) + 1, step = line2 & 0xFFFF;
            const uint32_t bytes = size == 0 ? 1 : size == 1 ? 2 : 4;
            uint32_t at = address, data = value;

            if (active())
                for (uint32_t n = 0; n < count; ++n) {
                    if (memory.valid(at, bytes))
                        memory.write(at, bytes, data & (bytes == 4 ? 0xFFFFFFFFu : (1u << (bytes * 8)) - 1));
                    at += step;
                    data += increment;
                }

            i += 2;
            continue;
        }

        if (!active())
            continue;

        if (type <= 0x1F) { // 00-05 writes, (value >> 16) + 1 repeats for 8/16-bit
            switch (type & 0x0E) {
            case 0x00: {
                const uint32_t count = (value >> 16) + 1;
                for (uint32_t n = 0; n < count; ++n)
                    if (memory.valid(address + n, 1))
                        memory.write(address + n, 1, value & 0xFF);
                break;
            }
            case 0x02: {
                const uint32_t count = (value >> 16) + 1;
                for (uint32_t n = 0; n < count; ++n)
                    if (memory.valid(address + n * 2, 2))
                        memory.write(address + n * 2, 2, value & 0xFFFF);
                break;
            }
            case 0x04:
                if (memory.valid(address & ~3u, 4))
                    memory.write(address & ~3u, 4, value);
                break;
            default:
                return false;
            }

            continue;
        }

        if (type >= 0x40 && type <= 0x4F) {
            // 40/48 load ba/po from memory, 42/4A set it, 44/4C store it, 46/4E
            // set it to the code's own address (unsupported: no code list in RAM).
            const bool targetPo = type >= 0x48;
            uint32_t& target = targetPo ? po : ba;
            const uint32_t op = type & 0x06;
            // 4?TYZ00N: T add to the target rather than replace it, Y add a
            // base to the address, Z that base is po rather than ba.
            const bool addTarget = word & 0x00F00000;
            const bool addBase = word & 0x000F0000;
            const uint32_t base = (word & 0x0000F000) ? po : ba;
            const uint32_t at = value + (addBase ? base : 0);

            switch (op) {
            case 0x00:
                if (!memory.valid(at, 4))
                    return false;
                target = (addTarget ? target : 0) + memory.read(at, 4);
                break;
            case 0x02:
                target = (addTarget ? target : 0) + at;
                break;
            case 0x04:
                if (memory.valid(at, 4))
                    memory.write(at, 4, target);
                break;
            default:
                return false;
            }

            continue;
        }

        return false;
    }

    return true;
}

#ifndef MH3_GECKO_STANDALONE
// The game's executable sections (main.cpp registers the same range).
constexpr uint32_t kExecutableStart = 0x80004000, kExecutableEnd = 0x8056FAA0;

Memory GameMemory() {
    Memory memory;
    memory.valid = [](uint32_t address, uint32_t bytes) {
        const uint64_t end = uint64_t(address) + bytes;
        const bool mem1 = address >= 0x80000000 && end <= 0x81800000, mem2 = address >= 0x90000000 && end <= 0x94000000;
        const bool code = address < kExecutableEnd && end > kExecutableStart;
        return (mem1 || mem2) && !code;
    };
    memory.read = [](uint32_t address, uint32_t bytes) -> uint32_t {
        return bytes == 1 ? ::Memory::Read8(address) : bytes == 2 ? ::Memory::Read16(address) : ::Memory::Read32(address);
    };
    memory.write = [](uint32_t address, uint32_t bytes, uint32_t value) {
        if (bytes == 1)
            ::Memory::Write8(address, uint8_t(value));
        else if (bytes == 2)
            ::Memory::Write16(address, uint16_t(value));
        else
            ::Memory::Write32(address, value);
    };
    return memory;
}

namespace {
std::vector<Code> loaded;
}

const std::vector<Code>& Loaded() {
    return loaded;
}

void LoadFiles() {
    loaded.clear();
    const auto directory = PcPaths::Codes();
    std::error_code error;

    if (!std::filesystem::is_directory(directory, error))
        return;

    std::vector<std::filesystem::path> files;

    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        auto extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });

        if (entry.is_regular_file() && (extension == ".ini" || extension == ".txt"))
            files.push_back(entry.path());
    }

    std::sort(files.begin(), files.end());
    std::set<std::string> names;

    for (const auto& path : files) {
        std::ifstream file(path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();

        for (auto& code : Parse(text.str()))
            if (names.insert(code.name).second)
                loaded.push_back(std::move(code));
    }

    std::fprintf(stderr, "MH3 gecko: %zu codes from %s\n", loaded.size(), directory.string().c_str());
}

void RunEnabled() {
    const auto& enabled = PcSettings::Get().geckoCodes;

    if (enabled.empty() || loaded.empty())
        return;

    static const Memory memory = GameMemory();

    for (const auto& code : loaded)
        if (code.unsupported.empty() && std::find(enabled.begin(), enabled.end(), code.name) != enabled.end())
            Run(code, memory);
}
#endif

} // namespace Gecko
