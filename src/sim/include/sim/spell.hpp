#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "sim/types.hpp"
#include "sim/vocation_type.hpp"
#include "sim/world.hpp"

// Player spells. Data is hardcoded for V1 (one active per playable vocation);
// a text catalogue can replace this later the same way monsters.txt did.
// Casting is validated in sim/ so solo and server share one path.

namespace sim {

using SpellId = std::uint16_t;

constexpr SpellId kSpellNone         = 0;
constexpr SpellId kSpellBash         = 1;  ///< Knight — melee smash
constexpr SpellId kSpellBlessing     = 2;  ///< Paladin — holy heal (self)
constexpr SpellId kSpellFirebolt     = 3;  ///< Mage — fire damage at range
constexpr SpellId kSpellNaturesTouch = 4;  ///< Druid — nature heal (self)

enum class SpellKind : std::uint8_t { Damage, Heal };

struct SpellDef {
    SpellId       id = kSpellNone;
    std::string_view name;
    VocationId    vocation = kVocationNone;  ///< who may cast it
    SpellKind     kind = SpellKind::Damage;
    std::int32_t  mana_cost = 0;
    Tick          cooldown_ticks = 20;
    std::uint8_t  range = 0;   ///< 0 = self only; else max Chebyshev to target
    std::int32_t  power = 0;   ///< damage or heal amount
    std::uint8_t  effect = 0;  ///< AttackEvent / client effect id
};

/// All shipped spells (not vocation-filtered).
std::span<const SpellDef> all_spells();

const SpellDef* find_spell(SpellId id);

/// Spells the vocation may put on the hotbar (one each for CAV/PAL/MAG/DRU).
std::span<const SpellDef> spells_for_vocation(VocationId vocation);

/// Applies vocation stats/kit to a player actor. Replaces CVocation, CProgress,
/// CHealth max, and starter gear. Call once at character creation / solo spawn.
void apply_vocation(World& world, entt::entity actor, VocationId vocation);

/// Casts `spell` for `caster`. `target` is required for Damage spells (may be
/// self for Heal). Returns false when vocation/mana/cooldown/range fail.
bool cast_spell(World& world, NetId caster, SpellId spell,
                NetId target = kInvalidNetId);

}  // namespace sim
