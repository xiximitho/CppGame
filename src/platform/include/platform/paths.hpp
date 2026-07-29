#pragma once

#include <string>

namespace platform {

/// Must be called once after SDL_Init, before any vfs call.
void paths_init(const char* organisation, const char* app_name);

/// Where shipped, read-only game data lives.
///
/// Empty string on Android: there, asset paths are passed to SDL_IOFromFile
/// unprefixed and SDL routes them into the APK's asset manager. Everywhere else
/// this is a real directory next to the executable. Callers must not assume it
/// is a valid filesystem path — that is exactly what vfs exists to hide.
const std::string& asset_root();

/// Per-user writable directory. Always a real, existing directory.
const std::string& user_root();

}  // namespace platform
