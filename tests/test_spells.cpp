#include <doctest/doctest.h>

#include "sim/components.hpp"
#include "sim/spell.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/vocation_type.hpp"
#include "sim/weapon.hpp"
#include "sim/world.hpp"

using namespace sim;

namespace {

World make_armed_world() {
    TileMap map(12, 12, 1);
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                   static_cast<std::int16_t>(y), 0},
                           tiles::kStone);
        }
    }
    return World(std::move(map), build_default_registry());
}

void equip_weapon(World& world, NetId id, ItemTypeId item) {
    CEquipment& eq = world.registry().get_or_emplace<CEquipment>(world.lookup(id));
    eq.slots[static_cast<std::size_t>(EquipSlot::Weapon)] = item;
}

}  // namespace

TEST_CASE("knight keeps sword damage and blunts bow/staff") {
    World world = make_armed_world();
    const NetId id = world.allocate_net_id();
    const entt::entity e = world.spawn_actor(id, TilePos{5, 5, 0}, 0);
    apply_vocation(world, e, vocations::kKnight);

    // combat_stats starts at kBaseMeleeDamage; weapon attack is added on top.
    equip_weapon(world, id, tiles::kSword);
    CHECK(combat_stats(world, e).attack == kBaseMeleeDamage + 12);

    equip_weapon(world, id, tiles::kBow);
    CHECK(combat_stats(world, e).attack == kBaseMeleeDamage + 3);  // 8 * 40%

    equip_weapon(world, id, tiles::kStaff);
    CHECK(combat_stats(world, e).attack == kBaseMeleeDamage + 2);  // 10 * 25%
}

TEST_CASE("paladin is best with the bow") {
    World world = make_armed_world();
    const NetId id = world.allocate_net_id();
    const entt::entity e = world.spawn_actor(id, TilePos{5, 5, 0}, 0);
    apply_vocation(world, e, vocations::kPaladin);

    equip_weapon(world, id, tiles::kBow);
    CHECK(combat_stats(world, e).attack == kBaseMeleeDamage + 8);

    equip_weapon(world, id, tiles::kSword);
    CHECK(combat_stats(world, e).attack == kBaseMeleeDamage + 6);  // 12 * 55%
}

TEST_CASE("knight bash spends mana and hits adjacent target") {
    World world = make_armed_world();
    const NetId knight = world.allocate_net_id();
    const entt::entity knight_e = world.spawn_actor(knight, TilePos{3, 3, 0}, 0);
    apply_vocation(world, knight_e, vocations::kKnight);

    const NetId mob = world.allocate_net_id();
    world.spawn_actor(mob, TilePos{4, 3, 0}, 0);  // Chebyshev 1

    const auto* progress = world.registry().try_get<CProgress>(knight_e);
    REQUIRE(progress != nullptr);
    const std::int32_t mana_before = progress->mana;

    REQUIRE(cast_spell(world, knight, kSpellBash, mob));
    CHECK(world.registry().get<CProgress>(knight_e).mana ==
          mana_before - find_spell(kSpellBash)->mana_cost);
    CHECK(world.registry().get<CHealth>(world.lookup(mob)).hp == 100 - 22);
    REQUIRE_FALSE(world.attack_events().empty());
    CHECK(world.attack_events().back().effect == kEffectMeleeGlow);

    // Cooldown blocks a second cast on the same tick.
    CHECK_FALSE(cast_spell(world, knight, kSpellBash, mob));

    // Past cooldown, out of melee range still fails.
    world.registry().get<CSpellCooldown>(knight_e).ready_tick = 0;
    const NetId far = world.allocate_net_id();
    world.spawn_actor(far, TilePos{6, 3, 0}, 0);
    CHECK_FALSE(cast_spell(world, knight, kSpellBash, far));
}

TEST_CASE("paladin blessing heals self") {
    World world = make_armed_world();
    const NetId paladin = world.allocate_net_id();
    const entt::entity e = world.spawn_actor(paladin, TilePos{4, 4, 0}, 0);
    apply_vocation(world, e, vocations::kPaladin);

    auto& health = world.registry().get<CHealth>(e);
    health.hp = 50;

    REQUIRE(cast_spell(world, paladin, kSpellBlessing, kInvalidNetId));
    CHECK(health.hp == 80);  // 50 + 30
    CHECK(world.attack_events().back().effect == kEffectHoly);
}

TEST_CASE("mage firebolt spends mana and damages in range") {
    World world = make_armed_world();
    const NetId mage = world.allocate_net_id();
    const entt::entity mage_e = world.spawn_actor(mage, TilePos{2, 2, 0}, 0);
    apply_vocation(world, mage_e, vocations::kMage);

    const NetId mob = world.allocate_net_id();
    world.spawn_actor(mob, TilePos{5, 2, 0}, 0);  // Chebyshev 3, range 4

    const auto* progress = world.registry().try_get<CProgress>(mage_e);
    REQUIRE(progress != nullptr);
    const std::int32_t mana_before = progress->mana;

    REQUIRE(cast_spell(world, mage, kSpellFirebolt, mob));
    CHECK(world.registry().get<CProgress>(mage_e).mana ==
          mana_before - find_spell(kSpellFirebolt)->mana_cost);
    CHECK(world.registry().get<CHealth>(world.lookup(mob)).hp == 100 - 28);
    REQUIRE_FALSE(world.attack_events().empty());
    CHECK(world.attack_events().back().effect == kEffectFirebolt);

    // Cooldown blocks a second cast on the same tick.
    CHECK_FALSE(cast_spell(world, mage, kSpellFirebolt, mob));
}

