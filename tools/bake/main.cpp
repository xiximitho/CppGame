// game_bake — turns the authored content database into the blob the client reads.
//
// The client cannot open the database, and that is not an oversight: on Android and
// iOS its assets live inside the application package and are not files, while SQLite
// needs a real path it can open and seek. So content reaches the client as bytes,
// through platform::vfs, and this is the step that produces those bytes.
//
// The server does not need it — it reads content.db directly (docs/content.md).
//
//   game_bake [--content assets/content.db] [--out assets/content.bin]

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "core/log.hpp"
#include "sim/content_blob.hpp"
#include "sim/item_type.hpp"
#include "store/content.hpp"
#include "store/db.hpp"

namespace {

struct Options {
    std::string content_path = "assets/content.db";
    std::string out_path = "assets/content.bin";
};

void print_usage() {
    std::printf(
        "usage: game_bake [options]\n"
        "\n"
        "  --content PATH   content database to read (default assets/content.db)\n"
        "  --out PATH       blob to write (default assets/content.bin)\n"
        "  --help           this text\n");
}

/// Returns false when the arguments say "stop", not when they are wrong.
bool parse_args(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = (i + 1) < argc;
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return false;
        }
        if (arg == "--content" && has_value) {
            options.content_path = argv[++i];
        } else if (arg == "--out" && has_value) {
            options.out_path = argv[++i];
        } else {
            LOG_WARN("ignoring unknown argument '%s'", arg.c_str());
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    core::log_set_tag("bake");

    Options options;
    if (!parse_args(argc, argv, options)) {
        return 0;
    }

    // Read-only: baking must never be the thing that creates or migrates a
    // database. If the file is missing, that is a mistake worth reporting rather
    // than papering over with an empty catalogue.
    std::optional<store::Db> db = store::Db::open_read_only(options.content_path);
    if (!db.has_value()) {
        LOG_ERROR("cannot open '%s' for reading", options.content_path.c_str());
        return 1;
    }

    sim::ItemTypeRegistry registry;
    if (!store::load_item_types(*db, registry)) {
        LOG_ERROR("cannot read item types from '%s'",
                  options.content_path.c_str());
        return 1;
    }

    const std::vector<std::uint8_t> blob = sim::write_content_blob(registry);
    if (blob.empty()) {
        LOG_ERROR("serialisation failed (empty blob)");
        return 1;
    }

    // Write, then verify by reading back through the very function the client will
    // use. A blob that this tool cannot parse is a blob no client can, and finding
    // that out here costs nothing while finding it out on a device costs a day.
    sim::ItemTypeRegistry check;
    if (!sim::read_content_blob(blob.data(), blob.size(), check)) {
        LOG_ERROR("wrote a blob that cannot be read back; refusing to save it");
        return 1;
    }
    if (check.ids() != registry.ids()) {
        LOG_ERROR("blob round-trip lost item types (%zu in, %zu out)",
                  registry.count(), check.count());
        return 1;
    }
    // Whole-struct comparison, not a list of fields: a check that only compares the
    // fields someone remembered to list is how a dropped field ships.
    for (const sim::ItemTypeId id : registry.ids()) {
        if (!(check.get(id) == registry.get(id))) {
            LOG_ERROR("item %u does not survive the blob round-trip",
                      static_cast<unsigned>(id));
            return 1;
        }
    }

    std::ofstream out(options.out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        LOG_ERROR("cannot open '%s' for writing", options.out_path.c_str());
        return 1;
    }
    out.write(reinterpret_cast<const char*>(blob.data()),
              static_cast<std::streamsize>(blob.size()));
    if (!out.good()) {
        LOG_ERROR("failed while writing '%s'", options.out_path.c_str());
        return 1;
    }

    LOG_INFO("baked %zu item types into '%s' (%zu bytes)", registry.count(),
             options.out_path.c_str(), blob.size());
    return 0;
}
