#include <cstdio>

#include "client/session.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include <string>
#include <utility>

#include "platform/vfs.hpp"
#include "sim/components.hpp"
#include "sim/item_type.hpp"
#include "sim/map_gen.hpp"
#include "sim/map_io.hpp"
#include "sim/systems.hpp"
#include "sim/world.hpp"

namespace client {
namespace {

/// Builds the solo World. Prefers an authored map read through platform::vfs
/// (so it also works from inside the APK on Android); falls back to the seeded
/// procedural map when the file is missing or malformed, which keeps a clone
/// with no map file runnable. The World keeps a copy of the item catalogue so
/// gameplay systems can query item properties without a global.
sim::World build_solo_world(std::uint64_t seed, const char* map_path) {
    const sim::ItemTypeRegistry item_types = sim::build_default_registry();

    std::string text;
    if (platform::vfs::read_asset_text(map_path, text)) {
        std::string error;
        if (auto parsed = sim::parse_text_map(text, item_types, &error)) {
            LOG_INFO("loaded map '%s' (%dx%d)", map_path, parsed->map.width(),
                     parsed->map.height());
            return sim::World(std::move(parsed->map), item_types);
        }
        LOG_WARN("map '%s' failed to parse: %s; using generated map", map_path,
                 error.c_str());
    } else {
        LOG_INFO("no map '%s'; using generated map", map_path);
    }

    return sim::World(
        sim::generate_demo_map(sim::MapGenSettings{96, 96, 3, seed}, item_types),
        item_types);
}

/// Runs the real simulation in-process at the real tick rate.
///
/// It deliberately does NOT shortcut through World for rendering: it builds the
/// same Snapshot a server would send and renders from that. If solo play read the
/// registry directly, single-player would drift from multiplayer behaviour and
/// nobody would notice until the network build was tested.
class SoloSession final : public Session {
public:
    SoloSession(std::uint64_t seed, int wanderers)
        : world_(build_solo_world(seed, "maps/dungeon.txt")),
          rng_(seed ^ 0x9E3779B97F4A7C15ULL) {
        const sim::TilePos spawn = sim::find_spawn_tile(world_.map(), rng_);

        local_id_ = world_.allocate_net_id();
        world_.spawn_actor(local_id_, spawn, 0);

        spawn_wanderers(wanderers, spawn);

        // The scaffold's map never changes, so the view copies it once instead of
        // every frame. Destructible terrain would replace this with a dirty-rect
        // or chunk-version scheme.
        view_.map = world_.map();
        view_.local_id = local_id_;
        view_.ready = true;

        last_time_ = core::now_nanos();

        LOG_INFO("solo world ready, spawn at (%d,%d,%d), %d wanderers",
                 spawn.x, spawn.y, static_cast<int>(spawn.z), spawned_wanderers_);
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

            sim::update_path_followers(world_);
            sim::update_wanderers(world_, rng_);
        }

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

    const WorldView& view() const override { return view_; }

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

            const sim::NetId id = world_.allocate_net_id();
            const entt::entity entity = world_.spawn_actor(id, at, 0);
            world_.registry().emplace<sim::CWanderer>(entity, sim::CWanderer{0});
            ++spawned_wanderers_;
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
    int            spawned_wanderers_ = 0;
};

}  // namespace

std::unique_ptr<Session> make_solo_session(std::uint64_t seed, int wanderers) {
    return std::make_unique<SoloSession>(seed, wanderers);
}

}  // namespace client
