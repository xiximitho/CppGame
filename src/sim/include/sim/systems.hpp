#pragma once

#include "sim/rng.hpp"
#include "sim/world.hpp"

namespace sim {

/// Moves every CWanderer entity in a random direction now and then. This is a
/// placeholder so the world is not empty and so snapshot/interpolation code has
/// something other than the local player to prove itself against. Real behaviour
/// gets its own systems; this one is expected to be deleted.
void update_wanderers(World& world, Rng& rng);

}  // namespace sim
