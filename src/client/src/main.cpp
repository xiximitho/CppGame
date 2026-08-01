#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "client/battle_list.hpp"
#include "client/content.hpp"
#include "client/corpse_loot_ui.hpp"
#include "client/damage_feed.hpp"
#include "client/effect_feed.hpp"
#include "client/input.hpp"
#include "client/inventory_ui.hpp"
#include "client/iso.hpp"
#include "client/sdl_backend.hpp"
#include "client/session.hpp"
#include "client/spell_hotbar_ui.hpp"
#include "client/tileset.hpp"
#include "client/world_render.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "net/protocol.hpp"
#include "platform/paths.hpp"
#include "platform/vfs.hpp"
#include "sim/spell.hpp"
#include "sim/vocation_type.hpp"

#include <optional>

namespace {

struct Options {
    bool          solo = true;
    std::string   host = "127.0.0.1";
    std::uint16_t port = net::kDefaultPort;
    std::string   name = "player";
    std::uint64_t seed = 1337;
    /// Extra random mobs on top of whatever the map authors. Zero by default: maps
    /// carry their own monsters and spawn points now.
    int           wanderers = 0;
    /// Asset-relative, resolved through the VFS: on Android the map lives inside
    /// the package and is not a file. Solo only; in network play the server owns
    /// the map and sends it.
    std::string   map_path = "maps/dungeon.txt";
    int           window_width = 1280;
    int           window_height = 720;
    float         zoom = 2.0F;

    /// When set, the client renders `screenshot_frame` frames, writes a BMP and
    /// exits. Works with SDL_VIDEODRIVER=dummy, so it runs on a machine with no
    /// display: that makes it usable for visual checks in CI and for asking
    /// someone to send you what they actually see.
    std::string screenshot_path;
    int         screenshot_frame = 45;
    /// Class kit for solo (and the client's idea of vocation for the hotbar in
    /// remote play until mana/vocation ride the snapshot).
    sim::VocationId vocation = sim::vocations::kKnight;
};

void print_usage() {
    std::printf(
        "usage: game_client [options]\n"
        "\n"
        "  --solo               run the simulation in-process (default)\n"
        "  --connect HOST[:PORT]  join a server (default port %u)\n"
        "  --name NAME          player name sent to the server\n"
        "  --seed N             solo world seed\n"
        "  --wanderers N        extra random mobs on top of the map's (default 0)\n"
        "  --map PATH           solo map. A bare name ('torre'), an asset-relative\n"
        "                       path ('maps/torre.txt') or a full path inside the\n"
        "                       asset tree all work (default maps/dungeon.txt);\n"
        "                       falls back to the seeded generated map when it\n"
        "                       cannot be read\n"
        "  --zoom N             initial zoom (default 2)\n"
        "  --screenshot FILE    render a few frames, write a BMP, exit\n"
        "  --screenshot-frame N  which frame to capture (default 45)\n"
        "  --vocation NAME      class (knight/paladin/mage/druid; default knight)\n"
        "  --help               this text\n"
        "\n"
        "controls:\n"
        "  WASD / arrows        walk\n"
        "  left click / touch   step toward the tile; bag opens loot panel\n"
        "  click item in loot   take into backpack\n"
        "  1                    cast vocation spell (mage needs a target)\n"
        "  mouse wheel, +/-     zoom\n"
        "  F2                   toggle key scheme (screen-relative / grid)\n"
        "  Esc                  quit\n",
        static_cast<unsigned>(net::kDefaultPort));
}

/// Splits "host" or "host:port". IPv6 literals are not handled; ENet is IPv4 only.
void parse_endpoint(const std::string& text, Options& options) {
    const std::size_t colon = text.rfind(':');
    if (colon == std::string::npos) {
        options.host = text;
        return;
    }
    options.host = text.substr(0, colon);
    const int parsed = std::atoi(text.c_str() + colon + 1);
    if (parsed > 0 && parsed < 65536) {
        options.port = static_cast<std::uint16_t>(parsed);
    }
}

bool parse_args(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = (i + 1) < argc;

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return false;
        }
        if (arg == "--solo") {
            options.solo = true;
        } else if (arg == "--connect" && has_value) {
            options.solo = false;
            parse_endpoint(argv[++i], options);
        } else if (arg == "--name" && has_value) {
            options.name = argv[++i];
        } else if (arg == "--seed" && has_value) {
            options.seed = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--wanderers" && has_value) {
            options.wanderers = std::atoi(argv[++i]);
        } else if (arg == "--map" && has_value) {
            options.map_path = argv[++i];
        } else if (arg == "--zoom" && has_value) {
            options.zoom = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--screenshot" && has_value) {
            options.screenshot_path = argv[++i];
        } else if (arg == "--screenshot-frame" && has_value) {
            options.screenshot_frame = std::atoi(argv[++i]);
        } else if (arg == "--vocation" && has_value) {
            const sim::VocationId parsed = sim::parse_vocation_token(argv[++i]);
            if (parsed == sim::kVocationNone) {
                LOG_WARN("unknown --vocation '%s'; keeping knight", argv[i]);
            } else {
                options.vocation = parsed;
            }
        } else {
            LOG_WARN("ignoring unknown argument '%s'", arg.c_str());
        }
    }
    return true;
}

