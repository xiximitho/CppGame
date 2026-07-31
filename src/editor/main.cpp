// game_editor — a small isometric map editor.
//
// Loads a text map (docs/maps.md), draws it with the same renderer and tileset
// the client uses, and lets you paint tiles with the mouse. The palette is built
// at runtime from the item catalogue and the sprites actually present in the
// atlas — so it shows exactly the object ids you can place. Save writes the map
// back out through sim::write_text_map.
//
// Controls:
//   left click / drag   place the current brush
//   right click / drag  erase the object on a tile
//   Tab or ] / [        next / previous brush
//   0..9                pick brush by index
//   arrows              pan          wheel or +/-   zoom
//   PgUp / PgDn         go up / down a floor
//   Ctrl+PgUp           add a floor on top of the map
//   S                   save          Esc            quit
//   F2                  switch between map mode and ITEM mode
//   F3                  open another map (src/editor/map_browser.hpp)
//   F4                  switch between map mode and MOB mode
//
// Item mode (src/editor/item_mode.hpp) edits the content database: it is what
// makes adding an item authoring instead of a C++ change plus a rebuild. Mob mode
// (src/editor/mob_mode.hpp) does the same for a monster class's animation, which is
// the atlas half of a mob — the numbers stay in assets/monsters.txt.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "client/iso.hpp"
#include "client/renderer2d.hpp"
#include "client/sdl_backend.hpp"
#include "client/session.hpp"
#include "client/tileset.hpp"
#include "client/ui.hpp"
#include "client/world_render.hpp"
#include "core/log.hpp"
#include "platform/paths.hpp"
#include "sim/item_type.hpp"
#include "sim/map_io.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/types.hpp"
#include "store/content.hpp"
#include "store/db.hpp"

#include "item_mode.hpp"
#include "map_browser.hpp"
#include "mob_mode.hpp"

namespace {

struct Options {
    std::string map_path;  // empty -> asset_root()/maps/dungeon.txt after init
    float       zoom = 1.0F;
    std::string screenshot_path;      // headless verification, like the client
    std::string content_path = "assets/content.db";
    std::string blob_path = "assets/content.bin";
    int         screenshot_frame = 2;
    /// Floor to open on. Also the only way to reach an upper floor headlessly.
    int         floor = 0;
    /// Palette index to start on, for the same headless reason: the brush is what
    /// the bottom bar reports on, and no keystroke can pick one under the dummy
    /// driver. The palette order is logged at startup.
    int         brush = 0;
    /// Start in item mode. Exists so the form can be screenshotted
    /// headlessly (SDL_VIDEODRIVER=dummy delivers no keystrokes).
    bool        start_in_item_mode = false;
    bool        start_in_picker = false;
    /// Start in mob mode, and with its strip picker open. Headless reach, same as
    /// the item form's.
    bool        start_in_mob_mode = false;
    bool        start_in_mob_picker = false;
    /// Monster class to open mob mode on, by id. 0 = the first one. Same reason as
    /// --brush: no keystroke can change the selection under the dummy driver.
    int         mob_class = 0;
    /// Start with the map browser open, for the same headless reason.
    bool        start_in_browser = false;
    /// "kind:id:cellx:celly" — bind a sprite and exit, no window needed.
    std::string bind_command;
    /// "appearance:cellx:celly[:dirs:frames:cellw:cellh]" — bind a mob's whole
    /// animation strip and exit.
    std::string bind_mob_command;
};

void print_usage() {
    std::printf(
        "usage: game_editor [options]\n"
        "\n"
        "  --map PATH        map to edit. A bare name ('torre'), an asset-relative\n"
        "                    path ('maps/torre.txt') or any path that exists all\n"
        "                    work; a name that does not exist starts a new map.\n"
        "                    F3 in the editor picks one from a list instead.\n"
        "  --floor N         floor to open on (default 0)\n"
        "  --brush N         palette index to start on (order is logged at start)\n"
        "  --zoom N          initial zoom (default 1)\n"
        "  --content PATH    content database (default assets/content.db)\n"
        "  --blob PATH       baked blob written on save (assets/content.bin)\n"
        "  --screenshot FILE render a few frames, write a BMP, exit\n"
        "  --item-mode       start in item mode\n"
        "  --sprite-picker   start in item mode with the sprite picker open\n"
        "  --map-browser     start with the map browser open\n"
        "  --mob-mode        start in mob mode (a class's animation)\n"
        "  --mob ID          monster class to open mob mode on\n"
        "  --mob-picker      start in mob mode with the strip picker open\n"
        "  --bind-sprite kind:id:cellx:celly   bind a sprite and exit\n"
        "  --bind-mob appearance:cellx:celly[:dirs:frames:cellw:cellh]\n"
        "                    bind a whole animation strip and exit\n"
        "  --help            this text\n");
}

Options parse_args(int argc, char** argv, bool& wants_help) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            wants_help = true;
            return opt;
        }
        if (arg == "--map" && i + 1 < argc) {
            opt.map_path = argv[++i];
        } else if (arg == "--floor" && i + 1 < argc) {
            opt.floor = std::atoi(argv[++i]);
        } else if (arg == "--brush" && i + 1 < argc) {
            opt.brush = std::atoi(argv[++i]);
        } else if (arg == "--map-browser") {
            opt.start_in_browser = true;
        } else if (arg == "--zoom" && i + 1 < argc) {
            opt.zoom = std::strtof(argv[++i], nullptr);
        } else if (arg == "--screenshot" && i + 1 < argc) {
            opt.screenshot_path = argv[++i];
        } else if (arg == "--content" && i + 1 < argc) {
            opt.content_path = argv[++i];
        } else if (arg == "--blob" && i + 1 < argc) {
            opt.blob_path = argv[++i];
        } else if (arg == "--item-mode") {
            opt.start_in_item_mode = true;
        } else if (arg == "--bind-sprite" && i + 1 < argc) {
            opt.bind_command = argv[++i];
        } else if (arg == "--sprite-picker") {
            opt.start_in_item_mode = true;
            opt.start_in_picker = true;
        } else if (arg == "--mob" && i + 1 < argc) {
            opt.mob_class = std::atoi(argv[++i]);
            opt.start_in_mob_mode = true;
        } else if (arg == "--mob-mode") {
            opt.start_in_mob_mode = true;
        } else if (arg == "--mob-picker") {
            opt.start_in_mob_mode = true;
            opt.start_in_mob_picker = true;
        } else if (arg == "--bind-mob" && i + 1 < argc) {
            opt.bind_mob_command = argv[++i];
        }
    }
    return opt;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool write_file(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << data;
    return out.good();
}

