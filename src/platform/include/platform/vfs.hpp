#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

/// Asset access. Every read of shipped game data must go through here.
///
/// This is not paranoia about abstraction: on Android the assets live *inside*
/// the APK and are not files on a filesystem at all. std::ifstream simply cannot
/// open them. SDL_IOStream can, transparently, on all five targets. Code that
/// reaches for <fstream> works on desktop and fails on device, and it fails
/// months later when the Android build is finally tried.
namespace vfs {

/// Reads a shipped asset, path relative to the asset root ("tilesets/x.png").
/// Returns false when missing; `out` is cleared first.
bool read_asset(const std::string& relative_path, std::vector<std::uint8_t>& out);

/// Convenience for text assets. Returns false when missing.
bool read_asset_text(const std::string& relative_path, std::string& out);

/// Writes into the per-user writable directory (save games, settings, logs).
/// Never the asset root: on mobile and in a packaged desktop install it is
/// read-only. Creates parent directories as needed.
bool write_user_file(const std::string& relative_path, const void* data,
                     std::size_t length);

bool read_user_file(const std::string& relative_path,
                    std::vector<std::uint8_t>& out);

bool read_user_text(const std::string& relative_path, std::string& out);

}  // namespace vfs
}  // namespace platform
