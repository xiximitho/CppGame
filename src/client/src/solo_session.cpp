#include <cstdio>

#include "client/session.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include <string>
#include <utility>

#include <cstdint>
#include <optional>
#include <vector>

#include "client/content.hpp"
#include "platform/vfs.hpp"
#include "sim/components.hpp"
#include "sim/content_blob.hpp"
#include "sim/item_type.hpp"
#include "sim/map_gen.hpp"
#include "sim/map_io.hpp"
#include "sim/monster_io.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/world.hpp"

namespace client {
namespace {

/// A built world plus what the map file said about it. The authored player spawn
/// and the authored monsters both come from the same parse, so they travel
/// together instead of the caller re-reading the file.
struct SoloWorld {
    sim::World                     world;
    std::optional<sim::TilePos>    spawn;
    std::vector<sim::MonsterSpawn> monsters;
    std::vector<sim::SpawnerSpec>  spawners;
};

/// Builds the solo World. Prefers an authored map read through platform::vfs
/// (so it also works from inside the APK on Android); falls back to the seeded
/// procedural map when the file is missing or malformed, which keeps a clone
/// with no map file runnable. The World keeps a copy of the item catalogue so
/// gameplay systems can query item properties without a global.
SoloWorld build_solo_world(std::uint64_t seed, const char* map_path) {
    const sim::ItemTypeRegistry item_types = client::load_item_catalogue();
    const sim::MonsterRegistry monsters = client::load_monster_catalogue();

    std::string text;
    if (platform::vfs::read_asset_text(map_path, text)) {
        std::string error;
        if (auto parsed = sim::parse_text_map(text, item_types, &error)) {
            LOG_INFO("loaded map '%s' (%dx%d), %zu monster(s) + %zu spawner(s)"
                     " + %zu portal(s)",
                     map_path, parsed->map.width(), parsed->map.height(),
                     parsed->monsters.size(), parsed->spawners.size(),
                     parsed->portals.size());
            sim::World world(std::move(parsed->map), item_types, monsters);
            // Same install as the server, deliberately: a warp that only worked in
            // multiplayer would be exactly the solo/network divergence the layering
            // rule exists to prevent.
            for (const sim::PortalSpec& portal : parsed->portals) {
                world.add_portal(portal.from, portal.to);
            }
            return SoloWorld{std::move(world), parsed->spawn,
                             std::move(parsed->monsters),
                             std::move(parsed->spawners)};
        }
        LOG_WARN("map '%s' failed to parse: %s; using generated map", map_path,
                 error.c_str());
    } else {
        LOG_INFO("no map '%s'; using generated map", map_path);
    }

    return SoloWorld{
        sim::World(sim::generate_demo_map(sim::MapGenSettings{96, 96, 3, seed},
                                          item_types),
                   item_types, monsters),
        std::nullopt,
        {},
        {}};
}

/// Runs the real simulation in-process at the real tick rate.
///
/// It deliberately does NOT shortcut through World for rendering: it builds the
/// same Snapshot a server would send and renders from that. If solo play read the
/// registry directly, single-player would drift from multiplayer behaviour and
/// nobody would notice until the network build was tested.
class SoloSession final : public Session {
public:
    /// Takes the built world by value so it can be MOVED in: the map file is read
    /// once, and what it said about the spawn and the mobs is consumed here rather
    /// than kept as state. make_solo_session() does the reading.
    SoloSession(SoloWorld built, std::uint64_t seed, int wanderers)
        : world_(std::move(built.world)),
          rng_(seed ^ 0x9E3779B97F4A7C15ULL) {
        // The map's own spawn point wins, but only if it is actually walkable —
        // dungeon.txt has its '@' painted over with a wall, and trusting that puts
        // the player inside rock. Same check the server does, for the same reason.
        sim::TilePos spawn = sim::find_spawn_tile(world_.map(), rng_);
        if (built.spawn.has_value() && world_.map().is_walkable(*built.spawn)) {
            spawn = *built.spawn;
        } else if (built.spawn.has_value()) {
            LOG_WARN("map spawn (%d,%d,%d) is not walkable; using (%d,%d,%d)",
                     built.spawn->x, built.spawn->y,
                     static_cast<int>(built.spawn->z), spawn.x, spawn.y,
                     static_cast<int>(spawn.z));
        }

        local_id_ = world_.allocate_net_id();
        const entt::entity local_entity =
            world_.spawn_actor(local_id_, spawn, 0);
        // The player respawns on death instead of vanishing; monsters do not.
        world_.registry().emplace<sim::CRespawn>(local_entity,
                                                 sim::CRespawn{spawn});

        // Starting kit: a sword and body armour worn, spares in the pack.
        {
            auto& equipment =
                world_.registry().emplace<sim::CEquipment>(local_entity);
            equipment.slots[static_cast<std::size_t>(sim::EquipSlot::Weapon)] =
                sim::tiles::kSword;
            equipment.slots[static_cast<std::size_t>(sim::EquipSlot::Body)] =
                sim::tiles::kArmor;
        }
        world_.registry().emplace<sim::CInventory>(
            local_entity, sim::CInventory{{{sim::tiles::kBow, 1},
                                           {sim::tiles::kShield, 1},
                                           {sim::tiles::kHelmet, 1}}});

        // Authored mobs first: they are the ones the map author aimed, and they
        // hold their tiles against the random ones that come after.
        const int authored = sim::spawn_authored_monsters(world_, built.monsters);
        const int spawners = sim::create_spawners(world_, built.spawners);
        spawn_wanderers(wanderers, spawn);

        // The scaffold's map never changes, so the view copies it once instead of
        // every frame. Destructible terrain would replace this with a dirty-rect
        // or chunk-version scheme.
        view_.map = world_.map();
        view_.local_id = local_id_;
        view_.ready = true;

        last_time_ = core::now_nanos();

        LOG_INFO("solo world ready, spawn at (%d,%d,%d), %d monster(s) + %d "
                 "spawner(s) + %d random",
                 spawn.x, spawn.y, static_cast<int>(spawn.z), authored, spawners,
                 spawned_wanderers_);
    }

