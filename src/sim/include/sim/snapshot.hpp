#pragma once

#include <cstdint>
#include <vector>

#include "sim/outfit.hpp"
#include "sim/types.hpp"
#include "sim/world.hpp"

namespace sim {

/// One actor as the client needs to see it.
///
/// A step in flight is described by where it started plus how far along it is,
/// not by an interpolated float position. The client reconstructs the smooth
/// position itself, which means a dropped snapshot costs nothing: the next one
/// still says exactly where the actor is in its step.
struct ActorState {
    NetId         net_id     = kInvalidNetId;
    TilePos       tile;                            ///< tile being left (or stood on)
    Direction     facing     = Direction::South;
    bool          walking    = false;
    Direction     walk_dir   = Direction::South;
    std::uint8_t  walk_progress = 0;               ///< 0..255 through the step
    std::uint16_t appearance = 0;
    /// Palette indices for appearance 0 (player). Ignored for mobs.
    COutfit       outfit{};
    std::int16_t  hp         = 0;
    std::int16_t  max_hp     = 0;
};

struct Snapshot {
    Tick                    tick = 0;
    std::vector<ActorState> actors;
};

/// Reads one actor's visible state out of the world.
ActorState read_actor_state(const World& world, entt::entity entity);

/// Fills `out` with every actor inside the area of interest around `center`.
/// Clears `out.actors` first. Same floor only for now — showing the floor below
/// through a hole is a rendering feature that needs its own visibility rule.
void build_snapshot(const World& world, TilePos center, Snapshot& out);

/// Where an actor should be drawn, in fractional tile coordinates. This is the
/// one place that turns discrete simulation state into continuous screen input.
struct InterpolatedPos {
    float x = 0.0F;
    float y = 0.0F;
    int   z = 0;
};
InterpolatedPos interpolate(const ActorState& state);

}  // namespace sim
