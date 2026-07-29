#include "platform/vfs.hpp"

#include <SDL3/SDL.h>

#include "core/log.hpp"
#include "platform/paths.hpp"

namespace platform::vfs {
namespace {

bool load_via_sdl(const std::string& full_path, std::vector<std::uint8_t>& out) {
    out.clear();

    std::size_t size = 0;
    // SDL_LoadFile goes through SDL_IOFromFile, which is what makes this work
    // for APK-embedded assets on Android as well as ordinary files.
    void* data = SDL_LoadFile(full_path.c_str(), &size);
    if (data == nullptr) {
        return false;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out.assign(bytes, bytes + size);
    SDL_free(data);
    return true;
}

}  // namespace

bool read_asset(const std::string& relative_path,
                std::vector<std::uint8_t>& out) {
    if (load_via_sdl(asset_root() + relative_path, out)) {
        return true;
    }

    // Second chance for developer builds: the executable sits in build/<x>/bin
    // but the assets are still in the source tree.
    const std::string source_relative = std::string("../../../assets/") + relative_path;
    if (load_via_sdl(asset_root() + source_relative, out)) {
        return true;
    }

    LOG_DEBUG("asset not found: %s", relative_path.c_str());
    return false;
}

bool read_asset_text(const std::string& relative_path, std::string& out) {
    std::vector<std::uint8_t> bytes;
    if (!read_asset(relative_path, bytes)) {
        out.clear();
        return false;
    }
    out.assign(bytes.begin(), bytes.end());
    return true;
}

bool write_user_file(const std::string& relative_path, const void* data,
                     std::size_t length) {
    const std::string full = user_root() + relative_path;

    // Create the parent directory chain; SDL_CreateDirectory is recursive.
    const std::size_t slash = full.find_last_of("/\\");
    if (slash != std::string::npos) {
        SDL_CreateDirectory(full.substr(0, slash).c_str());
    }

    SDL_IOStream* stream = SDL_IOFromFile(full.c_str(), "wb");
    if (stream == nullptr) {
        LOG_WARN("cannot write '%s': %s", full.c_str(), SDL_GetError());
        return false;
    }

    const std::size_t written = SDL_WriteIO(stream, data, length);
    SDL_CloseIO(stream);

    if (written != length) {
        LOG_WARN("short write to '%s'", full.c_str());
        return false;
    }
    return true;
}

bool read_user_file(const std::string& relative_path,
                    std::vector<std::uint8_t>& out) {
    return load_via_sdl(user_root() + relative_path, out);
}

bool read_user_text(const std::string& relative_path, std::string& out) {
    std::vector<std::uint8_t> bytes;
    if (!read_user_file(relative_path, bytes)) {
        out.clear();
        return false;
    }
    out.assign(bytes.begin(), bytes.end());
    return true;
}

}  // namespace platform::vfs
