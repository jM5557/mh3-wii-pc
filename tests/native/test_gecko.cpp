// Gecko data-code engine (gecko.cpp) against a fake memory.
#include "gecko.h"

#include <cstdio>
#include <map>

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

std::map<uint32_t, uint8_t> ram;

Gecko::Memory FakeMemory() {
    Gecko::Memory m;
    m.valid = [](uint32_t a, uint32_t n) { return a >= 0x80000000 && uint64_t(a) + n <= 0x81800000 && !(a < 0x8056FAA0 && a + n > 0x80004000); };
    m.read = [](uint32_t a, uint32_t n) {
        uint32_t v = 0;
        for (uint32_t i = 0; i < n; ++i)
            v = v << 8 | ram[a + i];
        return v;
    };
    m.write = [](uint32_t a, uint32_t n, uint32_t v) {
        for (uint32_t i = 0; i < n; ++i)
            ram[a + i] = uint8_t(v >> (8 * (n - 1 - i)));
    };
    return m;
}

uint32_t r32(uint32_t a) { return FakeMemory().read(a, 4); }
uint32_t r16(uint32_t a) { return FakeMemory().read(a, 2); }
uint32_t r8(uint32_t a) { return FakeMemory().read(a, 1); }
} // namespace

int main() {
    const char* ini = R"([Gecko]
$Writes [Tester]
* 8, 16 and 32-bit writes
00600000 000200AB
02600010 0001BEEF
04600020 12345678
$Conditional
20600100 00000001
04600104 CAFEBABE
E2000001 00000000
04600108 00000007
$String
06600200 00000005
48656C6C 6F000000
$Pointer
48000000 80600300
14000010 0000002A
E0000000 80008000
$Serial
08600400 00000001
20020004 00000001
$Code patch
C2123456 00000002
60000000 60000000
4E800020 00000000
$Masked 16-bit if
28600500 FF000034
04600504 00000099
E0000000 80008000
[Gecko_Enabled]
$Writes
$String
)";
    std::vector<std::string> enabled;
    const auto codes = Gecko::Parse(ini, &enabled);
    check(codes.size() == 7, "seven codes parsed");
    check(enabled.size() == 2 && enabled[0] == "Writes" && enabled[1] == "String", "[Gecko_Enabled] list");
    check(codes[0].author == "Tester" && codes[0].notes == "8, 16 and 32-bit writes", "author and notes");
    check(!codes[5].unsupported.empty(), "C2 code patch is unsupported");
    const auto memory = FakeMemory();

    for (size_t i = 0; i < codes.size(); ++i)
        if (codes[i].unsupported.empty())
            check(Gecko::Run(codes[i], memory), codes[i].name.c_str());

    check(r8(0x80600000) == 0xAB && r8(0x80600001) == 0xAB && r8(0x80600002) == 0xAB && r8(0x80600003) == 0, "8-bit write x3");
    check(r16(0x80600010) == 0xBEEF && r16(0x80600012) == 0xBEEF && r16(0x80600014) == 0, "16-bit write x2");
    check(r32(0x80600020) == 0x12345678, "32-bit write");
    check(r32(0x80600104) == 0, "false condition skips its write");
    check(r32(0x80600108) == 7, "write after the endif runs");
    check(r8(0x80600200) == 'H' && r8(0x80600204) == 'o' && r8(0x80600205) == 0, "string write");
    check(r32(0x80600408) == 3 && r32(0x80600404) == 2 && r32(0x80600400) == 1, "serial write");

    // Condition true now: a second run takes the branch.
    FakeMemory().write(0x80600100, 4, 1);
    Gecko::Run(codes[1], memory);
    check(r32(0x80600104) == 0xCAFEBABE, "true condition runs its write");

    // Pointer: po = [0x80600300]; po + 0x10 gets 0x2A.
    FakeMemory().write(0x80600300, 4, 0x80700000);
    Gecko::Run(codes[3], memory);
    check(r32(0x80700010) == 0x2A, "pointer write through po");

    // Masked 16-bit: (value & ~0xFF00) == 0x34.
    FakeMemory().write(0x80600500, 2, 0x7734);
    Gecko::Run(codes[6], memory);
    check(r32(0x80600504) == 0x99, "masked 16-bit condition");

    // Writes into the executable range are refused.
    ram.clear();
    const auto text = Gecko::Parse("[Gecko]\n$Text\n04100000 60000000\n");
    Gecko::Run(text[0], memory);
    check(r32(0x80100000) == 0, "no writes into the game's code");

    if (failures)
        return 1;

    std::puts("gecko: all checks passed");
    return 0;
}
