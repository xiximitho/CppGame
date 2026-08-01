#pragma once

#include <vector>

#include "sim/components.hpp"
#include "sim/monster_type.hpp"
#include "sim/rng.hpp"

// Monster loot rolls. Pure: no I/O, no World mutation — apply_damage owns the
// side effects (spawn corpse). LootEntry / kLootChanceScale live on MonsterType.

namespace sim {

/// Independent rolls per table row. Quantity is uniform in [1, max_count] when
/// the chance roll succeeds. Rows with item == kItemNone or chance == 0 are
/// skipped. Sum of chances may exceed the scale (Tibia allows that).
std::vector<ItemStack> roll_monster_loot(const MonsterType& spec, Rng& rng);

}  // namespace sim
