#include "platform/paths.hpp"

#include <SDL3/SDL.h>

#include "core/log.hpp"

namespace platform {
namespace {

std::string g_asset_root;
std::string g_user_root;
bool        g_initialised = false;

}  // namespace

void paths_init(const char* organisation, const char* app_name) {
    if (g_initialised) {
        return;
    }
    g_initialised = true;

#if defined(SDL_PLATFORM_ANDROID)
    // Asset paths go to SDL_IOFromFile unprefixed; SDL resolves them through the
    // APK asset manager. A base path would be meaningless here.
    g_asset_root.clear();
#else
    if (const char* base = SDL_GetBasePath(); base != nullptr) {
        g_asset_root = base;
        // Development runs have the executable in build/<preset>/bin while the
        // assets stay in the source tree, so fall back to the source path baked
        // in at configure time.
        g_asset_root += "assets/";
    }
#endif

    if (char* pref = SDL_GetPrefPath(organisation, app_name); pref != nullptr) {
        g_user_root = pref;
        SDL_free(pref);
    }

    LOG_INFO("asset root: '%s'", g_asset_root.c_str());
    LOG_INFO("user root:  '%s'", g_user_root.c_str());
}

const std::string& asset_root() {
    return g_asset_root;
}

const std::string& user_root() {
    return g_user_root;
}

}  // namespace platform