/// Optional key=value overrides, read through the VFS so this works identically
/// from an APK on Android and from a directory on desktop.
void apply_config_file(Options& options) {
    std::string text;
    if (!platform::vfs::read_asset_text("client.cfg", text)) {
        return;
    }

    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t end = text.find('\n', offset);
        const std::string line =
            text.substr(offset, end == std::string::npos ? std::string::npos
                                                         : end - offset);
        offset = (end == std::string::npos) ? text.size() : end + 1;

        const std::size_t equals = line.find('=');
        if (line.empty() || line[0] == '#' || equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        if (key == "host") {
            parse_endpoint(value, options);
        } else if (key == "name") {
            options.name = value;
        } else if (key == "map") {
            options.map_path = value;
        } else if (key == "zoom") {
            options.zoom = static_cast<float>(std::atof(value.c_str()));
        } else if (key == "width") {
            options.window_width = std::atoi(value.c_str());
        } else if (key == "height") {
            options.window_height = std::atoi(value.c_str());
        }
    }
    LOG_INFO("applied overrides from client.cfg");
}

/// Turns whatever was passed to --map into a path the VFS can read.
///
/// Asset paths are relative to the asset root — on Android they are not files at
/// all — but what people type is "torre", or the full path the editor printed. Both
/// used to land on "no map; using generated map", which looks like the map argument
/// was ignored rather than misspelt. Probing is a read of a few KB, once, at start.
std::string resolve_map_asset(const std::string& given) {
    if (given.empty()) {
        return given;
    }

    std::vector<std::string> candidates;
    candidates.push_back(given);
    // A full path inside the asset tree: keep only the part below the root, which
    // is the only form read_asset understands.
    const std::string& root = platform::asset_root();
    if (!root.empty() && given.size() > root.size() &&
        given.compare(0, root.size(), root) == 0) {
        candidates.push_back(given.substr(root.size()));
    }
    // A path relative to the working directory, "assets/maps/x.txt" — the form the
    // server takes, and the one a shell tab-completes. Derived from the root's own
    // last component rather than hardcoding "assets", so it follows if that is ever
    // renamed. Empty on Android, where there is no directory to strip.
    const std::size_t last = root.find_last_not_of('/');
    if (last != std::string::npos) {
        const std::size_t slash = root.find_last_of('/', last);
        const std::size_t from = slash == std::string::npos ? 0 : slash + 1;
        const std::string name = root.substr(from, last + 1 - from);
        if (!name.empty() && given.compare(0, name.size() + 1, name + "/") == 0) {
            candidates.push_back(given.substr(name.size() + 1));
        }
    }
    if (given.find('/') == std::string::npos) {
        candidates.push_back("maps/" + given);
    }
    // Same list again with the extension, rather than guessing whether a name that
    // happens to contain a dot already has one. The bound is read once: pushing
    // into the vector being iterated is how this loop would never end.
    const std::size_t bare = candidates.size();
    for (std::size_t i = 0; i < bare; ++i) {
        candidates.push_back(candidates[i] + ".txt");
    }

    std::string text;
    for (const std::string& candidate : candidates) {
        if (platform::vfs::read_asset_text(candidate, text)) {
            if (candidate != given) {
                LOG_INFO("--map '%s' resolved to asset '%s'", given.c_str(),
                         candidate.c_str());
            }
            return candidate;
        }
    }
    return given;  // build_solo_world logs the fallback to the generated map
}

}  // namespace