TEST_CASE("druid nature touch heals self and refuses firebolt") {
    World world = make_armed_world();
    const NetId druid = world.allocate_net_id();
    const entt::entity e = world.spawn_actor(druid, TilePos{4, 4, 0}, 0);
    apply_vocation(world, e, vocations::kDruid);

    auto& health = world.registry().get<CHealth>(e);
    health.hp = 40;

    CHECK_FALSE(cast_spell(world, druid, kSpellFirebolt, druid));
    REQUIRE(cast_spell(world, druid, kSpellNaturesTouch, kInvalidNetId));
    CHECK(health.hp == 75);  // 40 + 35
}

TEST_CASE("parse_vocation_token accepts common aliases") {
    CHECK(parse_vocation_token("mage") == vocations::kMage);
    CHECK(parse_vocation_token("Druida") == vocations::kDruid);
    CHECK(parse_vocation_token("PAL") == vocations::kPaladin);
    CHECK(parse_vocation_token("nope") == kVocationNone);
}

TEST_CASE("spells_for_vocation covers the four playable classes") {
    CHECK(spells_for_vocation(vocations::kKnight)[0].id == kSpellBash);
    CHECK(spells_for_vocation(vocations::kPaladin)[0].id == kSpellBlessing);
    CHECK(spells_for_vocation(vocations::kMage)[0].id == kSpellFirebolt);
    CHECK(spells_for_vocation(vocations::kDruid)[0].id == kSpellNaturesTouch);
    CHECK(spells_for_vocation(vocations::kRogue).empty());
    CHECK(spells_for_vocation(vocations::kNecro).empty());
}

TEST_CASE("apply_vocation equips the class kit") {
    World world = make_armed_world();

    const NetId knight = world.allocate_net_id();
    const entt::entity ke = world.spawn_actor(knight, TilePos{2, 2, 0}, 0);
    apply_vocation(world, ke, vocations::kKnight);
    const auto& k_eq = world.registry().get<CEquipment>(ke);
    CHECK(k_eq.slots[static_cast<std::size_t>(EquipSlot::Weapon)] ==
          tiles::kKnightSword);
    CHECK(k_eq.slots[static_cast<std::size_t>(EquipSlot::Shield)] ==
          tiles::kSteelShield);
    CHECK(k_eq.slots[static_cast<std::size_t>(EquipSlot::Helmet)] ==
          tiles::kSteelHelmet);
    CHECK(k_eq.slots[static_cast<std::size_t>(EquipSlot::Body)] ==
          tiles::kPlateArmor);
    CHECK(k_eq.slots[static_cast<std::size_t>(EquipSlot::Legs)] ==
          tiles::kPlateLegs);
    CHECK(k_eq.slots[static_cast<std::size_t>(EquipSlot::Boots)] ==
          tiles::kSteelBoots);
    CHECK(world.registry().get<CInventory>(ke).items.size() == 1U);

    const NetId paladin = world.allocate_net_id();
    const entt::entity pe = world.spawn_actor(paladin, TilePos{3, 3, 0}, 0);
    apply_vocation(world, pe, vocations::kPaladin);
    const auto& p_eq = world.registry().get<CEquipment>(pe);
    CHECK(p_eq.slots[static_cast<std::size_t>(EquipSlot::Weapon)] ==
          tiles::kWoodenBow);
    CHECK(p_eq.slots[static_cast<std::size_t>(EquipSlot::Body)] ==
          tiles::kRangerTunic);
    CHECK(p_eq.slots[static_cast<std::size_t>(EquipSlot::Amulet)] ==
          tiles::kCrossAmulet);
    CHECK(p_eq.slots[static_cast<std::size_t>(EquipSlot::Shield)] == kItemNone);

    const NetId mage = world.allocate_net_id();
    const entt::entity me = world.spawn_actor(mage, TilePos{4, 4, 0}, 0);
    apply_vocation(world, me, vocations::kMage);
    const auto& m_eq = world.registry().get<CEquipment>(me);
    CHECK(m_eq.slots[static_cast<std::size_t>(EquipSlot::Weapon)] ==
          tiles::kBlueWand);
    CHECK(m_eq.slots[static_cast<std::size_t>(EquipSlot::Body)] ==
          tiles::kMageRobe);
    CHECK(m_eq.slots[static_cast<std::size_t>(EquipSlot::Helmet)] ==
          tiles::kWizardHat);
    CHECK(m_eq.slots[static_cast<std::size_t>(EquipSlot::Ring)] ==
          tiles::kSapphireRing);
    CHECK(combat_stats(world, me).range == 4);

    const NetId druid = world.allocate_net_id();
    const entt::entity de = world.spawn_actor(druid, TilePos{5, 5, 0}, 0);
    apply_vocation(world, de, vocations::kDruid);
    const auto& d_eq = world.registry().get<CEquipment>(de);
    CHECK(d_eq.slots[static_cast<std::size_t>(EquipSlot::Weapon)] ==
          tiles::kNatureStaff);
    CHECK(d_eq.slots[static_cast<std::size_t>(EquipSlot::Helmet)] ==
          tiles::kWolfHood);
    CHECK(d_eq.slots[static_cast<std::size_t>(EquipSlot::Amulet)] ==
          tiles::kMoonAmulet);
}
