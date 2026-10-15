// Player-added content directories (pc_paths.h).
#include "pc_paths.h"

#include <cstdlib>

namespace PcPaths {

std::filesystem::path Data() {
    if (const char* user = std::getenv("MH3_USER_DIR"); user && *user) {
        auto saves = std::filesystem::path(user).lexically_normal();

        if (saves.filename().empty()) // a trailing separator
            saves = saves.parent_path();

        const auto parent = saves.parent_path();

        if (!parent.empty())
            return parent;
    }

    return std::filesystem::current_path();
}

std::filesystem::path Mods() {
    return Data() / "mods";
}

std::filesystem::path Codes() {
    return Data() / "codes";
}

std::filesystem::path Textures() {
    return Data() / "Load" / "Textures" / "RMHP08";
}

std::filesystem::path TextureDumps() {
    return Data() / "Dump" / "Textures" / "RMHP08";
}

} // namespace PcPaths
