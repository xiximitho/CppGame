#include <doctest/doctest.h>

#include "sim/components.hpp"
#include "sim/systems.hpp"
#include "sim/tile_ids.hpp"
#include "sim/tile_map.hpp"
#include "sim/world.hpp"

using namespace sim;

namespace {

/// A small all-walkable stone map.
World make_open_world() {
    TileMap map(12, 12, 1);
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 12; ++x) {
            map.set_ground(TilePos{static_cast<std::int16_t>(x),
                                   static_cast<std::int16_t>(y), 0},
                           tiles::kStone);
        }
    }
    return World(std::move(map));
}

std::int32_t hp_of(World& world, NetId id) {
    return world.registry().get<CHealth>(world.lookup(id)).hp;
}

// One simulation tick with combat resolved after it, as the sessions do.
void tick(World& world) {
    world.step();
    update_combat(world);
}

}  // namespace

TEST_CASE("an adjacent target takes damage on cooldown and a monster despawns") {
    World world = make_open_world();
    const NetId attacker = world.allocate_net_id();
    world.spawn_actor(attacker, TilePos{5, 5, 0}, 0);
    const NetId target = world.allocate_net_id();
    world.spawn_actor(target, TilePos{6, 5, 0}, 0);

    world.set_attack_target(attacker, target);

    // First tick lands the first swing (cooldown starts at 0).
    tick(world);
    CHECK(hp_of(world, target) == 100 - kBaseMeleeDamage);

    // No second swing until the cooldown elapses.
    tick(world);
    CHECK(hp_of(world, target) == 100 - kBaseMeleeDamage);

    // The attacker turned to face the target (east).
    CHECK(world.registry().get<CPosition>(world.lookup(attacker)).facing ==
          Direction::East);

    // Run well past enough swings to kill it. No CRespawn -> despawned.
    for (int i = 0; i < 220; ++i) {
        tick(world);
    }
    CHECK((world.lookup(target) == entt::null));
    // The attacker's now-dangling target was dropped.
    CHECK_FALSE(world.registry().all_of<CTarget>(world.lookup(attacker)));
}

TEST_CASE("a target out of melee range is never hit") {
    World world = make_open_world();
    const NetId attacker = world.allocate_net_id();
    world.spawn_actor(attacker, TilePos{2, 2, 0}, 0);
    const NetId target = world.allocate_net_id();
    world.spawn_actor(target, TilePos{5, 2, 0}, 0);  // three tiles away

    world.set_attack_target(attacker, target);
    for (int i = 0; i < 120; ++i) {
        tick(world);
    }
    CHECK(hp_of(world, target) == 100);
}

// Same open map, but the World carries the real catalogue so equipped-item
// stats resolve.
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

void equip(World& world, NetId id, EquipSlot slot, ItemTypeId item) {
    CEquipment& eq = world.registry().get_or_emplace<CEquipment>(world.lookup(id));
    eq.slots[static_cast<std::size_t>(slot)] = item;
}

TEST_CASE("equipped weapon and armour change the damage dealt") {
    World world = make_armed_world();
    const NetId attacker = world.allocate_net_id();
    world.spawn_actor(attacker, TilePos{5, 5, 0}, 0);
    const NetId target = world.allocate_net_id();
    world.spawn_actor(target, TilePos{6, 5, 0}, 0);

    equip(world, attacker, EquipSlot::Weapon, tiles::kSword);   // +12 attack
    equip(world, target, EquipSlot::Body, tiles::kArmor);       // +8 defense

    world.set_attack_target(attacker, target);
    tick(world);  // first swing

    // (base 18 + sword 12) - armour 8 = 22.
    CHECK(hp_of(world, target) == 100 - (kBaseMeleeDamage + 12 - 8));
}

TEST_CASE("a bow reaches a target three tiles away that a punch cannot") {
    World world = make_armed_world();
    const NetId attacker = world.allocate_net_id();
    world.spawn_actor(attacker, TilePos{2, 2, 0}, 0);
    const NetId target = world.allocate_net_id();
    world.spawn_actor(target, TilePos{5, 2, 0}, 0);  // three tiles away

    world.set_attack_target(attacker, target);

    // Unarmed (melee range 1) never lands.
    for (int i = 0; i < 60; ++i) {
        tick(world);
    }
    CHECK(hp_of(world, target) == 100);

    // With a bow (range 4) it does, and records an attack event to render.
    equip(world, attacker, EquipSlot::Weapon, tiles::kBow);
    tick(world);
    CHECK(hp_of(world, target) < 100);
    REQUIRE_FALSE(world.attack_events().empty());
    CHECK(world.attack_events().back().effect == kEffectRangedShot);
}

