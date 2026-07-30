#include "store/schema.hpp"

#include <optional>
#include <string_view>

#include "core/log.hpp"

namespace store {
namespace {

/// Content schema v1.
///
/// `items` mirrors sim::ItemType field for field, plus `name`, which is authoring
/// and UI only and deliberately NOT in the baked blob: neither the simulation nor
/// the server has any use for it (docs/content.md, "Três conhecimentos sobre o
/// mesmo id").
///
/// `retired_item_ids` exists because ids are a contract. A saved map and an older
/// client reference items by number, so a retired id must never be handed out
/// again — reusing 1723 would silently turn an old map into something else.
constexpr char kContentV1[] = R"sql(
CREATE TABLE items (
    id            INTEGER PRIMARY KEY,
    name          TEXT    NOT NULL,
    flags         INTEGER NOT NULL DEFAULT 0,
    weight        INTEGER NOT NULL DEFAULT 0,
    max_stack     INTEGER NOT NULL DEFAULT 1,
    equippable    INTEGER NOT NULL DEFAULT 0,
    slot          INTEGER NOT NULL DEFAULT 0,
    attack        INTEGER NOT NULL DEFAULT 0,
    defense       INTEGER NOT NULL DEFAULT 0,
    attack_kind   INTEGER NOT NULL DEFAULT 0,
    attack_range  INTEGER NOT NULL DEFAULT 1,
    effect        INTEGER NOT NULL DEFAULT 0,
    CHECK (id > 0),
    CHECK (max_stack >= 1),
    CHECK (attack_range >= 1)
);

CREATE TABLE retired_item_ids (
    id         INTEGER PRIMARY KEY,
    name       TEXT    NOT NULL,
    retired_at INTEGER NOT NULL
);
)sql";

/// Player schema v1.
///
/// Positions are tile coordinates, matching sim::CPosition — there is no continuous
/// position in this game to lose (see CLAUDE.md on tile movement being the netcode
/// contract).
///
/// `item_id` is a plain number with no foreign key, on purpose: it points into the
/// content database, which is a different file. That is the same relationship a
/// tile's ground/object slot already has with the catalogue.
///
/// character_items covers worn gear and backpack in one table: exactly one of
/// (slot, bag_index) is set, which the CHECK enforces. Two tables would duplicate
/// every query for no gain.
constexpr char kPlayerV1[] = R"sql(
CREATE TABLE accounts (
    id         INTEGER PRIMARY KEY,
    name       TEXT    NOT NULL UNIQUE,
    created_at INTEGER NOT NULL
);

CREATE TABLE characters (
    id         INTEGER PRIMARY KEY,
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name       TEXT    NOT NULL UNIQUE,
    tile_x     INTEGER NOT NULL,
    tile_y     INTEGER NOT NULL,
    tile_z     INTEGER NOT NULL DEFAULT 0,
    facing     INTEGER NOT NULL DEFAULT 0,
    hp         INTEGER NOT NULL,
    max_hp     INTEGER NOT NULL,
    last_seen  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE character_items (
    character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    slot         INTEGER,
    bag_index    INTEGER,
    item_id      INTEGER NOT NULL,
    count        INTEGER NOT NULL DEFAULT 1,
    CHECK (count >= 1),
    CHECK ((slot IS NULL) <> (bag_index IS NULL))
);

CREATE INDEX idx_character_items_owner ON character_items(character_id);
CREATE INDEX idx_characters_account ON characters(account_id);
)sql";

/// Shared migration driver. `steps` is indexed by the version it upgrades TO minus
/// one, so steps[0] takes a fresh file to v1.
bool migrate(Db& db, std::string_view label, std::int64_t target,
             const char* const* steps) {
    const std::optional<std::int64_t> current = db.user_version();
    if (!current.has_value()) {
        LOG_ERROR("cannot read %.*s schema version: %s",
                  static_cast<int>(label.size()), label.data(),
                  db.last_error().c_str());
        return false;
    }

    if (*current > target) {
        LOG_ERROR("%.*s database is schema v%lld but this build understands only "
                  "v%lld; refusing to touch it",
                  static_cast<int>(label.size()), label.data(),
                  static_cast<long long>(*current),
                  static_cast<long long>(target));
        return false;
    }
    if (*current == target) {
        return true;
    }

    Db::Transaction tx(db);
    if (!tx.begun()) {
        LOG_ERROR("cannot begin %.*s migration: %s",
                  static_cast<int>(label.size()), label.data(),
                  db.last_error().c_str());
        return false;
    }

    for (std::int64_t version = *current; version < target; ++version) {
        if (!db.exec(steps[version])) {
            return false;  // Transaction rolls back on destruction.
        }
    }

    if (!db.set_user_version(target) || !tx.commit()) {
        LOG_ERROR("cannot finish %.*s migration: %s",
                  static_cast<int>(label.size()), label.data(),
                  db.last_error().c_str());
        return false;
    }

    LOG_INFO("%.*s database migrated from schema v%lld to v%lld",
             static_cast<int>(label.size()), label.data(),
             static_cast<long long>(*current), static_cast<long long>(target));
    return true;
}

}  // namespace

bool apply_content_migrations(Db& db) {
    static const char* const kSteps[] = {kContentV1};
    return migrate(db, "content", kContentSchemaVersion, kSteps);
}

bool apply_player_migrations(Db& db) {
    static const char* const kSteps[] = {kPlayerV1};
    return migrate(db, "player", kPlayerSchemaVersion, kSteps);
}

}  // namespace store
