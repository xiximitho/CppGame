#include "sim/world.hpp"

#include <utility>
#include <vector>

#include "sim/loot.hpp"

namespace sim {

World::World(TileMap map, ItemTypeRegistry item_types, MonsterRegistry monsters,
             std::uint64_t rng_seed)
    : map_(std::move(map)),
      item_types_(std::move(item_types)),
      monsters_(std::move(monsters)),
      rng_(rng_seed) {}

std::uint64_t World::tile_key(TilePos pos) {
    // Bias into unsigned so negative coordinates still pack cleanly.
    const auto ux = static_cast<std::uint64_t>(static_cast<std::uint16_t>(pos.x));
    const auto uy = static_cast<std::uint64_t>(static_cast<std::uint16_t>(pos.y));
    const auto uz = static_cast<std::uint64_t>(static_cast<std::uint8_t>(pos.z));
    return (uz << 32U) | (uy << 16U) | ux;
}

void World::occupy(TilePos pos, NetId net_id) {
    occupancy_[tile_key(pos)] = net_id;
}

void World::vacate(TilePos pos, NetId net_id) {
    const auto it = occupancy_.find(tile_key(pos));
    // Guarded: a tile is only released by whoever actually holds it, so an
    // actor arriving on a tile another actor already left cannot erase it.
    if (it != occupancy_.end() && it->second == net_id) {
        occupancy_.erase(it);
    }
}

NetId World::allocate_net_id() {
    return next_net_id_++;
}

entt::entity World::spawn_actor(NetId net_id, TilePos at,
                               std::uint16_t appearance, Tick step_ticks) {
    const entt::entity entity = registry_.create();

    registry_.emplace<CPosition>(entity, CPosition{at, Direction::South});
    registry_.emplace<CActor>(entity, CActor{net_id, appearance, step_ticks});
    registry_.emplace<CHealth>(entity, CHealth{100, 100});

    by_net_id_[net_id] = entity;
    occupy(at, net_id);

    return entity;
}

void World::despawn(NetId net_id) {
    const auto it = by_net_id_.find(net_id);
    if (it == by_net_id_.end()) {
        return;
    }
    const entt::entity entity = it->second;

    if (const auto* pos = registry_.try_get<CPosition>(entity)) {
        vacate(pos->tile, net_id);
    }
    // A walking actor also holds its destination tile.
    if (const auto* walk = registry_.try_get<CWalk>(entity)) {
        vacate(walk->to, net_id);
    }

    registry_.destroy(entity);
    by_net_id_.erase(it);
}

entt::entity World::lookup(NetId net_id) const {
    const auto it = by_net_id_.find(net_id);
    return it == by_net_id_.end() ? entt::null : it->second;
}

NetId World::occupant(TilePos pos) const {
    const auto it = occupancy_.find(tile_key(pos));
    return it == occupancy_.end() ? kInvalidNetId : it->second;
}

bool World::can_enter(TilePos from, TilePos to, Direction dir,
                      NetId mover) const {
    // Static geometry, shared with the pathfinder so the two can never disagree.
    if (!can_traverse(map_, from, dir)) {
        return false;
    }

    const NetId other = occupant(to);
    if (other != kInvalidNetId && other != mover) {
        return false;
    }

    return true;
}

bool World::request_walk(NetId net_id, Direction dir) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return false;
    }

    auto* pos = registry_.try_get<CPosition>(entity);
    if (pos == nullptr) {
        return false;
    }

    // Turning happens even when the step is refused.
    pos->facing = dir;

    // Already mid-step: input during a step is dropped rather than queued. A
    // one-slot queue here is what you would add for responsive chained walking.
    if (registry_.all_of<CWalk>(entity)) {
        return false;
    }

    const TilePos target = tile_step(pos->tile, dir);
    if (!can_enter(pos->tile, target, dir, net_id)) {
        return false;
    }

    const auto* actor = registry_.try_get<CActor>(entity);
    const Tick base = actor != nullptr ? actor->step_ticks : kDefaultStepTicks;
    const Tick duration = is_diagonal(dir) ? step_ticks_for_diagonal(base) : base;

    registry_.emplace<CWalk>(entity,
                             CWalk{pos->tile, target, tick_, tick_ + duration, dir});

    // The destination is claimed immediately so a second actor cannot start a
    // step into the same tile while this one is in flight.
    vacate(pos->tile, net_id);
    occupy(target, net_id);

    return true;
}

