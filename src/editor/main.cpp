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
//   S                   save          Esc            quit
//   F2                  switch between map mode and ITEM mode
//
// Item mode (src/editor/item_mode.hpp) edits the content database: it is what
// makes adding an item authoring instead of a C++ change plus a rebuild.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
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

namespace {

struct Options {
    std::string map_path;  // empty -> asset_root()/maps/dungeon.txt after init
    float       zoom = 1.0F;
    std::string screenshot_path;      // headless verification, like the client
    std::string content_path = "assets/content.db";
    std::string blob_path = "assets/content.bin";
    int         screenshot_frame = 2;
    /// Start in item mode. Exists so the form can be screenshotted
    /// headlessly (SDL_VIDEODRIVER=dummy delivers no keystrokes).
    bool        start_in_item_mode = false;
    bool        start_in_picker = false;
    /// "kind:id:cellx:celly" — bind a sprite and exit, no window needed.
    std::string bind_command;
};

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--map" && i + 1 < argc) {
            opt.map_path = argv[++i];
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
    Options opt = parse_args(argc, argv);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    platform::paths_init("game", "game");

    // Resolve the default map against the asset root, which in a dev build is the
    // source tree — the exact directory the client reads from. A --map argument
    // (relative to the working directory) overrides this.
    if (opt.map_path.empty()) {
        opt.map_path = platform::asset_root() + "maps/dungeon.txt";
    }

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

        client::WorldView view;
        std::optional<sim::TilePos> spawn;
        std::vector<sim::MonsterSpawn> authored_monsters;
        std::vector<sim::SpawnerSpec>  authored_spawners;
        {
            std::string error;
            const std::string text = read_file(opt.map_path);
            if (!text.empty()) {
                if (auto parsed = sim::parse_text_map(text, registry, &error)) {
                    view.map = std::move(parsed->map);
                    spawn = parsed->spawn;
                    // Kept so saving writes them back: the editor cannot place a
                    // monster yet, and a save that dropped what it could not edit
                    // would empty every map the first time a wall got moved.
                    authored_monsters = std::move(parsed->monsters);
                    authored_spawners = std::move(parsed->spawners);
                    LOG_INFO("editing '%s' (%dx%d)", opt.map_path.c_str(),
                             view.map.width(), view.map.height());
                } else {
                    LOG_ERROR("could not parse '%s': %s", opt.map_path.c_str(),
                              error.c_str());
                }
            }
            if (view.map.width() == 0) {  // blank stone canvas to start on
                view.map = sim::TileMap(48, 32, 1);
                for (int y = 0; y < view.map.height(); ++y) {
                    for (int x = 0; x < view.map.width(); ++x) {
                        view.map.set_ground(
                            sim::TilePos{static_cast<std::int16_t>(x),
                                         static_cast<std::int16_t>(y), 0},
                            sim::tiles::kStone);
                    }
                }
                LOG_INFO("no map at '%s'; starting a blank 48x32 canvas",
                         opt.map_path.c_str());
            }
        }
        view.ready = true;

        std::vector<Brush> palette = build_palette(item_rows, tileset);
        std::size_t brush_i = 0;

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

        const client::iso::ScreenPos centre = client::iso::tile_to_screen(
            static_cast<float>(view.map.width()) * 0.5F,
            static_cast<float>(view.map.height()) * 0.5F, 0);
        float camera_x = centre.x;
        float camera_y = centre.y;
        float zoom = std::clamp(opt.zoom, 0.5F, 4.0F);
        const int floor = 0;

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

        const auto update_title = [&]() {
            char title[192];
            if (item_mode_active) {
                std::snprintf(title, sizeof title,
                              "game_editor  |  ITEM MODE  |  %s  |  F2 back to map",
                              item_mode.status().c_str());
            } else {
                std::snprintf(title, sizeof title,
                              "game_editor  |  %s%s  |  brush: %s  |  "
                              "L place  R erase  Ctrl+Z undo  S save  F2 items",
                              opt.map_path.c_str(), dirty ? " *" : "",
                              palette[brush_i].label.c_str());
            }
            SDL_SetWindowTitle(window, title);
        };
        update_title();

        const Brush eraser{Brush::Kind::EraseObject, sim::kItemNone, ""};

        // Snapshot-based undo/redo: one entry per paint stroke.
        std::vector<sim::TileMap> undo_stack;
        std::vector<sim::TileMap> redo_stack;
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

            const std::string status = opt.map_path + (dirty ? " *" : "");
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

            // To the RIGHT of the last palette cell, not below it: the bar is only
            // as wide as its cells, and anything drawn under them lands behind the
            // cell quads (which sort at kUi + 200).
            const float hint_x =
                kMenuPad + static_cast<float>(palette.size()) *
                               (kMenuCell + kMenuPad) + kMenuPad;
            client::ui::text(*renderer, tileset,
                             "L place   R erase   Tab brush   Ctrl+Z undo   "
                             "S save   Esc quit",
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
                    if (!item_mode_active) {
                        item_mode.on_exit();
                        rebuild_palette();
                    }
                    update_title();
                    continue;
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
                        } else if (sc == SDL_SCANCODE_S) {
                            const std::string out =
                                sim::write_text_map(view.map, spawn,
                                                    authored_monsters,
                                                    authored_spawners);
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
            // the arrows change the focused field.
            if (!item_mode_active) {
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

            const bool over_menu =
                mouse_y >= menu_bar_top(renderer->viewport_height());

            if (!item_mode_active && (painting || erasing) &&
                params.hover_valid && !over_menu) {
                apply_brush(view.map, registry, params.hover,
                            erasing ? eraser : palette[brush_i]);
                dirty = true;
                update_title();
            }

            renderer->begin_frame(client::Color{18, 20, 26, 255});
            if (item_mode_active) {
                item_mode.draw(*renderer);
            } else {
                client::render_world(*renderer, tileset, view, params);

                // Ghost preview of the current brush under the cursor.
                if (params.hover_valid && !over_menu) {
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
