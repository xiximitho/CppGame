#include "sim/spell.hpp"

#include <array>

#include "sim/components.hpp"
#include "sim/item_type.hpp"
#include "sim/tile_ids.hpp"
#include "sim/vocation_type.hpp"

namespace sim {
namespace {

constexpr std::array<SpellDef, 4> kSpells{{
    SpellDef{.id = kSpellBash,
             .name = "Bash",
             .vocation = vocations::kKnight,
             .kind = SpellKind::Damage,
             .mana_cost = 8,
             .cooldown_ticks = 20,
             .range = 1,
             .power = 22,
             .effect = kEffectMeleeGlow},
    SpellDef{.id = kSpellBlessing,
             .name = "Blessing",
             .vocation = vocations::kPaladin,
             .kind = SpellKind::Heal,
             .mana_cost = 20,
             .cooldown_ticks = 28,
             .range = 0,
             .power = 30,
             .effect = kEffectHoly},
    SpellDef{.id = kSpellFirebolt,
             .name = "Firebolt",
             .vocation = vocations::kMage,
             .kind = SpellKind::Damage,
             .mana_cost = 18,
             .cooldown_ticks = 18,
             .range = 4,
             .power = 28,
             .effect = kEffectFirebolt},
    SpellDef{.id = kSpellNaturesTouch,
             .name = "Nature's Touch",
             .vocation = vocations::kDruid,
             .kind = SpellKind::Heal,
             .mana_cost = 22,
             .cooldown_ticks = 24,
             .range = 0,
             .power = 35,
             .effect = kEffectNature},
}};

}  // namespace

std::span<const SpellDef> all_spells() { return kSpells; }

const SpellDef* find_spell(SpellId id) {
    for (const SpellDef& spell : kSpells) {
        if (spell.id == id) {
            return &spell;
        }
    }
    return nullptr;
}

std::span<const SpellDef> spells_for_vocation(VocationId vocation) {
    // One spell per playable vocation in V1; return a subspan of size 0 or 1.
    for (std::size_t i = 0; i < kSpells.size(); ++i) {
        if (kSpells[i].vocation == vocation) {
            return std::span<const SpellDef>(kSpells.data() + i, 1);
        }
    }
    return {};
}

void apply_vocation(World& world, entt::entity actor, VocationId vocation) {
    const VocationType& spec = default_vocations().get(vocation);
    if (spec.id == kVocationNone) {
        return;
    }
    entt::registry& registry = world.registry();

    registry.emplace_or_replace<CVocation>(actor, CVocation{spec.id});

    const std::int32_t max_hp =
        spec.base_hp + spec.hp_per_level * 0;  // level 1
    const std::int32_t max_mana = spec.base_mana;
    registry.emplace_or_replace<CHealth>(actor, CHealth{max_hp, max_hp});
    registry.emplace_or_replace<CProgress>(
        actor, CProgress{1, 0, max_mana, max_mana});
    registry.emplace_or_replace<CSpellCooldown>(actor, CSpellCooldown{0});

    if (auto* actor_cmp = registry.try_get<CActor>(actor)) {
        actor_cmp->appearance = spec.appearance;
    }

    if (!registry.all_of<CEquipment>(actor)) {
        registry.emplace<CEquipment>(actor);
    }
    if (!registry.all_of<CInventory>(actor)) {
        registry.emplace<CInventory>(actor);
    }
    auto& equipment = registry.get<CEquipment>(actor);
    auto& inventory = registry.get<CInventory>(actor);
    equipment = CEquipment{};
    inventory.items.clear();

    for (const ItemTypeId item : spec.starter_items) {
        const ItemType& type = world.item_types().get(item);
        if (type.equippable &&
            equipment.slots[static_cast<std::size_t>(type.slot)] == kItemNone) {
            equipment.slots[static_cast<std::size_t>(type.slot)] = item;
        } else {
            inventory.items.push_back(ItemStack{item, 1});
        }
    }
}

bool cast_spell(World& world, NetId caster, SpellId spell_id, NetId target) {
    const SpellDef* spell = find_spell(spell_id);
    if (spell == nullptr) {
        return false;
    }

    const entt::entity entity = world.lookup(caster);
    if (entity == entt::null) {
        return false;
    }
    entt::registry& registry = world.registry();
    if (registry.all_of<CDead>(entity)) {
        return false;
    }

    const auto* vocation = registry.try_get<CVocation>(entity);
    if (vocation == nullptr || vocation->id != spell->vocation) {
        return false;
    }
    auto* progress = registry.try_get<CProgress>(entity);
    if (progress == nullptr || progress->mana < spell->mana_cost) {
        return false;
    }
    auto* cooldown = registry.try_get<CSpellCooldown>(entity);
    if (cooldown != nullptr && world.tick() < cooldown->ready_tick) {
        return false;
    }
    const auto* pos = registry.try_get<CPosition>(entity);
    if (pos == nullptr) {
        return false;
    }

    entt::entity target_entity = entity;
    TilePos target_tile = pos->tile;
    if (spell->kind == SpellKind::Damage) {
        if (target == kInvalidNetId) {
            return false;
        }
        target_entity = world.lookup(target);
        if (target_entity == entt::null || registry.all_of<CDead>(target_entity)) {
            return false;
        }
        const auto* target_pos = registry.try_get<CPosition>(target_entity);
        if (target_pos == nullptr) {
            return false;
        }
        target_tile = target_pos->tile;
        if (!in_attack_range(pos->tile, target_tile,
                             static_cast<int>(spell->range))) {
            return false;
        }
    }

    progress->mana -= spell->mana_cost;
    if (cooldown != nullptr) {
        cooldown->ready_tick = world.tick() + spell->cooldown_ticks;
    } else {
        registry.emplace<CSpellCooldown>(
            entity, CSpellCooldown{world.tick() + spell->cooldown_ticks});
    }

    if (spell->kind == SpellKind::Damage) {
        world.apply_damage(target, spell->power);
        world.push_attack_event(
            AttackEvent{pos->tile, target_tile, spell->effect});
    } else {
        if (auto* health = registry.try_get<CHealth>(target_entity)) {
            health->hp += spell->power;
            if (health->hp > health->max_hp) {
                health->hp = health->max_hp;
            }
        }
        world.push_attack_event(
            AttackEvent{pos->tile, target_tile, spell->effect});
    }
    return true;
}

}  // namespace sim