bool World::request_move_to(NetId net_id, TilePos target) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return false;
    }
    const auto* pos = registry_.try_get<CPosition>(entity);
    if (pos == nullptr) {
        return false;
    }

    // Plan from where the actor will be, not where it is. Planning from the tile
    // being left would put the tile it is already walking onto at the head of the
    // route, and the actor would take a visible step backwards to "start" it.
    TilePos origin = pos->tile;
    if (const auto* walk = registry_.try_get<CWalk>(entity)) {
        origin = walk->to;
    }

    std::vector<TilePos> path;
    if (!pathfinder_.find(map_, origin, target, path)) {
        registry_.remove<CPathFollow>(entity);
        return false;
    }

    registry_.emplace_or_replace<CPathFollow>(
        entity, CPathFollow{std::move(path), 0, 0});
    return true;
}

void World::cancel_path(NetId net_id) {
    const entt::entity entity = lookup(net_id);
    if (entity != entt::null) {
        registry_.remove<CPathFollow>(entity);
        // Manual movement also ends a chase. Without this, update_chasers would
        // replan on the next tick and walk the actor straight back, and the player
        // would feel the input being ignored.
        registry_.remove<CFollow>(entity);
    }
}

void World::stop_path(NetId net_id) {
    const entt::entity entity = lookup(net_id);
    if (entity != entt::null) {
        registry_.remove<CPathFollow>(entity);
    }
}

void World::request_follow(NetId net_id, NetId target) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return;
    }
    if (target == kInvalidNetId || target == net_id) {
        registry_.remove<CFollow>(entity);
        return;
    }
    // Re-issuing the same target must not reset the plan, or a client sending the
    // intent every frame would keep the actor replanning and never stepping.
    if (const auto* existing = registry_.try_get<CFollow>(entity)) {
        if (existing->target == target) {
            return;
        }
    }
    registry_.emplace_or_replace<CFollow>(entity, CFollow{target, TilePos{}, 0});
}

bool World::is_following_path(NetId net_id) const {
    const entt::entity entity = lookup(net_id);
    return entity != entt::null && registry_.all_of<CPathFollow>(entity);
}

bool World::request_turn(NetId net_id, Direction dir) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return false;
    }
    auto* pos = registry_.try_get<CPosition>(entity);
    if (pos == nullptr) {
        return false;
    }
    pos->facing = dir;
    return true;
}

namespace {

void add_to_inventory(CInventory& inventory, ItemTypeId id) {
    for (ItemStack& stack : inventory.items) {
        if (stack.id == id) {
            ++stack.count;
            return;
        }
    }
    inventory.items.push_back(ItemStack{id, 1});
}

bool remove_from_inventory(CInventory& inventory, ItemTypeId id) {
    for (auto it = inventory.items.begin(); it != inventory.items.end(); ++it) {
        if (it->id == id) {
            if (--it->count == 0) {
                inventory.items.erase(it);
            }
            return true;
        }
    }
    return false;
}

void merge_stack(std::vector<ItemStack>& into, ItemStack stack) {
    for (ItemStack& existing : into) {
        if (existing.id == stack.id) {
            existing.count = static_cast<std::uint16_t>(existing.count +
                                                        stack.count);
            return;
        }
    }
    into.push_back(stack);
}

}  // namespace

void World::drop_item(TilePos tile, ItemStack stack) {
    if (stack.id == kItemNone || stack.count == 0) {
        return;
    }
    GroundPile& pile = ground_[tile_key(tile)];
    pile.tile = tile;
    merge_stack(pile.items, stack);
}

entt::entity World::spawn_corpse(TilePos tile, std::vector<ItemStack> items) {
    const entt::entity entity = registry_.create();
    registry_.emplace<CPosition>(entity, CPosition{tile, Direction::South});
    registry_.emplace<CCorpse>(entity);
    registry_.emplace<CInventory>(entity, CInventory{std::move(items)});
    return entity;
}

