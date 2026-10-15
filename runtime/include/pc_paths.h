#pragma once
// Where the PC build keeps what the player adds: the data directory is the
// parent of MH3_USER_DIR (the AppImage: ~/.local/share/mh3-wii), else the
// working directory (a native build's checkout).
#include <filesystem>

namespace PcPaths {
std::filesystem::path Data();
std::filesystem::path Mods();     // <data>/mods/<mod>/... (disc file replacements)
std::filesystem::path Codes();    // <data>/codes/*.ini, *.txt (Gecko codes)
std::filesystem::path Textures(); // <data>/Load/Textures/RMHP08 (Dolphin HD texture packs)
std::filesystem::path TextureDumps(); // <data>/Dump/Textures/RMHP08
} // namespace PcPaths
