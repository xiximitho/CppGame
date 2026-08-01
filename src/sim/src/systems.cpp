#include "sim/systems.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "sim/components.hpp"
#include "sim/item_type.hpp"
#include "sim/weapon.hpp"

namespace sim {

CombatStats combat_stats(const World& world, entt::entity actor) {
    CombatStats stats;
    const entt::registry& registry = world.registry();
    const ItemTypeRegistry& items = world.item_types();

    VocationId vocation = kVocationNone;
    if (const auto* voc = registry.try_get<CVocation>(actor)) {
        vocation = voc->id;
    }

    // Innate stats first: a monster class's damage and reach live here, gear adds
    // to them. A player has no CCombat, so this is a no-op for players.
    if (const auto* innate = registry.try_get<CCombat>(actor)) {
        stats.attack = static_cast<std::int32_t>(stats.attack + innate->attack);
        stats.defense = static_cast<std::int32_t>(stats.defense + innate->defense);
        stats.range = innate->range;
        stats.effect = innate->effect;
    }

    if (const auto* equipment = registry.try_get<CEquipment>(actor)) {
        for (const ItemTypeId id : equipment->slots) {
            if (id == kItemNone) {
                continue;
            }
            const ItemType& type = items.get(id);
            stats.defense =
                static_cast<std::int32_t>(stats.defense + type.defense);
            if (type.is_weapon()) {
                const int percent =
                    vocation_weapon_percent(vocation, weapon_family(id));
                const auto scaled = static_cast<std::int32_t>(
                    (static_cast<std::int32_t>(type.attack) * percent) / 100);
                stats.attack = static_cast<std::int32_t>(stats.attack + scaled);
                stats.range = type.attack_range;
                stats.effect = type.effect;
            }
        }
    }
    return stats;
}

entt::entity spawn_monster(World& world, MonsterTypeId type, TilePos at) {
    const MonsterType& spec = world.monsters().get(type);
    if (spec.id == kMonsterNone) {
        return entt::null;
    }

    const NetId net_id = world.allocate_net_id();
    const entt::entity entity =
        world.spawn_actor(net_id, at, spec.appearance, spec.step_ticks);
    entt::registry& registry = world.registry();

    registry.emplace_or_replace<CHealth>(entity, CHealth{spec.max_hp,
                                                        spec.max_hp});
    registry.emplace<CCombat>(entity, CCombat{spec.attack, spec.defense,
                                              spec.attack_range, spec.effect});
    registry.emplace<CMonster>(entity, CMonster{spec.id, at, spec.aggro_radius,
                                                spec.leash, 0});
    // Loot lives on the class table and is rolled into a corpse on death — not
    // pre-loaded into CInventory (that made every kill a guaranteed drop and
    // looked like the mob was carrying the item).
    // Deliberately no CRespawn: killing a monster removes it. Players come back,
    // monsters do not, and a repopulation rule is a spawner's job, not a corpse's.
    return entity;
}

int spawn_authored_monsters(World& world, const std::vector<MonsterSpawn>& list) {
    int placed = 0;
    for (const MonsterSpawn& entry : list) {
        if (!world.map().is_walkable(entry.tile) ||
            world.occupant(entry.tile) != kInvalidNetId) {
            continue;
        }
        if (spawn_monster(world, entry.type, entry.tile) != entt::null) {
            ++placed;
        }
    }
    return placed;
}

entt::entity spawn_random_monster(World& world, Rng& rng, TilePos at) {
    const std::vector<MonsterTypeId> ids = world.monsters().ids();
    if (ids.empty()) {
        return entt::null;
    }
    const auto pick = static_cast<std::size_t>(rng.next_below(
        static_cast<std::uint32_t>(ids.size())));
    return spawn_monster(world, ids[pick], at);
}

int create_spawners(World& world, const std::vector<SpawnerSpec>& list) {
    int created = 0;
    entt::registry& registry = world.registry();
    for (const SpawnerSpec& spec : list) {
        if (!world.monsters().contains(spec.type)) {
            continue;
        }
        const entt::entity entity = registry.create();
        registry.emplace<CSpawner>(
            entity,
            CSpawner{spec.tile, spec.type, spec.max_alive, spec.radius,
                     static_cast<Tick>(spec.respawn_seconds *
                                       static_cast<std::uint32_t>(kSimHz)),
                     0, {}});
        ++created;
    }
    return created;
}

void update_spawners(World& world, Rng& rng) {
    const Tick now = world.tick();

    // Gathered then applied, like every other system here: spawning creates
    // entities, and creating entities while iterating a view of them is how you get
    // an invalidated iterator.
    struct Birth {
        entt::entity spawner;
        TilePos      tile;
    };
    std::vector<Birth> births;

    for (auto [entity, spawner] : world.registry().view<CSpawner>().each()) {
        // Forget the dead first, so the population count is about what is actually
        // alive and not about what was ever created.
        const std::size_t before = spawner.children.size();
        std::erase_if(spawner.children, [&world](NetId id) {
            return world.lookup(id) == entt::null;
        });
        if (spawner.children.size() != before) {
            // A death starts the clock. Setting it here rather than at spawn time is
            // what makes "clear the nest and it stays clear for a while" true.
            spawner.next_spawn_tick = now + spawner.respawn_ticks;
        }

        if (spawner.children.size() >= spawner.max_alive ||
            now < spawner.next_spawn_tick) {
            continue;
        }

        // A free tile within the radius, by rejection sampling. Giving up after a
        // few tries and retrying next tick beats scanning the whole disc every time
        // a nest is boxed in by the player standing on it.
        const int radius = static_cast<int>(spawner.radius);
        for (int attempt = 0; attempt < 12; ++attempt) {
            const TilePos candidate{
                static_cast<std::int16_t>(spawner.tile.x +
                                          rng.next_range(-radius, radius)),
                static_cast<std::int16_t>(spawner.tile.y +
                                          rng.next_range(-radius, radius)),
                spawner.tile.z};
            if (world.map().is_walkable(candidate) &&
                world.occupant(candidate) == kInvalidNetId) {
                births.push_back({entity, candidate});
                break;
            }
        }
    }

    for (const Birth& birth : births) {
        auto& spawner = world.registry().get<CSpawner>(birth.spawner);
        const entt::entity child = spawn_monster(world, spawner.type, birth.tile);
        if (child == entt::null) {
            continue;
        }
        spawner.children.push_back(world.registry().get<CActor>(child).net_id);
        // The next one waits a full period even if this one was instant, so a
        // spawner with several slots trickles instead of dumping its whole
        // population on the first tick.
        spawner.next_spawn_tick = now + spawner.respawn_ticks;
    }
}

namespace {

/// The closest actor a monster is willing to fight: anything that is not itself a
/// monster, alive, and on the same floor. Distance is sim::tile_distance, the same
/// Chebyshev rule reach and aggro are counted with everywhere else.
entt::entity nearest_prey(const entt::registry& registry, TilePos from,
                          int max_distance) {
    entt::entity best = entt::null;
    int best_distance = max_distance + 1;

    const auto view =
        registry.view<CPosition, CActor>(entt::exclude<CMonster, CDead>);
    for (const entt::entity entity : view) {
        const int distance = tile_distance(from, view.get<CPosition>(entity).tile);
        if (distance >= 0 && distance < best_distance) {  // -1 = another floor
            best_distance = distance;
            best = entity;
        }
    }
    return best;
}

}  // namespace

void update_monsters(World& world, Rng& rng) {
    entt::registry& registry = world.registry();
    const Tick now = world.tick();

    // Decisions are gathered before any are applied: request_walk and
    // request_move_to add components, which would invalidate a view being iterated.
    struct Decision {
        NetId     net_id       = kInvalidNetId;
        NetId     attack       = kInvalidNetId;
        NetId     follow       = kInvalidNetId;
        bool      clear_attack = false;
        bool      step         = false;
        Direction step_dir     = Direction::South;
    };
    std::vector<Decision> decisions;

    for (auto [entity, monster, actor, pos] :
         registry.view<CMonster, CActor, CPosition>().each()) {
        if (registry.all_of<CDead>(entity) || registry.all_of<CWalk>(entity)) {
            continue;
        }
        if (now < monster.next_decision_tick) {
            continue;
        }
        // A tenth of a second between thoughts. Fast enough that a chase reads as
        // reactive, slow enough that A* is not run 30 times a second per mob.
        monster.next_decision_tick = now + static_cast<Tick>(kSimHz / 10);

        Decision decision;
        decision.net_id = actor.net_id;

        // Hysteresis: it gives up at 1.5x the radius it noticed you at, so walking
        // along the edge of aggro does not make it start and stop every thought.
        const int notice = static_cast<int>(monster.aggro_radius);
        const int forget = notice + notice / 2;
        const entt::entity prey =
            monster.aggro_radius == 0
                ? entt::null
                : nearest_prey(registry, pos.tile, forget);

        if (prey != entt::null) {
            const TilePos prey_tile = registry.get<CPosition>(prey).tile;
            const NetId prey_id = registry.get<CActor>(prey).net_id;
            const int distance = tile_distance(pos.tile, prey_tile);
            const bool engaged = registry.all_of<CTarget>(entity);

            if (distance <= notice || engaged) {
                // Attack AND chase. The walking is update_chasers' job, which
                // replans when the prey moves — a monster that stepped toward the
                // last known tile got stuck on wall corners and stopped swinging.
                decision.attack = prey_id;
                decision.follow = prey_id;
                decisions.push_back(decision);
                continue;
            }
        }

        // Nothing to chase: forget the old target and drift near home.
        decision.clear_attack = true;
        if (rng.next_below(3) == 0) {  // idles two thoughts out of three
            const auto dir = static_cast<Direction>(rng.next_below(kDirectionCount));
            const TileDelta delta = direction_delta(dir);
            const TilePos next{static_cast<std::int16_t>(pos.tile.x + delta.dx),
                               static_cast<std::int16_t>(pos.tile.y + delta.dy),
                               pos.tile.z};
            if (tile_distance(next, monster.home) <=
                static_cast<int>(monster.leash)) {
                decision.step = true;
                decision.step_dir = dir;
            }
        }
        decisions.push_back(decision);
    }

    for (const Decision& decision : decisions) {
        if (decision.clear_attack) {
            world.set_attack_target(decision.net_id, kInvalidNetId);
            world.request_follow(decision.net_id, kInvalidNetId);
        } else if (decision.attack != kInvalidNetId) {
            world.set_attack_target(decision.net_id, decision.attack);
            world.request_follow(decision.net_id, decision.follow);
        }
        if (decision.step) {
            // Wandering only. cancel_path here is safe because a wanderer has
            // nothing to chase — the branch above returned for anything engaged.
            world.cancel_path(decision.net_id);
            world.request_walk(decision.net_id, decision.step_dir);
        }
    }
}

void update_combat(World& world) {
    entt::registry& registry = world.registry();
    const Tick now = world.tick();

    // Respawns first: gather then apply, since respawn_actor mutates pools.
    std::vector<NetId> respawns;
    for (auto [entity, dead, actor] : registry.view<CDead, CActor>().each()) {
        if (now >= dead.respawn_tick) {
            respawns.push_back(actor.net_id);
        }
    }
    for (const NetId id : respawns) {
        world.respawn_actor(id);
    }

    // Resolve targets. Facing and the swing timer are plain field writes (safe
    // mid-view); the actual hits are collected and applied after, because
    // apply_damage can despawn an entity and invalidate the view.
    struct Swing {
        NetId        target;
        std::int32_t damage;
        TilePos      from;
        TilePos      to;
        std::uint8_t effect;
    };
    std::vector<Swing>        swings;
    std::vector<entt::entity> clear_targets;

    for (auto [entity, target, pos] :
         registry.view<CTarget, CPosition>().each()) {
        if (registry.all_of<CDead>(entity)) {
            continue;  // the dead do not swing
        }

        const entt::entity target_entity = world.lookup(target.target);
        if (target_entity == entt::null ||
            registry.all_of<CDead>(target_entity) ||
            !registry.all_of<CPosition>(target_entity)) {
            clear_targets.push_back(entity);  // target gone or dead
            continue;
        }

        const CombatStats attacker = combat_stats(world, entity);
        const TilePos target_tile = registry.get<CPosition>(target_entity).tile;
        if (!in_attack_range(pos.tile, target_tile, attacker.range)) {
            continue;  // keep the target, wait until it is in reach
        }

        pos.facing = direction_towards(pos.tile, target_tile);

        if (now < target.next_swing_tick) {
            continue;
        }
        target.next_swing_tick = now + kAttackCooldownTicks;

        const std::int32_t defense = combat_stats(world, target_entity).defense;
        const std::int32_t damage =
            std::max<std::int32_t>(1, attacker.attack - defense);
        swings.push_back(Swing{target.target, damage, pos.tile, target_tile,
                               attacker.effect});
    }

    for (const entt::entity entity : clear_targets) {
        registry.remove<CTarget>(entity);
    }
    for (const Swing& swing : swings) {
        world.apply_damage(swing.target, swing.damage);
        world.push_attack_event(AttackEvent{swing.from, swing.to, swing.effect});
    }
}

void update_chasers(World& world) {
    entt::registry& registry = world.registry();
    const Tick now = world.tick();

    struct Plan {
        NetId   net_id;
        TilePos target_tile;
    };
    std::vector<Plan>         plans;
    std::vector<NetId>        stop;
    std::vector<entt::entity> give_up;

    for (auto [entity, follow, pos, actor] :
         registry.view<CFollow, CPosition, CActor>().each()) {
        if (registry.all_of<CDead>(entity)) {
            continue;
        }

        const entt::entity target = world.lookup(follow.target);
        if (target == entt::null || registry.all_of<CDead>(target) ||
            !registry.all_of<CPosition>(target)) {
            give_up.push_back(entity);   // nothing left to chase
            continue;
        }

        const TilePos target_tile = registry.get<CPosition>(target).tile;
        const auto reach = static_cast<int>(combat_stats(world, entity).range);
        const int distance = tile_distance(pos.tile, target_tile);

        // Close enough to swing: stand still and let update_combat work. Walking
        // the last step would be refused anyway — the target occupies its tile.
        if (distance >= 0 && distance <= reach) {
            stop.push_back(actor.net_id);
            continue;
        }

        if (registry.all_of<CWalk>(entity)) {
            continue;  // mid-step; decide when it lands
        }

        // Replan when the target moved, when there is no route left (the follower
        // gave up or finished), or on a slow heartbeat as a backstop.
        const bool moved = follow.planned_for != target_tile;
        const bool routeless = !registry.all_of<CPathFollow>(entity);
        if (!moved && !routeless && now < follow.next_replan_tick) {
            continue;
        }

        follow.planned_for = target_tile;
        follow.next_replan_tick = now + static_cast<Tick>(kSimHz / 4);
        plans.push_back({actor.net_id, target_tile});
    }

    for (const NetId id : stop) {
        world.stop_path(id);
    }
    for (const Plan& plan : plans) {
        if (!world.request_move_to(plan.net_id, plan.target_tile)) {
            // No route at all (a lake in the way, another floor). Back off a beat
            // instead of running A* every quarter second against a wall.
            const entt::entity entity = world.lookup(plan.net_id);
            if (entity != entt::null) {
                if (auto* follow = world.registry().try_get<CFollow>(entity)) {
                    follow->next_replan_tick = now + static_cast<Tick>(kSimHz);
                }
            }
        }
    }
    for (const entt::entity entity : give_up) {
        registry.remove<CFollow>(entity);
    }
}

void update_path_followers(World& world) {
    entt::registry& registry = world.registry();

    // Two phases, as everywhere else in this file: request_walk() adds a CWalk
    // component, which would invalidate the view being iterated.
    struct Attempt {
        NetId        net_id;
        entt::entity entity;
        Direction    dir;
    };
    std::vector<Attempt>      attempts;
    std::vector<entt::entity> finished;

    const auto view = registry.view<CPathFollow, CPosition, CActor>();
    for (const entt::entity entity : view) {
        // Mid-step: nothing to do until it lands.
        if (registry.all_of<CWalk>(entity)) {
            continue;
        }

        const CPathFollow& follow = view.get<CPathFollow>(entity);
        if (follow.next >= follow.path.size()) {
            finished.push_back(entity);
            continue;
        }

        const TilePos current = view.get<CPosition>(entity).tile;
        const TilePos want = follow.path[follow.next];

        Direction dir = Direction::South;
        if (!direction_between(current, want, dir)) {
            // The actor is not adjacent to the next tile of its route, so the route
            // no longer describes where it is — teleported, shoved, or the path
            // outlived the actor's position. Drop it rather than walk nonsense.
            finished.push_back(entity);
            continue;
        }

        attempts.push_back(Attempt{view.get<CActor>(entity).net_id, entity, dir});
    }

    for (const Attempt& attempt : attempts) {
        if (world.request_walk(attempt.net_id, attempt.dir)) {
            auto& follow = registry.get<CPathFollow>(attempt.entity);
            ++follow.next;
            follow.blocked_ticks = 0;
            if (follow.next >= follow.path.size()) {
                finished.push_back(attempt.entity);
            }
            continue;
        }

        auto& follow = registry.get<CPathFollow>(attempt.entity);
        if (++follow.blocked_ticks >= kPathBlockedGiveUpTicks) {
            finished.push_back(attempt.entity);
        }
    }

    for (const entt::entity entity : finished) {
        // An entity can land here twice (arrived and then flagged again), and
        // remove() on an absent component is a no-op, so this needs no dedup.
        registry.remove<CPathFollow>(entity);
    }
}

}  // namespace sim
