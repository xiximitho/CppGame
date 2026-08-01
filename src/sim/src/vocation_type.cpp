#include "sim/vocation_type.hpp"

#include <string>
#include <string_view>

#include "sim/tile_ids.hpp"

namespace sim {

void VocationRegistry::add(const VocationType& type) {
    if (type.id == kVocationNone) {
        return;
    }
    const auto index = static_cast<std::size_t>(type.id);
    if (index >= by_id_.size()) {
        by_id_.resize(index + 1U);
    }
    if (by_id_[index].id == kVocationNone) {
        ++count_;
    }
    by_id_[index] = type;
}

const VocationType& VocationRegistry::get(VocationId id) const {
    const auto index = static_cast<std::size_t>(id);
    if (id == kVocationNone || index >= by_id_.size()) {
        return none_;
    }
    return by_id_[index];
}

bool VocationRegistry::contains(VocationId id) const {
    return get(id).id != kVocationNone;
}

std::vector<VocationId> VocationRegistry::ids() const {
    std::vector<VocationId> out;
    for (std::size_t i = 0; i < by_id_.size(); ++i) {
        if (by_id_[i].id != kVocationNone) {
            out.push_back(static_cast<VocationId>(i));
        }
    }
    return out;
}

const VocationRegistry& default_vocations() {
    static const VocationRegistry registry = [] {
        VocationRegistry r;

        r.add(VocationType{
            .id = vocations::kKnight,
            .name = "Cavaleiro",
            .code = "CAV",
            .appearance = 0,
            .base_hp = 185,
            .hp_per_level = 15,
            .base_mana = 20,
            .mana_per_level = 5,
            .capacity = 470,
            .preferred_kind = AttackKind::Melee,
            .starter_items = {tiles::kKnightSword, tiles::kSteelShield,
                              tiles::kSteelHelmet, tiles::kPlateArmor,
                              tiles::kPlateLegs, tiles::kSteelBoots,
                              tiles::kHealthPotion},
        });
        r.add(VocationType{
            .id = vocations::kPaladin,
            .name = "Paladino",
            .code = "PAL",
            .appearance = 0,
            .base_hp = 145,
            .hp_per_level = 10,
            .base_mana = 60,
            .mana_per_level = 15,
            .capacity = 430,
            .preferred_kind = AttackKind::Ranged,
            .starter_items = {tiles::kWoodenBow, tiles::kRangerTunic,
                              tiles::kLeatherLegs, tiles::kLeatherBoots,
                              tiles::kCrossAmulet, tiles::kQuiver,
                              tiles::kHealthPotion},
        });
        r.add(VocationType{
            .id = vocations::kMage,
            .name = "Mago elemental",
            .code = "MAG",
            .appearance = 0,
            .base_hp = 110,
            .hp_per_level = 5,
            .base_mana = 120,
            .mana_per_level = 30,
            .capacity = 400,
            .preferred_kind = AttackKind::Ranged,
            .starter_items = {tiles::kBlueWand, tiles::kMageRobe,
                              tiles::kWizardHat, tiles::kBlueBoots,
                              tiles::kSapphireRing, tiles::kManaPotion},
        });
        r.add(VocationType{
            .id = vocations::kDruid,
            .name = "Druida",
            .code = "DRU",
            .appearance = 0,
            .base_hp = 120,
            .hp_per_level = 5,
            .base_mana = 120,
            .mana_per_level = 30,
            .capacity = 410,
            .preferred_kind = AttackKind::Ranged,
            .starter_items = {tiles::kNatureStaff, tiles::kLeatherArmor,
                              tiles::kWolfHood, tiles::kGreenBoots,
                              tiles::kMoonAmulet, tiles::kManaPotion},
        });
        // Stubs: present in the catalogue so ids stay reserved; no starter kit
        // and weak numbers until stealth / summons exist.
        r.add(VocationType{
            .id = vocations::kRogue,
            .name = "Ladino",
            .code = "LAD",
            .appearance = 0,
            .base_hp = 130,
            .hp_per_level = 8,
            .base_mana = 40,
            .mana_per_level = 10,
            .capacity = 420,
            .preferred_kind = AttackKind::Melee,
            .starter_items = {},
        });
        r.add(VocationType{
            .id = vocations::kNecro,
            .name = "Necromante",
            .code = "NEC",
            .appearance = 0,
            .base_hp = 125,
            .hp_per_level = 6,
            .base_mana = 110,
            .mana_per_level = 28,
            .capacity = 400,
            .preferred_kind = AttackKind::Ranged,
            .starter_items = {},
        });

        return r;
    }();
    return registry;
}

VocationId parse_vocation_token(std::string_view text) {
    auto lower = [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    std::string key;
    key.reserve(text.size());
    for (const char ch : text) {
        key.push_back(lower(ch));
    }
    if (key == "1" || key == "knight" || key == "cavaleiro" || key == "cav") {
        return vocations::kKnight;
    }
    if (key == "2" || key == "paladin" || key == "paladino" || key == "pal") {
        return vocations::kPaladin;
    }
    if (key == "3" || key == "mage" || key == "mago" || key == "mag") {
        return vocations::kMage;
    }
    if (key == "4" || key == "druid" || key == "druida" || key == "dru") {
        return vocations::kDruid;
    }
    if (key == "5" || key == "rogue" || key == "ladino" || key == "lad") {
        return vocations::kRogue;
    }
    if (key == "6" || key == "necro" || key == "necromante" || key == "nec") {
        return vocations::kNecro;
    }
    return kVocationNone;
}

}  // namespace sim
