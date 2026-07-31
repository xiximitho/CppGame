#include "sim/monster_type.hpp"

#include "sim/tile_ids.hpp"

namespace sim {

void MonsterRegistry::add(const MonsterType& type) {
    if (type.id == kMonsterNone) {
        return;
    }
    const auto index = static_cast<std::size_t>(type.id);
    if (index >= by_id_.size()) {
        by_id_.resize(index + 1U);
    }
    if (by_id_[index].id == kMonsterNone) {
        ++count_;
    }
    by_id_[index] = type;
}

const MonsterType& MonsterRegistry::get(MonsterTypeId id) const {
    const auto index = static_cast<std::size_t>(id);
    if (id == kMonsterNone || index >= by_id_.size()) {
        return none_;
    }
    return by_id_[index];
}

bool MonsterRegistry::contains(MonsterTypeId id) const {
    return get(id).id != kMonsterNone;
}

std::vector<MonsterTypeId> MonsterRegistry::ids() const {
    std::vector<MonsterTypeId> out;
    for (std::size_t i = 0; i < by_id_.size(); ++i) {
        if (by_id_[i].id != kMonsterNone) {
            out.push_back(static_cast<MonsterTypeId>(i));
        }
    }
    return out;
}

const MonsterRegistry& default_monsters() {
    // Built once. A function-local static keeps the table out of static
    // initialisation order and costs one guard check per call.
    static const MonsterRegistry registry = [] {
        MonsterRegistry r;

        // A rat is a pressure test, not a threat: quick for a mob (13 ticks a step
        // against the player's 9, so still outrunnable), dies to two hits, and only
        // notices you from close up.
        //
        // These numbers are the FALLBACK. assets/monsters.txt is the real catalogue
        // and must agree with this list, or a missing asset quietly changes the
        // game instead of just logging that it is missing.
        r.add(MonsterType{.id = monsters::kRat,
                          .name = "rato",
                          .appearance = kAppearanceRat,
                          .max_hp = 14,
                          .attack = 3,
                          .defense = 0,
                          .attack_range = 1,
                          .step_ticks = 13,
                          .aggro_radius = 4,
                          .leash = 6,
                          .loot = tiles::kBoots});

        // A skeleton is the yardstick: a bit over twice the player's step time,
        // enough hp that a fight is a fight, and it sees far, so it comes to you.
        r.add(MonsterType{.id = monsters::kSkeleton,
                          .name = "esqueleto",
                          .appearance = kAppearanceSkeleton,
                          .max_hp = 40,
                          .attack = 8,
                          .defense = 3,
                          .attack_range = 1,
                          .step_ticks = 19,
                          .aggro_radius = 8,
                          .leash = 10,
                          .loot = tiles::kShield});

        // An ogre is a decision: a full second per tile, so walking away always
        // works, and painful enough that standing to trade blows is a bad idea.
        // Reach 2, so stepping back one tile does not leave its swing.
        r.add(MonsterType{.id = monsters::kOgre,
                          .name = "ogro",
                          .appearance = kAppearanceOgre,
                          .max_hp = 90,
                          .attack = 18,
                          .defense = 6,
                          .attack_range = 2,
                          .step_ticks = 30,
                          .aggro_radius = 7,
                          .leash = 12,
                          .loot = tiles::kAmulet});

        return r;
    }();
    return registry;
}

}  // namespace sim