bool World::take_from_corpse(NetId taker, TilePos corpse_tile,
                             std::size_t index) {
    const entt::entity actor = lookup(taker);
    if (actor == entt::null) {
        return false;
    }
    auto* inventory = registry_.try_get<CInventory>(actor);
    const auto* pos = registry_.try_get<CPosition>(actor);
    if (inventory == nullptr || pos == nullptr) {
        return false;
    }
    // Standing on the bag or on an adjacent tile — same reach as melee.
    const int distance = tile_distance(pos->tile, corpse_tile);
    if (distance < 0 || distance > 1) {
        return false;
    }

    entt::entity corpse = entt::null;
    for (const auto [entity, cpos] :
         registry_.view<CCorpse, CPosition>().each()) {
        if (cpos.tile == corpse_tile) {
            corpse = entity;
            break;
        }
    }
    if (corpse == entt::null) {
        return false;
    }
    auto& bag = registry_.get<CInventory>(corpse);
    if (index >= bag.items.size()) {
        return false;
    }

    const ItemStack stack = bag.items[index];
    bag.items.erase(bag.items.begin() +
                    static_cast<std::ptrdiff_t>(index));
    for (std::uint16_t n = 0; n < stack.count; ++n) {
        add_to_inventory(*inventory, stack.id);
    }
    if (bag.items.empty()) {
        registry_.destroy(corpse);
    }
    return true;
}

const std::vector<ItemStack>* World::ground_items_at(TilePos tile) const {
    const auto it = ground_.find(tile_key(tile));
    return it == ground_.end() ? nullptr : &it->second.items;
}

bool World::equip(NetId net_id, ItemTypeId item) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return false;
    }
    auto* inventory = registry_.try_get<CInventory>(entity);
    auto* equipment = registry_.try_get<CEquipment>(entity);
    if (inventory == nullptr || equipment == nullptr) {
        return false;
    }
    const ItemType& type = item_types_.get(item);
    if (!type.equippable) {
        return false;
    }
    if (!remove_from_inventory(*inventory, item)) {
        return false;  // the actor does not actually have it
    }

    const auto index = static_cast<std::size_t>(type.slot);
    const ItemTypeId previous = equipment->slots[index];
    equipment->slots[index] = item;
    if (previous != kItemNone) {
        add_to_inventory(*inventory, previous);  // swap the old one back
    }
    return true;
}

bool World::unequip(NetId net_id, EquipSlot slot) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return false;
    }
    auto* inventory = registry_.try_get<CInventory>(entity);
    auto* equipment = registry_.try_get<CEquipment>(entity);
    if (inventory == nullptr || equipment == nullptr) {
        return false;
    }
    const auto index = static_cast<std::size_t>(slot);
    const ItemTypeId current = equipment->slots[index];
    if (current == kItemNone) {
        return false;
    }
    equipment->slots[index] = kItemNone;
    add_to_inventory(*inventory, current);
    return true;
}

void World::set_attack_target(NetId attacker, NetId target) {
    const entt::entity entity = lookup(attacker);
    if (entity == entt::null) {
        return;
    }
    if (target == kInvalidNetId || target == attacker) {
        registry_.remove<CTarget>(entity);
        return;
    }
    // Re-issuing the SAME target must change nothing. sim::update_monsters calls
    // this a few times a second for an engaged mob, and resetting the swing timer
    // each time let monsters hit every decision tick instead of once per cooldown —
    // roughly ten times the intended damage, from a line that looks harmless.
    if (auto* existing = registry_.try_get<CTarget>(entity)) {
        if (existing->target == target) {
            return;
        }
    }
    // A fresh target is hit promptly, not on the old target's leftover cooldown.
    registry_.emplace_or_replace<CTarget>(entity, CTarget{target, 0});
}