int main(int argc, char** argv) {
    core::log_set_tag("client");

    Options options;
    // Parsed once up front so --help and a bad argument are handled before SDL is
    // touched: asking for usage must work on a machine with no display.
    if (!parse_args(argc, argv, options)) {
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // The config file needs the VFS, which needs SDL. Arguments are then re-applied
    // on top so the command line wins over the file, which is the precedence
    // everyone expects.
    platform::paths_init("game", "game");
    apply_config_file(options);
    parse_args(argc, argv, options);
    // After both, so it also fixes up a map named in client.cfg.
    options.map_path = resolve_map_asset(options.map_path);

    SDL_Window* window = SDL_CreateWindow(
        "Isometric Prototype", options.window_width, options.window_height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
    SDL_SetRenderVSync(sdl_renderer, 1);
    LOG_INFO("render backend: %s", SDL_GetRendererName(sdl_renderer));

    {
        auto renderer = client::make_sdl_renderer(sdl_renderer);
        const client::Tileset tileset = client::Tileset::load(*renderer);
        if (!tileset.texture().valid()) {
            LOG_ERROR("could not build the tile atlas");
            return 1;
        }

        std::unique_ptr<client::Session> session;
        if (options.solo) {
            session = client::make_solo_session(options.seed, options.wanderers,
                                                options.map_path,
                                                options.vocation);
        } else {
            session = client::make_remote_session(options.host, options.port,
                                                  options.name);
        }
        if (session == nullptr) {
            LOG_ERROR("could not create session");
            return 1;
        }

        auto  scheme = client::input::Scheme::ScreenRelative;
        float zoom = options.zoom;
        float camera_x = 0.0F;
        float camera_y = 0.0F;
        bool  camera_initialised = false;

        float mouse_x = 0.0F;
        float mouse_y = 0.0F;

        bool          running = true;
        std::uint64_t frames = 0;
        client::DamageFeed damage_feed;
        client::EffectFeed effect_feed;
        bool show_inventory = false;
        // Tile of the loot bag currently open in the left panel; nullopt = closed.
        std::optional<sim::TilePos> open_loot;
        // Class names for the battle list. Presentation only: in network play the
        // server owns the numbers, so a stale local file shows a wrong NAME and
        // never a wrong fight.
        const sim::MonsterRegistry monster_types = client::load_monster_catalogue();
        // What the player last clicked to fight. Kept client-side because the
        // snapshot does not carry "who am I attacking" — it is a UI highlight, and
        // the simulation is the one that actually holds the target.
        sim::NetId ui_target = sim::kInvalidNetId;
        const std::uint64_t start_nanos = core::now_nanos();
        std::uint64_t last_title_update = core::now_nanos();
        double        fps = 0.0;
        std::uint64_t fps_window_start = last_title_update;
        std::uint64_t fps_window_frames = 0;

        while (running) {
            renderer->set_camera(camera_x, camera_y, zoom);

            const int floor = client::local_floor(session->view());
            bool click_requested = false;

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                        running = false;
                        break;

                    case SDL_EVENT_KEY_DOWN:
                        if (event.key.key == SDLK_ESCAPE) {
                            running = false;
                        } else if (event.key.key == SDLK_F2) {
                            scheme = (scheme == client::input::Scheme::ScreenRelative)
                                         ? client::input::Scheme::GridAligned
                                         : client::input::Scheme::ScreenRelative;
                            LOG_INFO("key scheme: %s",
                                     scheme == client::input::Scheme::ScreenRelative
                                         ? "screen-relative"
                                         : "grid-aligned");
                        } else if (event.key.key == SDLK_EQUALS ||
                                   event.key.key == SDLK_KP_PLUS) {
                            zoom = SDL_min(zoom * 1.25F, 8.0F);
                        } else if (event.key.key == SDLK_MINUS ||
                                   event.key.key == SDLK_KP_MINUS) {
                            zoom = SDL_max(zoom / 1.25F, 0.5F);
                        } else if (event.key.scancode == SDL_SCANCODE_I) {
                            show_inventory = !show_inventory;
                        } else if (event.key.scancode == SDL_SCANCODE_1 ||
                                   event.key.scancode == SDL_SCANCODE_KP_1) {
                            const sim::VocationId voc =
                                session->view().vocation != sim::kVocationNone
                                    ? session->view().vocation
                                    : options.vocation;
                            const auto spells = sim::spells_for_vocation(voc);
                            if (!spells.empty()) {
                                const sim::SpellDef& spell = spells[0];
                                const sim::NetId target =
                                    spell.kind == sim::SpellKind::Damage
                                        ? ui_target
                                        : sim::kInvalidNetId;
                                session->request_cast_spell(spell.id, target);
                            }
                        }
                        break;

                    case SDL_EVENT_MOUSE_MOTION:
                        mouse_x = event.motion.x;
                        mouse_y = event.motion.y;
                        break;

                    case SDL_EVENT_MOUSE_WHEEL:
                        zoom = SDL_clamp(zoom * (event.wheel.y > 0 ? 1.15F : 0.87F),
                                         0.5F, 8.0F);
                        break;

                    case SDL_EVENT_MOUSE_BUTTON_DOWN:
                        if (event.button.button == SDL_BUTTON_LEFT) {
                            click_requested = true;
                        }
                        break;

                    case SDL_EVENT_FINGER_DOWN: {
                        // Touch coordinates are normalised, unlike mouse ones.
                        int win_w = 0;
                        int win_h = 0;
                        SDL_GetWindowSize(window, &win_w, &win_h);
                        mouse_x = event.tfinger.x * static_cast<float>(win_w);
                        mouse_y = event.tfinger.y * static_cast<float>(win_h);
                        click_requested = true;
                        break;
                    }

                    default:
                        break;
                }
            }

            // Resolved after the event loop so a click uses the cursor position
            // from this frame rather than the previous one.
            client::RenderParams params;
            {
                float world_x = 0.0F;
                float world_y = 0.0F;
                renderer->window_to_world(mouse_x, mouse_y, world_x, world_y);
                params.hover = client::iso::screen_to_tile(world_x, world_y, floor);
                params.hover_valid = session->view().map.in_bounds(params.hover);
            }
            // Where the battle list starts: under the inventory when that is open,
            // at the top of the screen otherwise. Passed in rather than the panel
            // knowing about the inventory, so neither has to know the other exists.
            const float battle_top = show_inventory ? 300.0F : 24.0F;

            // A click goes to the open loot panel first, then the inventory, then
            // the battle list, then the world. Clicking a bag opens its panel;
            // clicking an item in that panel takes it into the backpack.
            if (click_requested) {
                bool consumed = false;
                const client::WorldView& view = session->view();

                const client::CorpseView* open_corpse = nullptr;
                if (open_loot.has_value()) {
                    for (const client::CorpseView& corpse : view.corpses) {
                        if (corpse.tile == *open_loot) {
                            open_corpse = &corpse;
                            break;
                        }
                    }
                    if (open_corpse == nullptr) {
                        open_loot.reset();  // emptied or despawned
                    }
                }

                if (open_corpse != nullptr) {
                    if (const auto index = client::corpse_loot_hit(
                            *renderer, *open_corpse, mouse_x, mouse_y)) {
                        session->request_loot_take(open_corpse->tile, *index);
                        consumed = true;
                    } else if (client::corpse_loot_panel_contains(
                                   *renderer, *open_corpse, mouse_x, mouse_y)) {
                        consumed = true;  // click on chrome, keep panel open
                    }
                }

                if (!consumed && show_inventory) {
                    const client::InventoryAction action = client::inventory_hit(
                        *renderer, view, mouse_x, mouse_y);
                    if (action.kind == client::InventoryAction::Kind::Equip) {
                        session->request_equip(action.item);
                        consumed = true;
                    } else if (action.kind ==
                               client::InventoryAction::Kind::Unequip) {
                        session->request_unequip(action.slot);
                        consumed = true;
                    }
                }

                if (!consumed) {
                    const sim::NetId picked = client::battle_list_hit(
                        *renderer, tileset, view, mouse_x, mouse_y, battle_top);
                    if (picked != sim::kInvalidNetId) {
                        session->request_attack(picked);
                        ui_target = picked;
                        open_loot.reset();
                        consumed = true;
                    }
                }

                if (!consumed && params.hover_valid) {
                    // Prefer opening a loot bag over walking onto its tile — but
                    // only within reach (same floor, Chebyshev ≤ 1), like melee.
                    bool opened_bag = false;
                    sim::TilePos local_tile{};
                    bool have_local = false;
                    for (const sim::ActorState& actor : view.actors) {
                        if (actor.net_id == view.local_id) {
                            local_tile = actor.tile;
                            have_local = true;
                            break;
                        }
                    }
                    for (const client::CorpseView& corpse : view.corpses) {
                        if (corpse.tile != params.hover) {
                            continue;
                        }
                        const int dist =
                            have_local ? sim::tile_distance(local_tile, corpse.tile)
                                       : -1;
                        if (dist >= 0 && dist <= 1) {
                            open_loot = corpse.tile;
                            opened_bag = true;
                            consumed = true;
                        }
                        // Out of reach: fall through to walk toward the bag.
                        break;
                    }

                    if (!opened_bag) {
                        open_loot.reset();
                        sim::NetId target = sim::kInvalidNetId;
                        for (const sim::ActorState& actor : view.actors) {
                            if (actor.net_id != view.local_id &&
                                actor.tile == params.hover) {
                                target = actor.net_id;
                                break;
                            }
                        }
                        if (target != sim::kInvalidNetId) {
                            session->request_attack(target);
                            ui_target = target;
                        } else {
                            ui_target = sim::kInvalidNetId;
                            session->request_move_to(params.hover);
                        }
                    }
                }
            }

            // A target that walked out of view (or died) stops being highlighted:
            // the panel would otherwise frame a row that no longer exists.
            if (ui_target != sim::kInvalidNetId) {
                bool still_there = false;
                for (const sim::ActorState& actor : session->view().actors) {
                    if (actor.net_id == ui_target) {
                        still_there = true;
                        break;
                    }
                }
                if (!still_there) {
                    ui_target = sim::kInvalidNetId;
                }
            }

            const client::input::ScreenDir held = client::input::held_direction();
            if (held != client::input::ScreenDir::None) {
                session->request_walk(client::input::to_grid(held, scheme));
            }

            session->update();
            if (!session->alive()) {
                LOG_ERROR("session ended: %s", session->status_text().c_str());
                running = false;
            }

            // Monotonic seconds since start, for animating the damage numbers.
            const float now_seconds = static_cast<float>(
                static_cast<double>(core::now_nanos() - start_nanos) / 1.0e9);
            damage_feed.observe(session->view(), now_seconds);
            for (const sim::AttackEvent& effect : session->drain_effects()) {
                effect_feed.spawn(effect, now_seconds);
            }

            {
                float target_x = 0.0F;
                float target_y = 0.0F;
                if (client::camera_target(session->view(), target_x, target_y)) {
                    if (!camera_initialised) {
                        camera_x = target_x;
                        camera_y = target_y;
                        camera_initialised = true;
                    } else {
                        // Exponential smoothing: the camera trails the actor
                        // slightly instead of snapping, which hides the 1/30s
                        // granularity of tile stepping.
                        constexpr float kFollow = 0.18F;
                        camera_x += (target_x - camera_x) * kFollow;
                        camera_y += (target_y - camera_y) * kFollow;
                    }
                }
            }

            renderer->begin_frame(client::Color{18, 20, 26, 255});
            client::render_world(*renderer, tileset, session->view(), params);
            effect_feed.render(*renderer, tileset, now_seconds);
            damage_feed.render(*renderer, tileset, now_seconds);
            if (show_inventory) {
                client::draw_inventory(*renderer, tileset, session->view());
            }
            if (open_loot.has_value()) {
                for (const client::CorpseView& corpse : session->view().corpses) {
                    if (corpse.tile == *open_loot) {
                        client::draw_corpse_loot(*renderer, tileset, corpse);
                        break;
                    }
                }
            }
            client::draw_battle_list(*renderer, tileset, session->view(),
                                     monster_types, ui_target, battle_top);
            client::draw_spell_hotbar(*renderer, tileset, session->view(),
                                      options.vocation);
            renderer->end_frame();

            ++frames;
            ++fps_window_frames;

            // Captured after end_frame so what lands in the file is exactly what
            // was presented, camera smoothing included.
            if (!options.screenshot_path.empty() &&
                frames >= static_cast<std::uint64_t>(options.screenshot_frame)) {
                SDL_Surface* shot = SDL_RenderReadPixels(sdl_renderer, nullptr);
                if (shot == nullptr) {
                    LOG_ERROR("SDL_RenderReadPixels failed: %s", SDL_GetError());
                } else if (!SDL_SaveBMP(shot, options.screenshot_path.c_str())) {
                    LOG_ERROR("could not write '%s': %s",
                              options.screenshot_path.c_str(), SDL_GetError());
                } else {
                    LOG_INFO("wrote %dx%d screenshot to '%s'", shot->w, shot->h,
                             options.screenshot_path.c_str());
                }
                if (shot != nullptr) {
                    SDL_DestroySurface(shot);
                }
                running = false;
            }

            const std::uint64_t now = core::now_nanos();
            if (now - fps_window_start >= 500'000'000ULL) {
                fps = static_cast<double>(fps_window_frames) * 1.0e9 /
                      static_cast<double>(now - fps_window_start);
                fps_window_start = now;
                fps_window_frames = 0;
            }

            // The window title is the HUD until a real one exists. Dear ImGui is
            // the intended next step; see docs/roadmap.md.
            if (now - last_title_update >= 250'000'000ULL) {
                last_title_update = now;
                char title[256];
                std::snprintf(
                    title, sizeof(title),
                    "Isometric Prototype | %s | %.0f fps | %d draws | zoom %.2f "
                    "| floor %d | hover %d,%d",
                    session->status_text().c_str(), fps,
                    renderer->last_draw_calls(), static_cast<double>(zoom), floor,
                    static_cast<int>(params.hover.x),
                    static_cast<int>(params.hover.y));
                SDL_SetWindowTitle(window, title);
            }
        }

        LOG_INFO("shutting down after %llu frames | %s",
                 static_cast<unsigned long long>(frames),
                 session->status_text().c_str());
    }

    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
