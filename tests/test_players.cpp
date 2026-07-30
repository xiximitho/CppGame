#include <doctest/doctest.h>

#include <optional>

#include "sim/tile_ids.hpp"
#include "store/db.hpp"
#include "store/players.hpp"
#include "store/schema.hpp"

namespace {

store::Db fresh_player_db() {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());
    REQUIRE(store::apply_player_migrations(*db));
    return std::move(*db);
}

store::CharacterSave sample() {
    store::CharacterSave save;
    save.name = "felipe";
    save.tile = sim::TilePos{37, 21, 0};
    save.facing = sim::Direction::West;
    save.hp = 63;
    save.max_hp = 140;
    save.equipment[static_cast<std::size_t>(sim::EquipSlot::Weapon)] =
        sim::tiles::kSword;
    save.equipment[static_cast<std::size_t>(sim::EquipSlot::Body)] =
        sim::tiles::kArmor;
    save.inventory = {{sim::tiles::kBow, 1}, {sim::tiles::kShield, 2}};
    return save;
}

}  // namespace

TEST_CASE("a character that was never saved does not load") {
    store::Db db = fresh_player_db();
    CHECK_FALSE(store::load_character(db, "nobody").has_value());
    CHECK(store::character_count(db) == 0);
}

TEST_CASE("a character round-trips through the database") {
    store::Db db = fresh_player_db();
    const store::CharacterSave saved = sample();
    REQUIRE(store::save_character(db, saved));

    const std::optional<store::CharacterSave> loaded =
        store::load_character(db, "felipe");
    REQUIRE(loaded.has_value());

    CHECK(loaded->name == saved.name);
    CHECK((loaded->tile == saved.tile));
    CHECK((loaded->facing == saved.facing));
    CHECK(loaded->hp == saved.hp);
    CHECK(loaded->max_hp == saved.max_hp);
    CHECK(loaded->equipment == saved.equipment);
    REQUIRE(loaded->inventory.size() == saved.inventory.size());
    for (std::size_t i = 0; i < saved.inventory.size(); ++i) {
        CHECK(loaded->inventory[i].id == saved.inventory[i].id);
        CHECK(loaded->inventory[i].count == saved.inventory[i].count);
    }
    CHECK(loaded->id != 0);
}

TEST_CASE("saving twice updates instead of duplicating") {
    store::Db db = fresh_player_db();
    REQUIRE(store::save_character(db, sample()));

    store::CharacterSave moved = sample();
    moved.tile = sim::TilePos{5, 6, 1};
    moved.hp = 12;
    moved.inventory.clear();
    REQUIRE(store::save_character(db, moved));

    // One character, one account — a login must not accumulate rows.
    CHECK(store::character_count(db) == 1);
    CHECK(db.query_int("SELECT COUNT(*) FROM accounts") == 1);

    const std::optional<store::CharacterSave> loaded =
        store::load_character(db, "felipe");
    REQUIRE(loaded.has_value());
    CHECK((loaded->tile == moved.tile));
    CHECK(loaded->hp == 12);
    // The old backpack must be gone, not merged: items are replaced wholesale.
    CHECK(loaded->inventory.empty());
    // Equipment survived, because it was still set.
    CHECK(loaded->equipment[static_cast<std::size_t>(sim::EquipSlot::Weapon)] ==
          sim::tiles::kSword);
}

TEST_CASE("unequipping is persisted as an absent row, not a zero") {
    store::Db db = fresh_player_db();
    REQUIRE(store::save_character(db, sample()));

    store::CharacterSave stripped = sample();
    stripped.equipment = {};  // everything off
    REQUIRE(store::save_character(db, stripped));

    const std::optional<store::CharacterSave> loaded =
        store::load_character(db, "felipe");
    REQUIRE(loaded.has_value());
    for (const sim::ItemTypeId item : loaded->equipment) {
        CHECK(item == sim::kItemNone);
    }
    // The rows for the worn items are gone; only the backpack ones remain.
    CHECK(db.query_int("SELECT COUNT(*) FROM character_items WHERE slot IS NOT NULL")
          == 0);
}

TEST_CASE("two characters do not see each other's items") {
    store::Db db = fresh_player_db();

    store::CharacterSave a = sample();
    store::CharacterSave b = sample();
    b.name = "outro";
    b.inventory = {{sim::tiles::kHelmet, 1}};
    b.equipment = {};
    REQUIRE(store::save_character(db, a));
    REQUIRE(store::save_character(db, b));

    CHECK(store::character_count(db) == 2);

    const auto loaded_a = store::load_character(db, "felipe");
    const auto loaded_b = store::load_character(db, "outro");
    REQUIRE(loaded_a.has_value());
    REQUIRE(loaded_b.has_value());
    CHECK(loaded_a->inventory.size() == 2U);
    REQUIRE(loaded_b->inventory.size() == 1U);
    CHECK(loaded_b->inventory[0].id == sim::tiles::kHelmet);
    CHECK(loaded_b->equipment[0] == sim::kItemNone);
}

TEST_CASE("a nameless character is refused") {
    store::Db db = fresh_player_db();
    store::CharacterSave save = sample();
    save.name.clear();
    CHECK_FALSE(store::save_character(db, save));
    CHECK(store::character_count(db) == 0);
}

TEST_CASE("a corrupt facing loads as a valid direction") {
    store::Db db = fresh_player_db();
    REQUIRE(store::save_character(db, sample()));
    // A hand-edited or corrupted row must not turn into an out-of-range enum, which
    // would be undefined behaviour the moment anything switched on it.
    REQUIRE(db.exec("UPDATE characters SET facing = 99 WHERE name = 'felipe'"));

    const std::optional<store::CharacterSave> loaded =
        store::load_character(db, "felipe");
    REQUIRE(loaded.has_value());
    CHECK((loaded->facing == sim::Direction::South));
}

TEST_CASE("deleting a character takes its items with it") {
    store::Db db = fresh_player_db();
    REQUIRE(store::save_character(db, sample()));
    REQUIRE(db.query_int("SELECT COUNT(*) FROM character_items") > 0);

    // ON DELETE CASCADE only works because store::Db turns foreign keys on for
    // every connection; SQLite defaults them off.
    REQUIRE(db.exec("DELETE FROM characters WHERE name = 'felipe'"));
    CHECK(db.query_int("SELECT COUNT(*) FROM character_items") == 0);
}

TEST_CASE("open_player_db migrates a fresh file") {
    std::optional<store::Db> db = store::open_player_db(":memory:");
    REQUIRE(db.has_value());
    CHECK(db->user_version() == store::kPlayerSchemaVersion);
    CHECK(store::character_count(*db) == 0);
}
