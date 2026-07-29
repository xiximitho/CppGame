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
#elif defined(GAME_ASSET_ROOT)
    // Development runs have the executable in build/<preset>/bin while the assets
    // stay in the source tree. Point straight at the source dir (baked in at
    // configure time) so an edit — a map-editor save, a new sprite — is seen by
    // the next run without a rebuild or a copy step, and so the editor and the
    // client read and write the exact same files.
    g_asset_root = GAME_ASSET_ROOT;
#else
    if (const char* base = SDL_GetBasePath(); base != nullptr) {
        g_asset_root = base;
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