/// Turns whatever was typed after --map into a path to open. Tries it as given
/// (relative to the working directory), then against the asset root, then inside
/// the asset root's maps/ directory, with and without the .txt suffix.
///
/// Being forgiving is not cosmetic. "--map torre", "--map maps/torre.txt" and
/// "--map assets/maps/torre.txt" are all what people actually type, and the
/// behaviour for a path that does not resolve is to open a BLANK canvas — a typo
/// used to look like the editor had eaten the map.
std::string resolve_map_path(const std::string& given) {
    const std::string& root = platform::asset_root();
    if (given.empty()) {
        return root + "maps/dungeon.txt";
    }
    const std::string candidates[] = {given,        given + ".txt",
                                      root + given, root + given + ".txt",
                                      root + "maps/" + given,
                                      root + "maps/" + given + ".txt"};
    for (const std::string& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return given;  // a name that does not exist yet: a new map, saved on S
}

/// A map file's whole contents. The spawn, monsters and spawners travel with the
/// grid because saving has to write them back out: the editor cannot place them
/// yet, and a save that dropped what it could not edit would empty every map the
/// first time a wall was moved.
struct MapDoc {
    sim::TileMap                   map;
    std::optional<sim::TilePos>    spawn;
    std::vector<sim::MonsterSpawn> monsters;
    std::vector<sim::SpawnerSpec>  spawners;
};

/// A blank canvas, used when the requested file cannot be read.
sim::TileMap blank_map(int width, int height, int floors) {
    sim::TileMap map(width, height, floors);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            map.set_ground(sim::TilePos{static_cast<std::int16_t>(x),
                                        static_cast<std::int16_t>(y), 0},
                           sim::tiles::kStone);
        }
    }
    return map;
}

MapDoc load_map_doc(const std::string& path,
                    const sim::ItemTypeRegistry& registry) {
    MapDoc doc;
    std::string error;
    const std::string text = read_file(path);
    if (!text.empty()) {
        if (auto parsed = sim::parse_text_map(text, registry, &error)) {
            doc.map = std::move(parsed->map);
            doc.spawn = parsed->spawn;
            doc.monsters = std::move(parsed->monsters);
            doc.spawners = std::move(parsed->spawners);
            LOG_INFO("editing '%s' (%dx%d, %d floor(s))", path.c_str(),
                     doc.map.width(), doc.map.height(), doc.map.floors());
            return doc;
        }
        LOG_ERROR("could not parse '%s': %s", path.c_str(), error.c_str());
    }
    doc.map = blank_map(48, 32, 1);
    LOG_INFO("no map at '%s'; starting a blank 48x32 canvas", path.c_str());
    return doc;
}

/// The same map with one more empty floor on top. TileMap is a dense grid sized
/// once for a running world and has no resize, so growing one is a rebuild — which
/// is fine at authoring time and would not be inside the tick loop.
sim::TileMap with_extra_floor(const sim::TileMap& src) {
    sim::TileMap grown(src.width(), src.height(), src.floors() + 1);
    for (int z = 0; z < src.floors(); ++z) {
        for (int y = 0; y < src.height(); ++y) {
            for (int x = 0; x < src.width(); ++x) {
                const sim::TilePos pos{static_cast<std::int16_t>(x),
                                       static_cast<std::int16_t>(y),
                                       static_cast<std::int8_t>(z)};
                grown.mutable_at(pos) = src.at(pos);
            }
        }
    }
    return grown;
}

struct Brush {
    enum class Kind { Ground, Object, EraseObject, Void };
    Kind            kind = Kind::Ground;
    sim::ItemTypeId id = sim::kItemNone;
    std::string     label;
};

