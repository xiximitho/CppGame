#include "client/content.hpp"

#include <string>

#include "sim/monster_io.hpp"

#include <cstdint>
#include <vector>

#include "core/log.hpp"
#include "platform/vfs.hpp"
#include "sim/content_blob.hpp"

namespace client {

/// Loads the item catalogue from the baked blob, through platform::vfs so it works
/// from inside the APK on Android.
///
/// Falls back to the compiled-in table when the blob is missing, which keeps a
/// fresh clone runnable before anyone has run game_bake — the same reasoning as the
/// procedural-art fallback in Tileset::load. A blob that exists but does not parse
/// is a different matter and says so loudly: that is corruption or a version
/// mismatch, not an absence.
sim::ItemTypeRegistry load_item_catalogue() {
    std::vector<std::uint8_t> bytes;
    if (!platform::vfs::read_asset("content.bin", bytes)) {
        LOG_INFO("no content.bin; using the built-in item catalogue "
                 "(run game_bake to author items)");
        return sim::build_default_registry();
    }

    sim::ItemTypeRegistry loaded;
    if (!sim::read_content_blob(bytes.data(), bytes.size(), loaded)) {
        LOG_WARN("content.bin failed to parse (wrong version, or corrupt); "
                 "falling back to the built-in catalogue");
        return sim::build_default_registry();
    }
    LOG_INFO("loaded %zu item types from content.bin", loaded.count());
    return loaded;
}

sim::MonsterRegistry load_monster_catalogue() {
    std::string text;
    if (!platform::vfs::read_asset_text("monsters.txt", text)) {
        LOG_INFO("no monsters.txt; using the built-in monster classes");
        return sim::default_monsters();
    }
    sim::MonsterRegistry loaded;
    std::string error;
    if (!sim::parse_monster_catalogue(text, loaded, &error)) {
        LOG_WARN("monsters.txt failed to parse: %s; using built-in classes",
                 error.c_str());
        return sim::default_monsters();
    }
    LOG_INFO("loaded %zu monster class(es) from monsters.txt", loaded.count());
    return loaded;
}

}  // namespace client
