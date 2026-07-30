#include "store/content.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

#include "core/log.hpp"
#include "sim/tile_ids.hpp"
#include "store/schema.hpp"

namespace store {
namespace {

/// Column order shared by every read and write below. Written once so a new column
/// cannot be appended to one statement and forgotten in the other.
constexpr char kItemColumns[] =
    "id, name, flags, weight, max_stack, equippable, slot, attack, defense, "
    "attack_kind, attack_range, effect";

/// Id bands. Convention for humans, not meaning the simulation reads — see
/// ItemCategory in the header and docs/authoring.md.
struct Band {
    sim::ItemTypeId first;
    sim::ItemTypeId last;
};

Band band_for(ItemCategory category) {
    switch (category) {
        case ItemCategory::Ground:    return Band{1U, 99U};
        case ItemCategory::Object:    return Band{100U, 299U};
        case ItemCategory::Equipment: break;
    }
    return Band{300U, 65535U};
}

ItemRow row_from(const Stmt& stmt) {
    ItemRow row;
    row.type.id = static_cast<sim::ItemTypeId>(stmt.column_int(0));
    row.name = stmt.column_text(1);
    row.type.flags =
        sim::ItemFlags{static_cast<std::uint32_t>(stmt.column_int(2))};
    row.type.weight = static_cast<std::uint16_t>(stmt.column_int(3));
    row.type.max_stack = static_cast<std::uint8_t>(stmt.column_int(4));
    row.type.equippable = stmt.column_int(5) != 0;
    row.type.slot = static_cast<sim::EquipSlot>(stmt.column_int(6));
    row.type.attack = static_cast<std::int16_t>(stmt.column_int(7));
    row.type.defense = static_cast<std::int16_t>(stmt.column_int(8));
    row.type.attack_kind = stmt.column_int(9) != 0 ? sim::AttackKind::Ranged
                                                   : sim::AttackKind::Melee;
    row.type.attack_range = static_cast<std::uint8_t>(stmt.column_int(10));
    row.type.effect = static_cast<std::uint8_t>(stmt.column_int(11));
    return row;
}

/// The built-in catalogue's names. sim::build_default_registry() has the rules but
/// no names — it cannot have them, names are not a simulation concern — so the two
/// are joined here, once, to seed the database.
struct SeedName {
    sim::ItemTypeId id;
    std::string_view name;
};

constexpr std::array<SeedName, 16> kSeedNames{{
    {sim::tiles::kGrass, "grass"},
    {sim::tiles::kDirt, "dirt"},
    {sim::tiles::kStone, "stone"},
    {sim::tiles::kWater, "water"},
    {sim::tiles::kWall, "wall"},
    {sim::tiles::kTree, "tree"},
    {sim::tiles::kCrate, "crate"},
    {sim::tiles::kSword, "sword"},
    {sim::tiles::kBow, "bow"},
    {sim::tiles::kShield, "shield"},
    {sim::tiles::kHelmet, "helmet"},
    {sim::tiles::kArmor, "armor"},
    {sim::tiles::kLegs, "legs"},
    {sim::tiles::kBoots, "boots"},
    {sim::tiles::kRing, "ring"},
    {sim::tiles::kAmulet, "amulet"},
}};

std::string_view name_for(sim::ItemTypeId id) {
    for (const SeedName& entry : kSeedNames) {
        if (entry.id == id) {
            return entry.name;
        }
    }
    return "unnamed";
}

}  // namespace

bool load_item_rows(Db& db, std::vector<ItemRow>& out) {
    Stmt stmt =
        db.prepare(std::string("SELECT ") + kItemColumns + " FROM items ORDER BY id");
    if (!stmt.valid()) {
        return false;
    }

    std::vector<ItemRow> rows;
    while (stmt.step()) {
        rows.push_back(row_from(stmt));
    }
    if (stmt.failed()) {
        return false;
    }

    out = std::move(rows);
    return true;
}

bool load_item_types(Db& db, sim::ItemTypeRegistry& out) {
    std::vector<ItemRow> rows;
    if (!load_item_rows(db, rows)) {
        return false;
    }

    // Built locally and moved on success: a registry must never end up holding
    // half a catalogue, because "nothing blocks here" is a valid-looking answer.
    sim::ItemTypeRegistry registry;
    for (const ItemRow& row : rows) {
        if (row.type.id == sim::kItemNone) {
            LOG_ERROR("items table contains id 0, which means 'no item'");
            return false;
        }
        registry.add(row.type);
    }

    out = std::move(registry);
    return true;
}

bool save_item(Db& db, const ItemRow& row) {
    if (row.type.id == sim::kItemNone) {
        LOG_ERROR("refusing to save an item with id 0");
        return false;
    }

    Stmt stmt = db.prepare(std::string("INSERT OR REPLACE INTO items (") +
                           kItemColumns +
                           ") VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12)");
    if (!stmt.valid()) {
        return false;
    }
    stmt.bind_int(1, row.type.id);
    stmt.bind_text(2, row.name);
    stmt.bind_int(3, row.type.flags.bits());
    stmt.bind_int(4, row.type.weight);
    stmt.bind_int(5, row.type.max_stack);
    stmt.bind_int(6, row.type.equippable ? 1 : 0);
    stmt.bind_int(7, static_cast<std::int64_t>(row.type.slot));
    stmt.bind_int(8, row.type.attack);
    stmt.bind_int(9, row.type.defense);
    stmt.bind_int(10, row.type.attack_kind == sim::AttackKind::Ranged ? 1 : 0);
    stmt.bind_int(11, row.type.attack_range);
    stmt.bind_int(12, row.type.effect);
    return stmt.run();
}

bool retire_item(Db& db, sim::ItemTypeId id) {
    Db::Transaction tx(db);
    if (!tx.begun()) {
        return false;
    }

    std::string name;
    {
        Stmt find = db.prepare("SELECT name FROM items WHERE id = ?1");
        if (!find.valid()) {
            return false;
        }
        find.bind_int(1, id);
        if (!find.step()) {
            LOG_WARN("no item %u to retire", static_cast<unsigned>(id));
            return false;
        }
        name = find.column_text(0);
    }

    // Reserve first, delete second: if this fails halfway the transaction unwinds,
    // but the ordering makes the intent obvious to anyone reading it.
    Stmt reserve = db.prepare(
        "INSERT OR IGNORE INTO retired_item_ids (id, name, retired_at) "
        "VALUES (?1, ?2, strftime('%s','now'))");
    if (!reserve.valid()) {
        return false;
    }
    reserve.bind_int(1, id);
    reserve.bind_text(2, name);
    if (!reserve.run()) {
        return false;
    }

    Stmt remove = db.prepare("DELETE FROM items WHERE id = ?1");
    if (!remove.valid()) {
        return false;
    }
    remove.bind_int(1, id);
    if (!remove.run()) {
        return false;
    }

    return tx.commit();
}

std::optional<sim::ItemTypeId> next_free_item_id(Db& db, ItemCategory category) {
    const Band band = band_for(category);

    // The maximum across BOTH tables, so a retired id is never handed out again.
    // Taking max + 1 rather than filling gaps is the same rule: a gap may exist
    // because something was removed, and reusing it recycles meaning.
    Stmt stmt = db.prepare(
        "SELECT MAX(id) FROM ("
        "  SELECT id FROM items WHERE id BETWEEN ?1 AND ?2"
        "  UNION ALL"
        "  SELECT id FROM retired_item_ids WHERE id BETWEEN ?1 AND ?2)");
    if (!stmt.valid()) {
        return std::nullopt;
    }
    stmt.bind_int(1, band.first);
    stmt.bind_int(2, band.last);
    if (!stmt.step()) {
        return std::nullopt;
    }

    // MAX over an empty set is NULL, which is the "band untouched" case.
    const std::int64_t next =
        stmt.column_is_null(0) ? band.first : stmt.column_int(0) + 1;
    if (next > band.last) {
        LOG_ERROR("id band %u..%u is full", static_cast<unsigned>(band.first),
                  static_cast<unsigned>(band.last));
        return std::nullopt;
    }
    return static_cast<sim::ItemTypeId>(next);
}

bool seed_default_items(Db& db) {
    const std::optional<std::int64_t> count =
        db.query_int("SELECT COUNT(*) FROM items");
    if (!count.has_value()) {
        return false;
    }
    if (*count > 0) {
        return true;  // Already authored; the database is the source of truth now.
    }

    const sim::ItemTypeRegistry defaults = sim::build_default_registry();

    Db::Transaction tx(db);
    if (!tx.begun()) {
        return false;
    }
    for (const sim::ItemTypeId id : defaults.ids()) {
        if (!save_item(db, ItemRow{defaults.get(id), std::string(name_for(id))})) {
            return false;
        }
    }

    // sim::tiles::kActor is reserved and is not an item, but it sits inside the
    // object band. Recording it as retired is what stops next_free_item_id from
    // ever allocating it to a real object.
    Stmt reserve = db.prepare(
        "INSERT OR IGNORE INTO retired_item_ids (id, name, retired_at) "
        "VALUES (?1, 'actor (reserved, not an item)', strftime('%s','now'))");
    if (!reserve.valid()) {
        return false;
    }
    reserve.bind_int(1, sim::tiles::kActor);
    if (!reserve.run()) {
        return false;
    }

    if (!tx.commit()) {
        return false;
    }
    LOG_INFO("seeded content database with %zu built-in item types",
             defaults.count());
    return true;
}

std::optional<Db> open_content_db(const std::string& path) {
    std::optional<Db> db = Db::open(path);
    if (!db.has_value()) {
        return std::nullopt;
    }
    if (!apply_content_migrations(*db)) {
        return std::nullopt;
    }
    if (!seed_default_items(*db)) {
        return std::nullopt;
    }
    return db;
}

}  // namespace store