/// The palette: every catalogue id that has a sprite, split into ground vs object
/// by ItemFlag::Ground, followed by the two eraser brushes.
///
/// Names come from the content database, which is why this takes rows rather than
/// a registry: the simulation has no notion of an item's name, and the hardcoded
/// switch that used to live here could only ever name the seven ids someone had
/// remembered to add to it.
std::vector<Brush> build_palette(const std::vector<store::ItemRow>& rows,
                                 const client::Tileset& tileset) {
    std::vector<Brush> palette;
    char label[80];
    for (const store::ItemRow& row : rows) {
        const sim::ItemTypeId id = row.type.id;
        const char* kind = row.type.is_ground() ? "ground" : "object";
        const bool has_sprite = row.type.is_ground() ? tileset.ground(id).valid
                                                     : tileset.object(id).valid;
        if (!has_sprite) {
            continue;
        }
        std::snprintf(label, sizeof label, "%s %s (%u)", kind, row.name.c_str(),
                      static_cast<unsigned>(id));
        palette.push_back({row.type.is_ground() ? Brush::Kind::Ground
                                                : Brush::Kind::Object,
                           id, label});
    }
    palette.push_back({Brush::Kind::EraseObject, sim::kItemNone, "erase object"});
    palette.push_back({Brush::Kind::Void, sim::kItemNone, "void (hole)"});
    return palette;
}

void apply_brush(sim::TileMap& map, const sim::ItemTypeRegistry& registry,
                 sim::TilePos pos, const Brush& brush) {
    if (!map.in_bounds(pos)) {
        return;
    }
    const sim::Tile before = map.at(pos);
    sim::TileId ground = before.ground;
    sim::TileId object = before.object;
    switch (brush.kind) {
        case Brush::Kind::Ground:      ground = brush.id; break;
        case Brush::Kind::Object:      object = brush.id; break;
        case Brush::Kind::EraseObject: object = sim::kTileEmpty; break;
        case Brush::Kind::Void:
            ground = sim::kTileEmpty;
            object = sim::kTileEmpty;
            break;
    }
    map.set_ground(pos, ground);
    const bool blocking = registry.get(ground).blocks_walk() ||
                          registry.get(object).blocks_walk();
    map.set_object(pos, object, blocking);
}

// --- palette menu geometry, in window pixels ----------------------------------
constexpr float kMenuCell = 44.0F;
constexpr float kMenuPad = 6.0F;

float menu_bar_top(int viewport_h) {
    return static_cast<float>(viewport_h) - (kMenuCell + 2.0F * kMenuPad);
}

void menu_cell_rect(std::size_t index, int viewport_h, float& x, float& y) {
    x = kMenuPad + static_cast<float>(index) * (kMenuCell + kMenuPad);
    y = menu_bar_top(viewport_h) + kMenuPad;
}

