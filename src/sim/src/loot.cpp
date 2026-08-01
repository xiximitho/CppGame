#include "sim/loot.hpp"

namespace sim {

std::vector<ItemStack> roll_monster_loot(const MonsterType& spec, Rng& rng) {
    std::vector<ItemStack> out;
    for (const LootEntry& row : spec.loot_table) {
        if (row.item == kItemNone || row.chance == 0U) {
            continue;
        }
        const bool drops = row.chance >= kLootChanceScale ||
                           rng.next_below(kLootChanceScale) < row.chance;
        if (!drops) {
            continue;
        }
        const std::uint16_t count = static_cast<std::uint16_t>(
            row.max_count <= 1
                ? 1
                : rng.next_range(1, static_cast<int>(row.max_count)));
        out.push_back(ItemStack{row.item, count});
    }
    return out;
}

}  // namespace sim
