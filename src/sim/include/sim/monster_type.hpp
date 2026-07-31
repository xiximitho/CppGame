#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sim/item_type.hpp"
#include "sim/types.hpp"

// The monster catalogue: what a class of mob IS, as pure data.
//
// Same rules as item_type.hpp — no I/O, no SDL, no clock — and the same split of
// concerns: everything here is something the SERVER must reason about (health,
// damage, how far it notices you, how fast it walks). The only presentation field
// is `appearance`, which is a number both sides agree on; which sprite it draws is
// the client's business and lives in the atlas.
//
// WHERE THE REAL CATALOGUE LIVES: assets/monsters.txt, parsed by monster_io.hpp.
// The table built here is the FALLBACK for a missing or malformed file, and the two
// must agree — a fallback that plays differently turns "the asset did not load"
// into "the game changed", which is exactly the kind of thing nobody notices for a
// week. Not in content.db because monster stats are only read where the simulation
// runs, so there is nothing for the content hash to protect; see monster_io.hpp.

namespace sim {

using MonsterTypeId = std::uint16_t;

constexpr MonsterTypeId kMonsterNone = 0;

/// One class of monster.
struct MonsterType {
    MonsterTypeId id         = kMonsterNone;
    /// For humans and for the client's battle list. The simulation never reads it.
    /// It lives here rather than being dropped by the parser because the client
    /// reads the same file, and a second parser for one format is a format that
    /// drifts.
    std::string   name;
    /// Sprite row the client draws this class with (atlas `mob <appearance> <dir>`).
    std::uint16_t appearance = 0;
    std::int32_t  max_hp     = 20;
    /// Innate combat, copied into CCombat at spawn. Gear is separate and monsters
    /// have none, so these numbers ARE the fight.
    std::int16_t  attack     = 4;
    std::int16_t  defense    = 0;
    AttackKind    attack_kind = AttackKind::Melee;
    std::uint8_t  attack_range = 1;
    std::uint8_t  effect     = kEffectMeleeGlow;
    /// Ticks per cardinal step. Lower is faster; kDefaultStepTicks is player speed.
    Tick          step_ticks = kDefaultStepTicks;
    /// How far it notices a non-monster, in tiles (Chebyshev). 0 = never attacks.
    std::uint8_t  aggro_radius = 6;
    /// How far it will drift from where it spawned while it has nothing to chase.
    std::uint8_t  leash = 8;
    /// One item left on the ground when it dies. kItemNone drops nothing. A real
    /// loot table (several items, chances) is a later shape; one id keeps the
    /// kill-and-pick-up loop visible without pretending to be a drop system.
    ItemTypeId    loot = kItemNone;
};

/// Ids of the classes that exist. Numbers are a contract exactly like item ids:
/// a saved map or an older client refers to a class by number, so a shipped id
/// never changes meaning and is never recycled.
namespace monsters {

constexpr MonsterTypeId kRat      = 1;  ///< fast, weak, swarms
constexpr MonsterTypeId kSkeleton = 2;  ///< the middle of the road
constexpr MonsterTypeId kOgre     = 3;  ///< slow, tanky, hits hard

}  // namespace monsters

/// Appearance ids. 0 is the player/knight, which is what every actor drew before
/// monsters had their own art.
constexpr std::uint16_t kAppearancePlayer   = 0;
constexpr std::uint16_t kAppearanceRat      = 1;
constexpr std::uint16_t kAppearanceSkeleton = 2;
constexpr std::uint16_t kAppearanceOgre     = 3;

/// One monster placed by a map author: which class, and where.
///
/// Lives here rather than in map_io.hpp so systems.hpp can spawn a list of them
/// without sim/ growing an include from behaviour to file format.
struct MonsterSpawn {
    TilePos       tile;
    MonsterTypeId type = kMonsterNone;
};

/// A spawn point an author placed: a class, a population, and how fast it comes
/// back. This is the alternative to scattering mobs at random — the map decides
/// what lives where, and killing something does not empty the map for good.
struct SpawnerSpec {
    TilePos       tile;
    MonsterTypeId type = kMonsterNone;
    /// How many of this class it keeps alive at once.
    std::uint8_t  max_alive = 1;
    /// How far from the spawn point its children may appear, in tiles. 0 means
    /// exactly on it (and so at most one alive, since a tile holds one actor).
    std::uint8_t  radius = 2;
    /// Seconds between a death and the replacement appearing.
    std::uint16_t respawn_seconds = 30;
};

/// The catalogue. Dense by id, like ItemTypeRegistry, and read-only once built.
class MonsterRegistry {
public:
    void add(const MonsterType& type);

    /// Unknown ids return a type with id == kMonsterNone. Callers that spawn from
    /// authored data must check, because a map naming a class that does not exist
    /// is content to be reported, not a crash.
    const MonsterType& get(MonsterTypeId id) const;
    bool               contains(MonsterTypeId id) const;
    std::size_t        count() const { return count_; }
    std::vector<MonsterTypeId> ids() const;

private:
    std::vector<MonsterType> by_id_;
    MonsterType              none_{};
    std::size_t              count_ = 0U;
};

/// The three classes that ship today. Distinct along the axes that are actually
/// felt in play — speed, reach, how hard they hit, how far they notice you —
/// rather than three of the same monster with different numbers of hit points.
const MonsterRegistry& default_monsters();

}  // namespace sim