/// Palette index under a window point, if the point is on a cell.
std::optional<std::size_t> menu_hit(float mx, float my, int viewport_h,
                                    std::size_t count) {
    if (my < menu_bar_top(viewport_h)) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < count; ++i) {
        float cx = 0.0F;
        float cy = 0.0F;
        menu_cell_rect(i, viewport_h, cx, cy);
        if (mx >= cx && mx < cx + kMenuCell && my >= cy &&
            my < cy + kMenuCell) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    core::log_set_tag("editor");
    // Parsed before SDL so --help works on a machine with no display, the same
    // reason the client parses twice.
    bool wants_help = false;
    Options opt = parse_args(argc, argv, wants_help);
    if (wants_help) {
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    platform::paths_init("game", "game");

    // Resolve the map against the asset root, which in a dev build is the source
    // tree — the exact directory the client reads from. An empty --map lands on the
    // default map; anything else is looked up in the working directory first, so a
    // path that already works in a shell keeps working here.
    opt.map_path = resolve_map_path(opt.map_path);

    SDL_Window* window =
        SDL_CreateWindow("game_editor", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* sdl_renderer = SDL_CreateRenderer(window, nullptr);
    if (sdl_renderer == nullptr) {
        LOG_ERROR("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    {
        auto renderer = client::make_sdl_renderer(sdl_renderer);
        client::Tileset tileset = client::Tileset::load(*renderer);

        // The editor reads the content database directly — it is a tool, not the
        // client, and it is the thing that WRITES items. The connection stays open
        // for the session because the item mode saves through it.
        std::optional<store::Db> content =
            store::open_content_db(opt.content_path);
        if (!content.has_value()) {
            LOG_ERROR("cannot open content database '%s'",
                      opt.content_path.c_str());
            SDL_DestroyRenderer(sdl_renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        std::vector<store::ItemRow> item_rows;
        sim::ItemTypeRegistry registry;
        if (!store::load_item_rows(*content, item_rows) ||
            !store::load_item_types(*content, registry)) {
            LOG_ERROR("cannot load item types from '%s'",
                      opt.content_path.c_str());
            SDL_DestroyRenderer(sdl_renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }

        // The grid lives in `view.map`, because that is the thing the renderer
        // draws; `authored` keeps what the editor cannot edit and still has to write
        // back on save. Its own `map` is empty from here on, having been moved out.
        MapDoc authored = load_map_doc(opt.map_path, registry);
        client::WorldView view;
        view.map = std::move(authored.map);
        view.ready = true;

        std::vector<Brush> palette = build_palette(item_rows, tileset);
        std::size_t brush_i = static_cast<std::size_t>(
            std::clamp(opt.brush, 0, static_cast<int>(palette.size()) - 1));

        // The item editor. Modal: while it is open, events go to it and the map is
        // not drawn, because a form over a half-visible map reads as a render bug.
        // Rebinding a sprite rewrites atlas.txt, and the tileset holds both the
        // uploaded texture and the id->rect table, so it has to be rebuilt for the
        // change to be visible. The old texture is not freed: Renderer2D has no
        // destroy_texture, so each reload leaks one 256x256 atlas. Acceptable in a
        // tool, noted in docs/pendencias.md.
        // Deliberately not overridable by a flag: Tileset::load reads atlas.txt
        // through platform::vfs from the asset root, so a writer pointed anywhere
        // else would edit one file while the editor kept showing another.
        const std::string atlas_path =
            platform::asset_root() + "tilesets/atlas.txt";
        editor::ItemMode item_mode(
            *content, tileset, window, opt.blob_path, atlas_path,
            [&]() { tileset = client::Tileset::load(*renderer); });
        bool item_mode_active = opt.start_in_item_mode;
        if (opt.start_in_picker) {
            item_mode.open_picker();
        }

        // Mob mode: the same shape as item mode, over the other half of a monster
        // class. It writes atlas.txt and nothing else — the class's numbers live in
        // assets/monsters.txt, where editing them needs no tool.
        editor::MobMode mob_mode(tileset, atlas_path,
                                 [&]() { tileset = client::Tileset::load(*renderer); });
        bool mob_mode_active = opt.start_in_mob_mode;
        if (opt.mob_class > 0) {
            mob_mode.select_class(static_cast<sim::MonsterTypeId>(opt.mob_class));
        }
        if (opt.start_in_mob_picker) {
            mob_mode.open_picker();
        }

        // Editing an item can change what the palette shows (a new object, a
        // renamed one), so the palette is rebuilt when leaving item mode rather
        // than kept as a snapshot from boot.
        const auto rebuild_palette = [&]() {
            if (store::load_item_rows(*content, item_rows)) {
                store::load_item_types(*content, registry);
                palette = build_palette(item_rows, tileset);
                brush_i = std::min(brush_i, palette.size() - 1U);
            }
        };
        LOG_INFO("palette has %zu brushes:", palette.size());
        for (std::size_t i = 0; i < palette.size(); ++i) {
            LOG_INFO("  [%zu] %s", i, palette[i].label.c_str());
        }

        float camera_x = 0.0F;
        float camera_y = 0.0F;
        float zoom = std::clamp(opt.zoom, 0.5F, 4.0F);
        int   floor = std::clamp(opt.floor, 0, view.map.floors() - 1);

        // Which floor is being edited. Everything that reads the mouse — the hover
        // tile, the brush, the ghost preview — goes through `floor`, so this one
        // variable is the whole of "edit the floor above".
        const auto recentre_camera = [&]() {
            const client::iso::ScreenPos centre = client::iso::tile_to_screen(
                static_cast<float>(view.map.width()) * 0.5F,
                static_cast<float>(view.map.height()) * 0.5F, floor);
            camera_x = centre.x;
            camera_y = centre.y;
        };
        recentre_camera();

        float mouse_x = 640.0F;  // start the cursor mid-window
        float mouse_y = 360.0F;
        std::uint64_t frames = 0;
        bool painting = false;
        bool erasing = false;
        bool dirty = false;
        bool running = true;

        if (!opt.bind_command.empty()) {
            // kind:id:cellx:celly
            std::string kind;
            int id = 0;
            int cx = 0;
            int cy = 0;
            std::istringstream parts(opt.bind_command);
            // Reported separately from the bind below: lumping "you typed it wrong"
            // together with "the bind failed" sends you looking in the wrong place.
            const bool parsed = static_cast<bool>(std::getline(parts, kind, ':')) &&
                                static_cast<bool>(parts >> id) &&
                                static_cast<bool>(parts.ignore(1) >> cx) &&
                                static_cast<bool>(parts.ignore(1) >> cy);
            if (!parsed) {
                LOG_ERROR("cannot parse --bind-sprite '%s' "
                          "(expected kind:id:cellx:celly)",
                          opt.bind_command.c_str());
            } else if (!item_mode.bind_from_command(
                           static_cast<sim::ItemTypeId>(id), kind, cx, cy)) {
                // The specific reason was already logged by bind_from_command.
                LOG_ERROR("could not bind %s %d to cell %d,%d", kind.c_str(), id,
                          cx, cy);
            } else {
                LOG_INFO("bound %s %d to cell %d,%d", kind.c_str(), id, cx, cy);
            }
            running = false;
        }

        if (!opt.bind_mob_command.empty()) {
            // appearance:cellx:celly[:dirs:frames:cellw:cellh] — the tail is optional
            // because 4 directions of 3 frames in 32x32 cells is what the sheets in
            // assets/tibia_like are, and spelling it out every time invites a typo in
            // the part that is almost always the same.
            int appearance = 0;
            int cx = 0;
            int cy = 0;
            int dirs = 4;
            int cycle = 3;  // not `frames`: the render loop's frame counter is that
            int cell_w = 32;
            int cell_h = 32;
            std::istringstream parts(opt.bind_mob_command);
            const bool parsed = static_cast<bool>(parts >> appearance) &&
                                static_cast<bool>(parts.ignore(1) >> cx) &&
                                static_cast<bool>(parts.ignore(1) >> cy);
            if (parsed) {
                // Each optional field only counts if the one before it was there.
                if (parts.ignore(1) >> dirs && parts.ignore(1) >> cycle) {
                    if (!(parts.ignore(1) >> cell_w && parts.ignore(1) >> cell_h)) {
                        cell_w = 32;
                        cell_h = 32;
                    }
                }
            }
            if (!parsed) {
                LOG_ERROR("cannot parse --bind-mob '%s' (expected "
                          "appearance:cellx:celly[:dirs:frames:cellw:cellh])",
                          opt.bind_mob_command.c_str());
            } else if (!mob_mode.bind_from_command(
                           static_cast<std::uint16_t>(appearance), cx, cy, dirs,
                           cycle, cell_w, cell_h)) {
                LOG_ERROR("could not bind appearance %d", appearance);
            } else {
                LOG_INFO("bound appearance %d to cell %d,%d (%d dirs, %d frames, "
                         "%dx%d)", appearance, cx, cy, dirs, cycle, cell_w, cell_h);
            }
            running = false;
        }

        const auto update_title = [&]() {
            char title[256];
            if (item_mode_active) {
                std::snprintf(title, sizeof title,
                              "game_editor  |  ITEM MODE  |  %s  |  F2 back to map",
                              item_mode.status().c_str());
            } else if (mob_mode_active) {
                std::snprintf(title, sizeof title,
                              "game_editor  |  MOB MODE  |  %s  |  F4 back to map",
                              mob_mode.status().c_str());
            } else {
                std::snprintf(title, sizeof title,
                              "game_editor  |  %s%s  |  floor %d/%d  |  brush: %s"
                              "  |  L place  R erase  PgUp/PgDn floor  S save  "
                              "F2 items  F3 open  F4 mobs",
                              opt.map_path.c_str(), dirty ? " *" : "", floor,
                              view.map.floors() - 1,
                              palette[brush_i].label.c_str());
            }
            SDL_SetWindowTitle(window, title);
        };
        update_title();

        // The map browser. Opening a map replaces everything the editor holds about
        // the one before it, undo history included: an undo stack whose snapshots
        // belong to another file is worse than no undo stack.
        editor::MapBrowser browser(tileset);
        std::vector<sim::TileMap> undo_stack;
        std::vector<sim::TileMap> redo_stack;
        const auto open_map = [&](const std::string& path) {
            MapDoc loaded = load_map_doc(path, registry);
            view.map = std::move(loaded.map);
            authored.spawn = loaded.spawn;
            authored.monsters = std::move(loaded.monsters);
            authored.spawners = std::move(loaded.spawners);
            opt.map_path = path;
            undo_stack.clear();
            redo_stack.clear();
            floor = 0;
            dirty = false;
            recentre_camera();
            update_title();
        };

        /// Directory the browser lists: the one the current map lives in, so
        /// --map pointing somewhere else keeps browsing there.
        const auto maps_dir = [&]() {
            const std::filesystem::path parent =
                std::filesystem::path(opt.map_path).parent_path();
            return parent.empty() ? platform::asset_root() + "maps"
                                  : parent.string();
        };
        const auto current_file = [&]() {
            return std::filesystem::path(opt.map_path).filename().string();
        };
        if (opt.start_in_browser) {
            browser.open(maps_dir(), current_file(), false);
        }

        const Brush eraser{Brush::Kind::EraseObject, sim::kItemNone, ""};

        // Snapshot-based undo/redo: one entry per paint stroke. Declared with the
        // browser above, which clears both when another map is opened.
        constexpr std::size_t kMaxUndo = 64;
        const auto push_undo = [&]() {
            undo_stack.push_back(view.map);
            if (undo_stack.size() > kMaxUndo) {
                undo_stack.erase(undo_stack.begin());
            }
            redo_stack.clear();
        };

        // Draws an atlas region at a fixed window rectangle regardless of the
        // camera. client::ui does the camera inversion; the argument order here is
        // kept as it was so the menu code below reads unchanged.
        const auto submit_screen = [&](float sx, float sy, float sw, float sh,
                                       const client::AtlasEntry& entry,
                                       client::Color tint, float depth) {
            client::ui::sprite(*renderer, tileset, entry, sx, sy, sw, sh, tint,
                               depth);
        };

        // The palette menu along the bottom. Depths sit above every world tile
        // (see iso::depth_key) with steps of 100 so float keeps them ordered.
        const auto draw_menu = [&]() {
            const int vw = renderer->viewport_width();
            const int vh = renderer->viewport_height();
            const client::AtlasEntry& solid = tileset.solid();
            constexpr float kUi = client::ui::kDepth;

            submit_screen(0.0F, menu_bar_top(vh), static_cast<float>(vw),
                          kMenuCell + 2.0F * kMenuPad, solid,
                          client::Color{18, 20, 26, 235}, kUi);

            for (std::size_t i = 0; i < palette.size(); ++i) {
                float cx = 0.0F;
                float cy = 0.0F;
                menu_cell_rect(i, vh, cx, cy);
                if (i == brush_i) {  // gold frame behind the selected cell
                    submit_screen(cx - 3.0F, cy - 3.0F, kMenuCell + 6.0F,
                                  kMenuCell + 6.0F, solid,
                                  client::Color{201, 162, 39, 255}, kUi + 100.0F);
                }
                submit_screen(cx, cy, kMenuCell, kMenuCell, solid,
                              client::Color{45, 48, 56, 255}, kUi + 200.0F);

                const Brush& brush = palette[i];
                const client::AtlasEntry* entry = nullptr;
                if (brush.kind == Brush::Kind::Ground) {
                    entry = &tileset.ground(brush.id);
                } else if (brush.kind == Brush::Kind::Object) {
                    entry = &tileset.object(brush.id);
                }
                if (entry != nullptr && entry->valid) {
                    const float scale = std::min(kMenuCell / entry->width,
                                                 kMenuCell / entry->height);
                    const float dw = entry->width * scale;
                    const float dh = entry->height * scale;
                    submit_screen(cx + (kMenuCell - dw) * 0.5F,
                                  cy + (kMenuCell - dh) * 0.5F, dw, dh, *entry,
                                  client::Color{255, 255, 255, 255}, kUi + 300.0F);
                } else {  // eraser / void: a coloured chip, no sprite
                    const client::Color chip =
                        brush.kind == Brush::Kind::EraseObject
                            ? client::Color{150, 50, 45, 255}
                            : client::Color{8, 8, 10, 255};
                    submit_screen(cx + 8.0F, cy + 8.0F, kMenuCell - 16.0F,
                                  kMenuCell - 16.0F, solid, chip, kUi + 300.0F);
                }
            }

            // Labels. These used to live in the window title, which meant the
            // brush you were painting with was readable everywhere except the
            // window you were painting in.
            const client::Color dim{150, 156, 170, 255};
            const client::Color bright{236, 240, 248, 255};

            char floor_text[48];
            std::snprintf(floor_text, sizeof floor_text, "  floor %d/%d", floor,
                          view.map.floors() - 1);
            const std::string status =
                opt.map_path + (dirty ? " *" : "") + floor_text;
            client::ui::fill(*renderer, tileset, 0.0F, 0.0F,
                             client::ui::text_width(tileset, status, 2.0F) +
                                 2.0F * kMenuPad,
                             client::ui::text_height(tileset, 2.0F) +
                                 2.0F * kMenuPad,
                             client::Color{18, 20, 26, 200}, kUi);
            client::ui::text(*renderer, tileset, status, kMenuPad, kMenuPad,
                             bright, 2.0F);

            client::ui::text(*renderer, tileset, palette[brush_i].label,
                             kMenuPad, menu_bar_top(vh) - 18.0F, bright, 2.0F);

            // A stair with no floor to lead to is the one authoring mistake that
            // fails in total silence at runtime: World::apply_stairs refuses when
            // the destination is out of bounds, and a refused stair looks exactly
            // like a stair that does not work. Say it while it is being painted.
            const int stair_delta =
                registry.get(palette[brush_i].id).stair_delta_z();
            const int destination = floor + stair_delta;
            if (stair_delta != 0 &&
                (destination < 0 || destination >= view.map.floors())) {
                client::ui::text(
                    *renderer, tileset,
                    stair_delta > 0
                        ? "this stair leads nowhere: Ctrl+PgUp adds a floor above"
                        : "this stair leads nowhere: there is no floor below",
                    kMenuPad, menu_bar_top(vh) - 40.0F,
                    client::Color{226, 132, 60, 255}, 2.0F);
            }

            // To the RIGHT of the last palette cell, not below it: the bar is only
            // as wide as its cells, and anything drawn under them lands behind the
            // cell quads (which sort at kUi + 200).
            const float hint_x =
                kMenuPad + static_cast<float>(palette.size()) *
                               (kMenuCell + kMenuPad) + kMenuPad;
            client::ui::text(*renderer, tileset,
                             "L place   R erase   Tab brush   Ctrl+Z undo   "
                             "PgUp/PgDn floor   Ctrl+PgUp add floor   "
                             "S save   F3 open   Esc quit",
                             hint_x,
                             menu_bar_top(vh) + kMenuPad +
                                 (kMenuCell - client::ui::text_height(tileset)) *
                                     0.5F,
                             dim, 1.0F, kUi + 400.0F);
        };

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                // F2 owns the mode switch in both directions and is checked before
                // anything else, so it cannot be swallowed by a captured keyboard.
                if (event.type == SDL_EVENT_KEY_DOWN &&
                    event.key.key == SDLK_F2) {
                    item_mode_active = !item_mode_active;
                    if (item_mode_active) {
                        mob_mode_active = false;  // one modal at a time
                    } else {
                        item_mode.on_exit();
                        rebuild_palette();
                    }
                    update_title();
                    continue;
                }
                // F4 does the same for mob mode. Checked here, next to F2, for the
                // same reason: a mode switch that a captured keyboard can swallow is
                // a mode you get stuck in.
                if (event.type == SDL_EVENT_KEY_DOWN &&
                    event.key.key == SDLK_F4) {
                    mob_mode_active = !mob_mode_active;
                    if (mob_mode_active) {
                        item_mode_active = false;
                    }
                    update_title();
                    continue;
                }
                // F3 opens the map browser, and closes it again: a key that only
                // opens a modal leaves the only way out being Esc, which also means
                // "quit" here.
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F3 &&
                    !item_mode_active && !mob_mode_active) {
                    if (browser.active()) {
                        browser.close();
                    } else {
                        browser.open(maps_dir(), current_file(), dirty);
                        // The button-up that ends a stroke goes to the browser, so
                        // a stroke in flight has to be ended here or it resumes
                        // painting the moment the list closes.
                        painting = false;
                        erasing = false;
                    }
                    continue;
                }
                if (browser.active() && event.type != SDL_EVENT_QUIT) {
                    if (browser.handle_event(event, *renderer)) {
                        if (auto chosen = browser.take_choice()) {
                            open_map(*chosen);
                        }
                        continue;
                    }
                }
                if (item_mode_active && event.type != SDL_EVENT_QUIT) {
                    if (item_mode.handle_event(event, *renderer)) {
                        update_title();
                        continue;
                    }
                    // Unconsumed keys still must not reach the map: painting while a
                    // form is open would edit the map behind it invisibly.
                    if (event.type == SDL_EVENT_KEY_DOWN ||
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                        event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                        continue;
                    }
                }
                if (mob_mode_active && event.type != SDL_EVENT_QUIT) {
                    if (mob_mode.handle_event(event, *renderer)) {
                        update_title();
                        continue;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN ||
                        event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                        event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                        continue;
                    }
                }
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        running = false;
                        break;
                    case SDL_EVENT_MOUSE_MOTION:
                        mouse_x = event.motion.x;
                        mouse_y = event.motion.y;
                        break;
                    case SDL_EVENT_MOUSE_WHEEL:
                        zoom = std::clamp(
                            zoom * (event.wheel.y > 0.0F ? 1.1F : 0.9F), 0.5F,
                            4.0F);
                        break;
                    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                        const int vh = renderer->viewport_height();
                        const auto cell =
                            menu_hit(mouse_x, mouse_y, vh, palette.size());
                        const bool on_bar = mouse_y >= menu_bar_top(vh);
                        if (event.button.button == SDL_BUTTON_LEFT) {
                            if (cell.has_value()) {
                                brush_i = *cell;  // pick from the menu
                                update_title();
                            } else if (!on_bar) {
                                push_undo();  // start a paint stroke
                                painting = true;
                            }
                        } else if (event.button.button == SDL_BUTTON_RIGHT &&
                                   !on_bar) {
                            push_undo();
                            erasing = true;
                        }
                        break;
                    }
                    case SDL_EVENT_MOUSE_BUTTON_UP:
                        if (event.button.button == SDL_BUTTON_LEFT) {
                            painting = false;
                        } else if (event.button.button == SDL_BUTTON_RIGHT) {
                            erasing = false;
                        }
                        break;
                    case SDL_EVENT_KEY_DOWN: {
                        const SDL_Keycode key = event.key.key;
                        const SDL_Scancode sc = event.key.scancode;
                        const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
                        if (ctrl && sc == SDL_SCANCODE_Z) {
                            if (!undo_stack.empty()) {
                                redo_stack.push_back(view.map);
                                view.map = std::move(undo_stack.back());
                                undo_stack.pop_back();
                                dirty = true;
                                update_title();
                            }
                        } else if (ctrl && sc == SDL_SCANCODE_Y) {
                            if (!redo_stack.empty()) {
                                undo_stack.push_back(view.map);
                                view.map = std::move(redo_stack.back());
                                redo_stack.pop_back();
                                dirty = true;
                                update_title();
                            }
                        } else if (key == SDLK_ESCAPE) {
                            running = false;
                        } else if (ctrl && key == SDLK_PAGEUP) {
                            // Adding a floor is what makes a stair authorable: a
                            // stair painted on the top floor of a one-floor map
                            // refuses in silence, because there is nowhere to go.
                            push_undo();
                            view.map = with_extra_floor(view.map);
                            floor = view.map.floors() - 1;
                            camera_y -= static_cast<float>(
                                client::iso::kFloorHeight);
                            dirty = true;
                            LOG_INFO("added floor %d (map is now %d floor(s))",
                                     floor, view.map.floors());
                            update_title();
                        } else if (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) {
                            const int step = key == SDLK_PAGEUP ? 1 : -1;
                            const int next = std::clamp(floor + step, 0,
                                                        view.map.floors() - 1);
                            if (next != floor) {
                                // The camera follows the projection: a floor is
                                // kFloorHeight higher on screen, and without this
                                // the map appears to jump away under the cursor.
                                camera_y -= static_cast<float>(
                                    (next - floor) * client::iso::kFloorHeight);
                                floor = next;
                                update_title();
                            }
                        } else if (sc == SDL_SCANCODE_S) {
                            const std::string out =
                                sim::write_text_map(view.map, authored.spawn,
                                                    authored.monsters,
                                                    authored.spawners);
                            if (write_file(opt.map_path, out)) {
                                LOG_INFO("saved '%s'", opt.map_path.c_str());
                                dirty = false;
                            } else {
                                LOG_ERROR("could not write '%s'",
                                          opt.map_path.c_str());
                            }
                            update_title();
                        } else if (sc == SDL_SCANCODE_TAB ||
                                   sc == SDL_SCANCODE_RIGHTBRACKET) {
                            brush_i = (brush_i + 1) % palette.size();
                            update_title();
                        } else if (sc == SDL_SCANCODE_LEFTBRACKET) {
                            brush_i =
                                (brush_i + palette.size() - 1) % palette.size();
                            update_title();
                        } else if (key == SDLK_EQUALS || key == SDLK_KP_PLUS) {
                            zoom = std::clamp(zoom * 1.25F, 0.5F, 4.0F);
                        } else if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
                            zoom = std::clamp(zoom * 0.8F, 0.5F, 4.0F);
                        } else if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
                            const auto idx = static_cast<std::size_t>(
                                sc - SDL_SCANCODE_1);
                            if (idx < palette.size()) {
                                brush_i = idx;
                                update_title();
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            // Held-key panning reads the keyboard state directly, so it bypasses the
            // event routing above and has to be suppressed explicitly: in item mode
            // the arrows change the focused field, and in the browser they move the
            // selection.
            if (!item_mode_active && !mob_mode_active && !browser.active()) {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                const float pan = 12.0F / zoom;
                if (keys[SDL_SCANCODE_LEFT]) {
                    camera_x -= pan;
                }
                if (keys[SDL_SCANCODE_RIGHT]) {
                    camera_x += pan;
                }
                if (keys[SDL_SCANCODE_UP]) {
                    camera_y -= pan;
                }
                if (keys[SDL_SCANCODE_DOWN]) {
                    camera_y += pan;
                }
            }

            renderer->set_camera(camera_x, camera_y, zoom);

            client::RenderParams params;
            float world_x = 0.0F;
            float world_y = 0.0F;
            renderer->window_to_world(mouse_x, mouse_y, world_x, world_y);
            params.hover = client::iso::screen_to_tile(world_x, world_y, floor);
            params.hover_valid = view.map.in_bounds(params.hover);
            // The editor has no actor to derive a floor from, and it is editing one
            // specific floor: it says so rather than letting the renderer guess.
            params.floor_override = floor;

            const bool over_menu =
                mouse_y >= menu_bar_top(renderer->viewport_height());

            if (!item_mode_active && !mob_mode_active && !browser.active() &&
                (painting || erasing) && params.hover_valid && !over_menu) {
                apply_brush(view.map, registry, params.hover,
                            erasing ? eraser : palette[brush_i]);
                dirty = true;
                update_title();
            }

            renderer->begin_frame(client::Color{18, 20, 26, 255});
            if (item_mode_active) {
                item_mode.draw(*renderer);
            } else if (mob_mode_active) {
                mob_mode.draw(*renderer);
            } else {
                client::render_world(*renderer, tileset, view, params);

                // Ghost preview of the current brush under the cursor. Suppressed
                // under the browser: it sorts above every UI quad on purpose, so it
                // would float on top of the modal list.
                if (params.hover_valid && !over_menu && !browser.active()) {
                    const Brush& brush = palette[brush_i];
                    const client::AtlasEntry* entry = nullptr;
                    if (brush.kind == Brush::Kind::Ground) {
                        entry = &tileset.ground(brush.id);
                    } else if (brush.kind == Brush::Kind::Object) {
                        entry = &tileset.object(brush.id);
                    }
                    if (entry != nullptr && entry->valid) {
                        const client::iso::ScreenPos at =
                            client::iso::tile_to_screen(params.hover);
                        client::SpriteCmd cmd;
                        cmd.texture = tileset.texture();
                        cmd.dst = client::Rect{at.x + entry->origin_x,
                                               at.y + entry->origin_y,
                                               entry->width, entry->height};
                        cmd.uv = entry->uv;
                        cmd.depth = 1.0e9F;  // always on top
                        cmd.tint = client::Color{255, 255, 255, 150};
                        renderer->submit(cmd);
                    }
                }

                draw_menu();
                browser.draw(*renderer);
            }

            renderer->end_frame();
            ++frames;

            if (!opt.screenshot_path.empty() &&
                frames >= static_cast<std::uint64_t>(opt.screenshot_frame)) {
                SDL_Surface* shot = SDL_RenderReadPixels(sdl_renderer, nullptr);
                if (shot != nullptr) {
                    if (SDL_SaveBMP(shot, opt.screenshot_path.c_str())) {
                        // Draw calls go with it: headless is the only place this
                        // is observable (the client puts it in the window title),
                        // and one call for the whole scene + UI + text is the
                        // batching invariant worth noticing when it breaks.
                        LOG_INFO("wrote screenshot to '%s' (%d draw calls)",
                                 opt.screenshot_path.c_str(),
                                 renderer->last_draw_calls());
                    } else {
                        LOG_ERROR("SDL_SaveBMP failed: %s", SDL_GetError());
                    }
                    SDL_DestroySurface(shot);
                }
                running = false;
            }
        }
    }

    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
