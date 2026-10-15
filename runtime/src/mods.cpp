// File-replacement mods (mods.h).
#include "mods.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>

namespace Mods {
namespace {

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

std::string Trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");

    if (begin == std::string::npos)
        return {};

    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

// The disc path of a file in a mod folder: relative to the folder, or to its
// files/ subfolder.
std::string DiscPath(const std::filesystem::path& folder, const std::filesystem::path& file) {
    auto relative = file.lexically_relative(folder).generic_string();

    if (relative.rfind("files/", 0) == 0)
        relative = relative.substr(6);

    return relative;
}

uint64_t Fnv64(const std::string& text, uint64_t hash = 1469598103934665603ull) {
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }

    return hash;
}

} // namespace

std::vector<Info> Discover(const std::filesystem::path& directory) {
    std::vector<Info> mods;
    std::error_code error;

    if (!std::filesystem::is_directory(directory, error))
        return mods;

    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (!entry.is_directory(error))
            continue;

        Info info;
        info.folder = entry.path().filename().string();
        info.title = info.folder;

        if (std::ifstream ini(entry.path() / "mod.ini"); ini) {
            std::string line;

            while (std::getline(ini, line)) {
                const auto equals = line.find('=');

                if (equals == std::string::npos || Trim(line).rfind('#', 0) == 0)
                    continue;

                const auto key = Lower(Trim(line.substr(0, equals))), value = Trim(line.substr(equals + 1));

                if (key == "title" || key == "name")
                    info.title = value;
                else if (key == "author")
                    info.author = value;
                else if (key == "version")
                    info.version = value;
                else if (key == "description")
                    info.description = value;
            }
        }

        for (const auto& file : std::filesystem::recursive_directory_iterator(entry.path(), error))
            if (file.is_regular_file(error) && file.path().filename() != "mod.ini") {
                ++info.files;
                info.bytes += file.file_size(error);
            }

        mods.push_back(std::move(info));
    }

    std::sort(mods.begin(), mods.end(), [](const Info& a, const Info& b) { return Lower(a.folder) < Lower(b.folder); });
    return mods;
}

std::vector<DiscImage::Replacement> Replacements(const std::filesystem::path& directory, const std::vector<std::string>& enabled,
                                                 const std::vector<std::string>& discFiles, std::vector<std::string>* unmatched) {
    // Disc paths by lower case: mod folders made on case-insensitive systems still match.
    std::map<std::string, std::string> byLower;

    for (const auto& name : discFiles)
        byLower.emplace(Lower(name), name);

    std::map<std::string, DiscImage::Replacement> chosen; // later mods overwrite
    std::error_code error;

    for (const auto& mod : enabled) {
        const auto folder = directory / mod;

        if (!std::filesystem::is_directory(folder, error)) {
            if (unmatched)
                unmatched->push_back(mod + " (folder not found)");
            continue;
        }

        for (const auto& file : std::filesystem::recursive_directory_iterator(folder, error)) {
            if (!file.is_regular_file(error) || file.path().filename() == "mod.ini")
                continue;

            const auto path = DiscPath(folder, file.path());
            const auto it = byLower.find(Lower(path));

            if (it == byLower.end()) {
                if (unmatched)
                    unmatched->push_back(mod + "/" + path);
                continue;
            }

            chosen[it->second] = {it->second, file.path(), uint64_t(file.file_size(error))};
        }
    }

    std::vector<DiscImage::Replacement> result;

    for (auto& [name, replacement] : chosen)
        result.push_back(std::move(replacement));

    return result;
}

std::string Fingerprint(const std::vector<DiscImage::Replacement>& replacements) {
    if (replacements.empty())
        return {};

    uint64_t hash = 1469598103934665603ull;
    std::error_code error;

    for (const auto& r : replacements) {
        const auto time = std::filesystem::last_write_time(r.path, error).time_since_epoch().count();
        hash = Fnv64(r.name + '\n' + std::to_string(r.size) + '\n' + std::to_string(time) + '\n', hash);
    }

    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

} // namespace Mods
