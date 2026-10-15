#pragma once
// File-replacement mods: <data>/mods/<Mod>/ holds files at their disc paths
// (either directly, mods/<Mod>/05/quest00_eng.bin, or under files/ as in a
// Dolphin extraction, mods/<Mod>/files/05/quest00_eng.bin), and an optional
// mod.ini (title, author, version, description). Enabled mods replace those
// disc files at any size; a later mod in PcSettings::enabledMods wins over an
// earlier one. Applied when the game data opens (asset_cache.cpp), so a
// change takes effect at the next start. The game's code is recompiled from
// main.dol, so sys/ files and code patches are not moddable.
#include "disc_image.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Mods {

struct Info {
    std::string folder;
    std::string title, author, version, description;
    size_t files = 0;
    uint64_t bytes = 0;
};

// Every mod folder, sorted by folder name.
std::vector<Info> Discover(const std::filesystem::path& directory);

// The files the enabled mods (in order, later wins) replace, among `discFiles`
// (DiscImage::FileNames). Paths matching no disc file go to `unmatched`.
std::vector<DiscImage::Replacement> Replacements(const std::filesystem::path& directory, const std::vector<std::string>& enabled,
                                                 const std::vector<std::string>& discFiles, std::vector<std::string>* unmatched = nullptr);

// Identifies a set of replacements by names, sizes and modification times:
// the converted asset cache is kept per set (empty for none).
std::string Fingerprint(const std::vector<DiscImage::Replacement>& replacements);

} // namespace Mods