    void update() override {
        const std::uint64_t now = core::now_nanos();
        std::uint64_t elapsed = now - last_time_;
        last_time_ = now;

        // A long stall (debugger breakpoint, window drag) must not be replayed as
        // hundreds of catch-up ticks. Clamp and accept the lost time.
        constexpr std::uint64_t kMaxElapsed = 250'000'000ULL;
        if (elapsed > kMaxElapsed) {
            elapsed = kMaxElapsed;
        }
        accumulator_ += elapsed;

        constexpr std::uint64_t kTickNanos = 1'000'000'000ULL / sim::kSimHz;
        while (accumulator_ >= kTickNanos) {
            accumulator_ -= kTickNanos;
            world_.step();

            // Player intent first, so a click or key press taken this frame is
            // acted on by this tick.
            if (pending_move_to_) {
                pending_move_to_ = false;
                // An explicit destination replaces a chase, like a keypress does.
                world_.cancel_path(local_id_);
                if (!world_.request_move_to(local_id_, pending_target_)) {
                    LOG_DEBUG("no route to (%d,%d,%d)", pending_target_.x,
                              pending_target_.y,
                              static_cast<int>(pending_target_.z));
                }
            }
            if (pending_walk_) {
                pending_walk_ = false;
                // Manual input takes back control from auto-walking.
                world_.cancel_path(local_id_);
                world_.request_walk(local_id_, pending_dir_);
            }
            if (pending_attack_) {
                pending_attack_ = false;
                // Attacking implies chasing, same as on the server.
                world_.set_attack_target(local_id_, pending_attack_target_);
                world_.request_follow(local_id_, pending_attack_target_);
            }
            if (pending_equip_ != sim::kItemNone) {
                world_.equip(local_id_, pending_equip_);
                pending_equip_ = sim::kItemNone;
            }
            if (has_pending_unequip_) {
                has_pending_unequip_ = false;
                world_.unequip(local_id_, pending_unequip_);
            }

            // Same order as the server's loop, for the same reason: monsters
            // decide, then followers step, then swings land.
            sim::update_spawners(world_, rng_);
            sim::update_monsters(world_, rng_);
            sim::update_chasers(world_);
            sim::update_path_followers(world_);
            sim::update_combat(world_);
        }

        // Hand attack effects to the client to render, then clear them.
        for (const sim::AttackEvent& effect : world_.attack_events()) {
            effects_buffer_.push_back(effect);
        }
        world_.clear_attack_events();

        refresh_view();
    }

