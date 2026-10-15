// Dolphin texture pack names on the native texture path
// (aurora texture_replacement.cpp, replace_native). The expected names were
// computed separately (Python xxhash, Dolphin's rules: XXH64 of the base
// level; for palette formats, of the palette entries the indices use), so a
// change in the hashing shows up here rather than as packs that silently
// stop matching.
#include "internal.hpp"
#include "gfx/dds_io.hpp"
#include "gfx/texture_replacement.hpp"
#include <aurora/native_texture.hpp>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace aurora::gfx::texture_replacement {
bool replace_native(const NativeTextureSource& source, NativeTextureReplacement& out);
}

namespace {
namespace fs = std::filesystem;
int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<uint8_t> bytes(size_t count, uint32_t mul, uint32_t add) {
    std::vector<uint8_t> out(count);
    for (size_t i = 0; i < count; ++i)
        out[i] = uint8_t(i * mul + add);
    return out;
}

void writeImage(const fs::path& path, uint32_t w, uint32_t h, uint8_t red) {
    std::vector<uint8_t> rgba(size_t(w) * h * 4);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = red;
        rgba[i + 3] = 255;
    }
    aurora::gfx::dds::write_rgba8_dds(path, w, h, {rgba.data(), rgba.size()});
}
} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "mh3-texture-pack-test";
    fs::remove_all(root);
    fs::create_directories(root / "pack" / "nested");
    writeImage(root / "pack" / "nested" / "tex1_16x8_01166e04da0ea4a8_1.dds", 64, 32, 200);
    writeImage(root / "pack" / "tex1_8x8_m_170aff5750bf8e80_39b223935f4a7d71_9.dds", 32, 32, 100);

    const auto pack = (root / "pack").string(), dump = (root / "dump").string();
    aurora::g_config.allowTextureReplacements = true;
    aurora::g_config.allowTextureDumps = true;
    aurora::g_config.textureReplacementPath = pack.c_str();
    aurora::g_config.textureDumpPath = dump.c_str();
    aurora::gfx::texture_replacement::initialize();
    using aurora::gfx::texture_replacement::replace_native;

    // I8 16x8, one level.
    const auto i8 = bytes(128, 37, 11);
    aurora::NativeTextureSource s{};
    s.bytes = i8;
    s.width = 16;
    s.height = 8;
    s.format = 1;
    aurora::NativeTextureReplacement r;
    check(replace_native(s, r) && r.width == 64 && r.height == 32 && r.levels == 1 && r.rgba[0] == 200, "I8 texture found in a nested folder");

    // Mipmapped original, one replacement level: box-filtered mips down to 1x1.
    s.levels = 3;
    r = {};
    check(replace_native(s, r) && r.levels == 7, "mips generated for a mipmapped original");

    // C8 8x8 (indices 5..20) with a 256-entry palette; the name carries _m.
    std::vector<uint8_t> indices(64);
    for (size_t i = 0; i < 64; ++i)
        indices[i] = uint8_t(5 + (i * 7) % 16);
    auto palette = bytes(512, 13, 1);
    aurora::NativeTextureSource p{};
    p.bytes = indices;
    p.palette = palette;
    p.width = 8;
    p.height = 8;
    p.levels = 2;
    p.format = 9;
    p.palette_format = 2;
    p.palette_entries = 256;
    r = {};
    check(replace_native(p, r) && r.width == 32 && r.rgba[0] == 100, "C8 texture found by texture and palette hash");

    // A used palette entry changed: no match. An unused one: still a match.
    palette[2 * 30] ^= 0xFF;
    r = {};
    check(replace_native(p, r), "entries outside the used range don't count");
    palette[2 * 7] ^= 0xFF;
    r = {};
    check(!replace_native(p, r), "a changed used entry misses");

    // An unknown texture is dumped once under its Dolphin name.
    const auto miss = bytes(48, 91, 3);
    aurora::NativeTextureSource m{};
    m.bytes = miss;
    m.width = 8;
    m.height = 4;
    m.levels = 2;
    m.format = 1;
    r = {};
    check(!replace_native(m, r), "an unknown texture misses");
    check(fs::exists(root / "dump" / "tex1_8x4_m_5f03325195675e97_1.dds"), "the miss is dumped under its pack name");

    aurora::gfx::texture_replacement::shutdown();
    fs::remove_all(root);

    if (failures)
        return 1;

    std::puts("texture pack names: all checks passed");
    return 0;
}