TEST_CASE("equipping from the backpack swaps gear and changes reach") {
    World world = make_armed_world();
    const NetId who = world.allocate_net_id();
    const entt::entity entity = world.spawn_actor(who, TilePos{5, 5, 0}, 0);
    world.registry().emplace<CEquipment>(entity).slots[static_cast<std::size_t>(
        EquipSlot::Weapon)] = tiles::kSword;
    world.registry().emplace<CInventory>(
        entity, CInventory{{{tiles::kBow, 1}}});

    CHECK(combat_stats(world, entity).range == 1);  // sword: melee

    REQUIRE(world.equip(who, tiles::kBow));
    CHECK(combat_stats(world, entity).range == 4);  // bow now reaches farther
    const auto& inventory = world.registry().get<CInventory>(entity);
    REQUIRE(inventory.items.size() == 1);
    CHECK(inventory.items[0].id == tiles::kSword);  // sword swapped back

    REQUIRE(world.unequip(who, EquipSlot::Weapon));
    CHECK(combat_stats(world, entity).range == 1);  // unarmed again
    CHECK_FALSE(world.unequip(who, EquipSlot::Weapon));  // already empty
}

TEST_CASE("a slain monster drops loot, picked up by walking over it") {
    World world = make_armed_world();
    const NetId hero = world.allocate_net_id();
    world.spawn_actor(hero, TilePos{5, 5, 0}, 0);
    world.registry().emplace<CInventory>(world.lookup(hero));  // empty pack

    const NetId mob = world.allocate_net_id();
    const entt::entity mob_entity = world.spawn_actor(mob, TilePos{6, 5, 0}, 0);
    world.registry().emplace<CInventory>(
        mob_entity, CInventory{{{tiles::kShield, 1}}});  // carries loot

    world.set_attack_target(hero, mob);
    for (int i = 0; i < 220; ++i) {  // beat it down
        tick(world);
    }
    REQUIRE((world.lookup(mob) == entt::null));  // no CRespawn -> despawned

    const std::vector<ItemStack>* pile = world.ground_items_at(TilePos{6, 5, 0});
    REQUIRE(pile != nullptr);
    REQUIRE(pile->size() == 1);
    CHECK(pile->front().id == tiles::kShield);

    // Walk the hero onto the loot tile; step() picks it up on arrival.
    world.request_walk(hero, Direction::East);
    for (int i = 0; i < static_cast<int>(kDefaultStepTicks) + 3; ++i) {
        tick(world);
    }
    CHECK(world.ground_items_at(TilePos{6, 5, 0}) == nullptr);
    const auto& pack = world.registry().get<CInventory>(world.lookup(hero));
    REQUIRE(pack.items.size() == 1);
    CHECK(pack.items[0].id == tiles::kShield);
}

TEST_CASE("a player (CRespawn) dies then respawns at its point") {
    World world = make_open_world();
    const NetId attacker = world.allocate_net_id();
    world.spawn_actor(attacker, TilePos{5, 5, 0}, 0);
    const NetId player = world.allocate_net_id();
    const entt::entity player_entity =
        world.spawn_actor(player, TilePos{6, 5, 0}, 0);
    world.registry().emplace<CRespawn>(player_entity, CRespawn{TilePos{1, 1, 0}});

    world.set_attack_target(attacker, player);

    // Kill it.
    int guard = 0;
    while (!world.registry().all_of<CDead>(world.lookup(player)) &&
           guard++ < 400) {
        tick(world);
    }
    REQUIRE(world.registry().all_of<CDead>(world.lookup(player)));
    CHECK(hp_of(world, player) == 0);
    // Dead: no longer occupies its tile, so it does not block movement.
    CHECK(world.occupant(TilePos{6, 5, 0}) == kInvalidNetId);

    // Wait out the respawn timer.
    for (int i = 0; i < static_cast<int>(kRespawnTicks) + 5; ++i) {
        tick(world);
    }
    CHECK_FALSE(world.registry().all_of<CDead>(world.lookup(player)));
    CHECK(hp_of(world, player) == 100);
    const CPosition& pos = world.registry().get<CPosition>(world.lookup(player));
    CHECK(pos.tile.x == 1);
    CHECK(pos.tile.y == 1);
    CHECK(world.occupant(TilePos{1, 1, 0}) == player);
}