    void request_walk(sim::Direction dir) override {
        // Buffered to the next tick rather than applied immediately, so input
        // resolution does not depend on frame rate.
        pending_walk_ = true;
        pending_dir_ = dir;
    }

    void request_move_to(sim::TilePos target) override {
        pending_move_to_ = true;
        pending_target_ = target;
    }

    void request_attack(sim::NetId target) override {
        pending_attack_ = true;
        pending_attack_target_ = target;
    }

    void request_equip(sim::ItemTypeId item) override { pending_equip_ = item; }
    void request_unequip(sim::EquipSlot slot) override {
        pending_unequip_ = slot;
        has_pending_unequip_ = true;
    }

    const WorldView& view() const override { return view_; }

    std::vector<sim::AttackEvent> drain_effects() override {
        std::vector<sim::AttackEvent> out;
        out.swap(effects_buffer_);
        return out;
    }

    std::string status_text() const override {
        char buffer[120];
        std::snprintf(buffer, sizeof(buffer), "solo | tick %u | %zu actors%s",
                      view_.tick, view_.actors.size(),
                      world_.is_following_path(local_id_) ? " | walking" : "");
        return std::string(buffer);
    }

    bool alive() const override { return true; }

private:
    void spawn_wanderers(int count, sim::TilePos near) {
        for (int i = 0; i < count; ++i) {
            sim::TilePos at = near;
            bool placed = false;
            for (int attempt = 0; attempt < 64; ++attempt) {
                const sim::TilePos candidate{
                    static_cast<std::int16_t>(near.x + rng_.next_range(-20, 20)),
                    static_cast<std::int16_t>(near.y + rng_.next_range(-20, 20)),
                    near.z};
                if (world_.map().is_walkable(candidate) &&
                    world_.occupant(candidate) == sim::kInvalidNetId) {
                    at = candidate;
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                continue;
            }

            // Class, stats, speed and loot come from the monster catalogue: the
            // server spawns through the same call, so the two cannot diverge.
            if (sim::spawn_random_monster(world_, rng_, at) != entt::null) {
                ++spawned_wanderers_;
            }
        }
    }

    void refresh_view() {
        const entt::entity local = world_.lookup(local_id_);
        if (local == entt::null) {
            return;
        }
        const sim::TilePos center =
            world_.registry().get<sim::CPosition>(local).tile;

        sim::Snapshot snapshot;
        sim::build_snapshot(world_, center, snapshot);

        view_.tick = snapshot.tick;
        view_.actors = std::move(snapshot.actors);

        // The inventory panel reads these; solo fills them straight from the
        // local player's components.
        if (const auto* equipment =
                world_.registry().try_get<sim::CEquipment>(local)) {
            view_.equipment = equipment->slots;
        }
        if (const auto* inventory =
                world_.registry().try_get<sim::CInventory>(local)) {
            view_.inventory = inventory->items;
        }

        view_.ground_items.clear();
        for (const auto& [key, pile] : world_.ground_piles()) {
            if (!pile.items.empty()) {
                view_.ground_items.push_back(
                    GroundItemView{pile.tile, pile.items.front().id});
            }
        }
    }

    sim::World  world_;
    sim::Rng    rng_;
    sim::NetId  local_id_ = sim::kInvalidNetId;
    WorldView   view_;

    std::uint64_t last_time_ = 0;
    std::uint64_t accumulator_ = 0;

    bool           pending_walk_ = false;
    sim::Direction pending_dir_ = sim::Direction::South;
    bool           pending_move_to_ = false;
    sim::TilePos   pending_target_;
    bool           pending_attack_ = false;
    sim::NetId     pending_attack_target_ = sim::kInvalidNetId;
    sim::ItemTypeId pending_equip_ = sim::kItemNone;
    bool           has_pending_unequip_ = false;
    sim::EquipSlot pending_unequip_ = sim::EquipSlot::Weapon;
    std::vector<sim::AttackEvent> effects_buffer_;
    int            spawned_wanderers_ = 0;
};

}  // namespace

std::unique_ptr<Session> make_solo_session(std::uint64_t seed, int wanderers,
                                           const std::string& map_path) {
    return std::make_unique<SoloSession>(build_solo_world(seed, map_path.c_str()),
                                         seed, wanderers);
}

}  // namespace client