bool World::apply_damage(NetId net_id, std::int32_t amount) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return false;
    }
    auto* health = registry_.try_get<CHealth>(entity);
    if (health == nullptr) {
        return false;
    }

    health->hp -= amount;
    if (health->hp > 0) {
        return false;
    }
    health->hp = 0;

    if (registry_.all_of<CRespawn>(entity)) {
        // Stop occupying and acting; the corpse waits out CDead in place.
        if (const auto* pos = registry_.try_get<CPosition>(entity)) {
            vacate(pos->tile, net_id);
        }
        if (const auto* walk = registry_.try_get<CWalk>(entity)) {
            vacate(walk->to, net_id);
        }
        registry_.remove<CWalk>(entity);
        registry_.remove<CPathFollow>(entity);
        registry_.remove<CTarget>(entity);
        registry_.emplace_or_replace<CDead>(entity, CDead{tick_ + kRespawnTicks});
    } else {
        // Monster death: roll the class table into a phantom corpse (plus anything
        // it was carrying). The corpse does not occupy the tile.
        if (const auto* pos = registry_.try_get<CPosition>(entity)) {
            const TilePos where = pos->tile;
            std::vector<ItemStack> loot;
            if (const auto* monster = registry_.try_get<CMonster>(entity)) {
                loot = roll_monster_loot(monsters_.get(monster->type), rng_);
            }
            if (const auto* inventory = registry_.try_get<CInventory>(entity)) {
                for (const ItemStack& stack : inventory->items) {
                    loot.push_back(stack);
                }
            }
            if (const auto* equipment = registry_.try_get<CEquipment>(entity)) {
                for (const ItemTypeId id : equipment->slots) {
                    if (id != kItemNone) {
                        loot.push_back(ItemStack{id, 1});
                    }
                }
            }
            if (!loot.empty()) {
                spawn_corpse(where, std::move(loot));
            }
        }
        despawn(net_id);
    }
    return true;
}

void World::respawn_actor(NetId net_id) {
    const entt::entity entity = lookup(net_id);
    if (entity == entt::null) {
        return;
    }
    const auto* respawn = registry_.try_get<CRespawn>(entity);
    auto* pos = registry_.try_get<CPosition>(entity);
    if (respawn == nullptr || pos == nullptr) {
        return;
    }
    // The dead actor did not occupy a tile, so just claim the respawn point.
    pos->tile = respawn->point;
    pos->facing = Direction::South;
    occupy(respawn->point, net_id);
    if (auto* health = registry_.try_get<CHealth>(entity)) {
        health->hp = health->max_hp;
    }
    registry_.remove<CDead>(entity);
}

void World::apply_stairs(entt::entity entity) {
    auto& pos = registry_.get<CPosition>(entity);
    const int delta_z = item_types_.get(map_.at(pos.tile).object).stair_delta_z();
    if (delta_z == 0) {
        return;
    }

    const TilePos destination{pos.tile.x, pos.tile.y,
                              static_cast<std::int8_t>(pos.tile.z + delta_z)};
    const NetId mover = registry_.get<CActor>(entity).net_id;

    // A stair leading into rock, off the top of the map, or onto an occupied tile
    // does nothing: the actor stays where it is. Refusing beats teleporting into
    // a wall, and beats a half-applied move that leaves occupancy lying.
    if (!map_.is_walkable(destination)) {
        return;
    }
    const NetId other = occupant(destination);
    if (other != kInvalidNetId && other != mover) {
        return;
    }

    vacate(pos.tile, mover);
    occupy(destination, mover);
    pos.tile = destination;

    // A route was planned on the floor left behind, so every tile still in it is
    // on the wrong floor. Dropping it is the only honest option; the follower
    // would otherwise walk the actor through geometry it never checked.
    registry_.remove<CPathFollow>(entity);
}

void World::step() {
    ++tick_;

    // Collected first rather than removed during iteration: mutating the pool a
    // view is walking is how you get a dangling iterator in any ECS.
    std::vector<entt::entity> arrived;
    for (auto [entity, walk] : registry_.view<CWalk>().each()) {
        if (tick_ >= walk.end_tick) {
            arrived.push_back(entity);
        }
    }

    for (const entt::entity entity : arrived) {
        const auto& walk = registry_.get<CWalk>(entity);
        auto& pos = registry_.get<CPosition>(entity);
        pos.tile = walk.to;
        registry_.erase<CWalk>(entity);

        // Walk over ground piles only. Phantom corpses are opened by click
        // (take_from_corpse); auto-scooping them would skip the loot bag UI.
        if (auto* inventory = registry_.try_get<CInventory>(entity)) {
            const auto pile = ground_.find(tile_key(pos.tile));
            if (pile != ground_.end()) {
                for (const ItemStack& stack : pile->second.items) {
                    for (std::uint16_t n = 0; n < stack.count; ++n) {
                        add_to_inventory(*inventory, stack.id);
                    }
                }
                ground_.erase(pile);
            }
        }

        // Stairs act on ARRIVAL BY WALKING, and that is what keeps a symmetric
        // pair from ping-ponging: being placed on the down-stair above is not a
        // walk arrival, so it does not immediately send the actor back. Stepping
        // onto that same tile later does, which is the behaviour you want.
        apply_stairs(entity);
    }
}

}  // namespace sim
