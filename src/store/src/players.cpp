#include "store/players.hpp"

#include <utility>

#include "core/log.hpp"
#include "store/schema.hpp"

namespace store {
namespace {

/// Returns the account rowid for `name`, creating the account if it is new.
///
/// One account per login name, one character with the same name in it. That is the
/// whole account model today, and it is a placeholder: the tables allow many
/// characters per account precisely so growing into that is a query change and not a
/// migration.
std::optional<std::int64_t> account_id_for(Db& db, const std::string& name) {
    Stmt insert = db.prepare(
        "INSERT OR IGNORE INTO accounts (name, created_at) "
        "VALUES (?1, strftime('%s','now'))");
    if (!insert.valid()) {
        return std::nullopt;
    }
    insert.bind_text(1, name);
    if (!insert.run()) {
        return std::nullopt;
    }

    // Not last_insert_rowid(): INSERT OR IGNORE leaves it untouched when the row
    // already existed, which on a returning player would hand back whatever id was
    // inserted last — someone else's account.
    Stmt find = db.prepare("SELECT id FROM accounts WHERE name = ?1");
    if (!find.valid()) {
        return std::nullopt;
    }
    find.bind_text(1, name);
    if (!find.step()) {
        return std::nullopt;
    }
    return find.column_int(0);
}

}  // namespace

std::optional<Db> open_player_db(const std::string& path) {
    std::optional<Db> db = Db::open(path);
    if (!db.has_value()) {
        return std::nullopt;
    }
    if (!apply_player_migrations(*db)) {
        return std::nullopt;
    }
    return db;
}

std::optional<CharacterSave> load_character(Db& db, const std::string& name) {
    CharacterSave out;
    {
        Stmt stmt = db.prepare(
            "SELECT id, name, tile_x, tile_y, tile_z, facing, hp, max_hp "
            "FROM characters WHERE name = ?1");
        if (!stmt.valid()) {
            return std::nullopt;
        }
        stmt.bind_text(1, name);
        if (!stmt.step()) {
            return std::nullopt;  // first login, not a failure
        }
        out.id = stmt.column_int(0);
        out.name = stmt.column_text(1);
        // TilePos is int16/int8, and the column is an int64 that a hand-edited row
        // could put anything in, so these are narrowing casts on purpose rather than
        // by accident.
        out.tile.x = static_cast<std::int16_t>(stmt.column_int(2));
        out.tile.y = static_cast<std::int16_t>(stmt.column_int(3));
        out.tile.z = static_cast<std::int8_t>(stmt.column_int(4));
        const std::int64_t facing = stmt.column_int(5);
        // A facing outside the enum would be undefined behaviour downstream, and a
        // hand-edited or corrupted row is not worth trusting.
        out.facing = (facing >= 0 && facing < 8)
                         ? static_cast<sim::Direction>(facing)
                         : sim::Direction::South;
        out.hp = static_cast<std::int32_t>(stmt.column_int(6));
        out.max_hp = static_cast<std::int32_t>(stmt.column_int(7));
    }

    Stmt items = db.prepare(
        "SELECT slot, bag_index, item_id, count FROM character_items "
        "WHERE character_id = ?1 ORDER BY bag_index");
    if (!items.valid()) {
        return std::nullopt;
    }
    items.bind_int(1, out.id);
    while (items.step()) {
        const auto item = static_cast<sim::ItemTypeId>(items.column_int(2));
        const auto count = static_cast<std::uint16_t>(items.column_int(3));
        if (!items.column_is_null(0)) {
            // Worn: exactly one of (slot, bag_index) is set, which the schema's
            // CHECK enforces, so this branch is the equipment case.
            const std::int64_t slot = items.column_int(0);
            if (slot >= 0 &&
                slot < static_cast<std::int64_t>(sim::kEquipSlotCount)) {
                out.equipment[static_cast<std::size_t>(slot)] = item;
            }
        } else {
            out.inventory.push_back(sim::ItemStack{item, count});
        }
    }
    if (items.failed()) {
        return std::nullopt;
    }
    return out;
}

bool save_character(Db& db, const CharacterSave& save) {
    if (save.name.empty()) {
        LOG_ERROR("refusing to save a character with no name");
        return false;
    }

    Db::Transaction tx(db);
    if (!tx.begun()) {
        return false;
    }

    const std::optional<std::int64_t> account = account_id_for(db, save.name);
    if (!account.has_value()) {
        return false;
    }

    {
        // Keyed on the character name, which is UNIQUE, so a returning player
        // updates their row instead of accumulating one per login.
        Stmt stmt = db.prepare(
            "INSERT INTO characters "
            "  (account_id, name, tile_x, tile_y, tile_z, facing, hp, max_hp,"
            "   last_seen) "
            "VALUES (?1,?2,?3,?4,?5,?6,?7,?8, strftime('%s','now')) "
            "ON CONFLICT(name) DO UPDATE SET "
            "  tile_x = excluded.tile_x, tile_y = excluded.tile_y,"
            "  tile_z = excluded.tile_z, facing = excluded.facing,"
            "  hp = excluded.hp, max_hp = excluded.max_hp,"
            "  last_seen = excluded.last_seen");
        if (!stmt.valid()) {
            return false;
        }
        stmt.bind_int(1, *account);
        stmt.bind_text(2, save.name);
        stmt.bind_int(3, save.tile.x);
        stmt.bind_int(4, save.tile.y);
        stmt.bind_int(5, save.tile.z);
        stmt.bind_int(6, static_cast<std::int64_t>(save.facing));
        stmt.bind_int(7, save.hp);
        stmt.bind_int(8, save.max_hp);
        if (!stmt.run()) {
            return false;
        }
    }

    std::int64_t character_id = 0;
    {
        Stmt find = db.prepare("SELECT id FROM characters WHERE name = ?1");
        if (!find.valid()) {
            return false;
        }
        find.bind_text(1, save.name);
        if (!find.step()) {
            return false;
        }
        character_id = find.column_int(0);
    }

    {
        Stmt clear =
            db.prepare("DELETE FROM character_items WHERE character_id = ?1");
        if (!clear.valid()) {
            return false;
        }
        clear.bind_int(1, character_id);
        if (!clear.run()) {
            return false;
        }
    }

    Stmt insert = db.prepare(
        "INSERT INTO character_items (character_id, slot, bag_index, item_id, "
        "count) VALUES (?1,?2,?3,?4,?5)");
    if (!insert.valid()) {
        return false;
    }

    for (std::size_t slot = 0; slot < save.equipment.size(); ++slot) {
        if (save.equipment[slot] == sim::kItemNone) {
            continue;  // an empty slot is the absence of a row, not a row of zeros
        }
        insert.reset();
        insert.bind_int(1, character_id);
        insert.bind_int(2, static_cast<std::int64_t>(slot));
        insert.bind_null(3);
        insert.bind_int(4, save.equipment[slot]);
        insert.bind_int(5, 1);
        if (!insert.run()) {
            return false;
        }
    }

    for (std::size_t i = 0; i < save.inventory.size(); ++i) {
        const sim::ItemStack& stack = save.inventory[i];
        if (stack.id == sim::kItemNone || stack.count == 0U) {
            continue;
        }
        insert.reset();
        insert.bind_int(1, character_id);
        insert.bind_null(2);
        insert.bind_int(3, static_cast<std::int64_t>(i));
        insert.bind_int(4, stack.id);
        insert.bind_int(5, stack.count);
        if (!insert.run()) {
            return false;
        }
    }

    return tx.commit();
}

std::optional<std::int64_t> character_count(Db& db) {
    return db.query_int("SELECT COUNT(*) FROM characters");
}

}  // namespace store
