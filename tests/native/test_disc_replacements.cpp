// Disc file replacements (DiscImage::ApplyReplacements, mods.cpp) on a tiny
// extracted partition built here: the file table is patched, the replaced
// file reads from the mod at any size, other files are untouched.
#include "disc_image.h"
#include "mods.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// The test reads an extracted partition only: the packed-image reader is not
// linked, and these stand-ins report that no image is available.
#include "rom_image.h"
class RomImage::Source {};
RomImage::RomImage() = default;
RomImage::~RomImage() = default;
Status RomImage::Open(const std::filesystem::path&, std::unique_ptr<RomImage>&) { return Status::Error("no disc images in this test"); }
uint64_t RomImage::FileBytes() const { return 0; }
Status RomImage::ReadPartition(uint64_t, uint8_t*, size_t) const { return Status::Error("no disc images in this test"); }

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v >> 24);
    b[at + 1] = uint8_t(v >> 16);
    b[at + 2] = uint8_t(v >> 8);
    b[at + 3] = uint8_t(v);
}

uint32_t get32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}

void write(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

std::vector<uint8_t> filled(size_t n, uint8_t v) {
    return std::vector<uint8_t>(n, v);
}
} // namespace

int main(int argc, char** argv) {
    const fs::path root = (argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "mh3-disc-replacements-test") / "DATA";
    fs::remove_all(root.parent_path());
    // Layout: boot 0, bi2 0x440, apploader 0x2440 (0x20), dol 0x3000 (0x100),
    // fst 0x4000, files: 05/a.bin at 0x8000 (0x10 bytes), 05/b.bin at 0x9000 (0x20 bytes).
    std::vector<uint8_t> boot(0x440);
    put32(boot, 0x18, 0x5D1C9EA3);
    put32(boot, 0x420, 0x3000 / 4);
    // FST: root, dir "05", files a.bin, b.bin; string table after 4 entries.
    std::vector<uint8_t> fst(4 * 12);
    std::string names;
    const auto name = [&](const char* text) {
        const auto at = uint32_t(names.size());
        names += text;
        names += '\0';
        return at;
    };
    put32(fst, 0, 0x01000000);
    put32(fst, 8, 4);
    put32(fst, 12, 0x01000000 | name("05"));
    put32(fst, 16, 0);
    put32(fst, 20, 4);
    put32(fst, 24, name("a.bin"));
    put32(fst, 28, 0x8000 / 4);
    put32(fst, 32, 0x10);
    put32(fst, 36, name("b.bin"));
    put32(fst, 40, 0x9000 / 4);
    put32(fst, 44, 0x20);
    fst.insert(fst.end(), names.begin(), names.end());
    while (fst.size() % 4)
        fst.push_back(0);
    put32(boot, 0x424, 0x4000 / 4);
    put32(boot, 0x428, uint32_t(fst.size() / 4));
    write(root / "sys/boot.bin", boot);
    write(root / "sys/bi2.bin", filled(0x2000, 0));
    write(root / "sys/apploader.img", filled(0x20, 0x11));
    write(root / "sys/main.dol", filled(0x100, 0x22));
    write(root / "sys/fst.bin", fst);
    write(root / "files/05/a.bin", filled(0x10, 0xAA));
    write(root / "files/05/b.bin", filled(0x20, 0xBB));

    // A mod replacing b.bin with a bigger file (Dolphin-style files/ layout).
    const fs::path mods = root.parent_path() / "mods";
    write(mods / "Bigger B/files/05/B.BIN", filled(0x30, 0xCC));
    write(mods / "Bigger B/readme.txt", filled(4, 0x20));
    std::ofstream(mods / "Bigger B/mod.ini") << "title = Bigger B\nauthor = Tester\n";

    DiscImage image(root, false);
    check(image.FileSize("05/b.bin") == 0x20, "original size");
    const auto infos = Mods::Discover(mods);
    check(infos.size() == 1 && infos[0].title == "Bigger B" && infos[0].author == "Tester" && infos[0].files == 2, "mod discovered with its ini");
    std::vector<std::string> unmatched;
    const auto replacements = Mods::Replacements(mods, {"Bigger B"}, image.FileNames(), &unmatched);
    check(replacements.size() == 1 && replacements[0].name == "05/b.bin" && replacements[0].size == 0x30, "case-insensitive match under files/");
    check(unmatched.size() == 1 && unmatched[0] == "Bigger B/readme.txt", "a file the disc doesn't have is reported");
    check(!Mods::Fingerprint(replacements).empty() && Mods::Fingerprint({}).empty(), "fingerprint");
    const auto status = image.ApplyReplacements(replacements);
    check(status.ok(), "replacements apply");

    const uint64_t moved = image.FileOffset("05/b.bin");
    check(moved >= 0x10000 && image.FileSize("05/b.bin") == 0x30, "file moved past the partition with its new size");
    const auto table = image.FstBytes();
    check(uint64_t(get32(table, 40)) * 4 == moved && get32(table, 44) == 0x30, "file table patched");
    check(get32(table, 28) == 0x8000 / 4 && get32(table, 32) == 0x10, "other entries untouched");

    std::vector<uint8_t> bytes(0x30);
    check(image.ReadRaw(moved, bytes.data(), bytes.size()).ok() && bytes == filled(0x30, 0xCC), "replacement content");
    std::vector<uint8_t> a(0x10);
    check(image.ReadRaw(0x8000, a.data(), a.size()).ok() && a == filled(0x10, 0xAA), "unreplaced file");
    std::vector<uint8_t> fstRead(table.size());
    check(image.ReadRaw(0x4000, fstRead.data(), fstRead.size()).ok() && fstRead == table, "reads of the file table see the patch");
    check(image.ReadFile("05/b.bin") == filled(0x30, 0xCC), "ReadFile through the moved entry");

    if (failures)
        return 1;

    std::puts("disc replacements: all checks passed");
    return 0;
}
