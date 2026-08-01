#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sim/item_type.hpp"
#include "sim/types.hpp"

// Player vocations (Grimhold): permanent class chosen at character creation.
// Same weight as monsters.txt — plain data, no content_hash — because vocation
// stats are only applied where the simulation runs. Presentation (nameplate,
// create-character UI) reads the same file on the client.

namespace sim {

using VocationId = std::uint16_t;

constexpr VocationId kVocationNone = 0;

/// One vocation. Numbers are the base kit; level growth is applied by whoever
/// builds CHealth / mana from CProgress + this row (not done until XP lands).
struct VocationType {
    VocationId    id         = kVocationNone;
    std::string   name;
    /// Short code for UI (CAV, PAL, …) — presentation only.
    std::string   code;
    std::uint16_t appearance = 0;  ///< atlas appearance; 0 = player/knight art
    std::int32_t  base_hp    = 150;
    std::int32_t  hp_per_level = 5;
    std::int32_t  base_mana  = 0;
    std::int32_t  mana_per_level = 0;
    std::uint16_t capacity   = 400;
    AttackKind    preferred_kind = AttackKind::Melee;
    /// Starting equipment / bag contents for a fresh character of this vocation.
    std::vector<ItemTypeId> starter_items;
};

namespace vocations {

constexpr VocationId kKnight   = 1;  ///< Cavaleiro — tank melee
constexpr VocationId kPaladin  = 2;  ///< Paladino — hybrid ranged
constexpr VocationId kMage     = 3;  ///< Mago elemental — burst
constexpr VocationId kDruid    = 4;  ///< Druida — heal / control
constexpr VocationId kRogue    = 5;  ///< Ladino — stub until stealth exists
constexpr VocationId kNecro    = 6;  ///< Necromante — stub until summons exist

}  // namespace vocations

class VocationRegistry {
public:
    void add(const VocationType& type);

    const VocationType& get(VocationId id) const;
    bool                contains(VocationId id) const;
    std::size_t         count() const { return count_; }
    std::vector<VocationId> ids() const;

private:
    std::vector<VocationType> by_id_;
    VocationType              none_{};
    std::size_t               count_ = 0U;
};

/// Fallback when assets/vocations.txt is missing. Must agree with the file.
const VocationRegistry& default_vocations();

/// CLI / create-character token → id. Accepts codes and English/Portuguese
/// names (`mage`, `mago`, `MAG`). Unknown → `kVocationNone`.
VocationId parse_vocation_token(std::string_view text);

}  // namespace sim
