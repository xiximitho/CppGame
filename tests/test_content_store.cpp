#include <doctest/doctest.h>

#include <optional>
#include <vector>

#include "sim/item_type.hpp"
#include "sim/tile_ids.hpp"
#include "store/content.hpp"
#include "store/db.hpp"
#include "store/schema.hpp"

namespace {

/// A migrated, unseeded in-memory content database.
store::Db fresh_db() {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());
    REQUIRE(store::apply_content_migrations(*db));
    return std::move(*db);
}

store::ItemRow sample_axe(sim::ItemTypeId id) {
    store::ItemRow row;
    row.name = "great axe";
    row.type.id = id;
    row.type.flags = sim::ItemFlag::Pickable;
    row.type.weight = 2100U;
    row.type.max_stack = 1U;
    row.type.equippable = true;
    row.type.slot = sim::EquipSlot::Weapon;
    row.type.attack = 27;
    row.type.defense = 3;
    row.type.attack_kind = sim::AttackKind::Melee;
    row.type.attack_range = 1U;
    row.type.effect = 1U;
    return row;
}

}  // namespace

TEST_CASE("migrations are idempotent and record their version") {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());

    REQUIRE(store::apply_content_migrations(*db));
    CHECK(db->user_version() == store::kContentSchemaVersion);
    // Running again must be a no-op rather than a failed CREATE TABLE.
    REQUIRE(store::apply_content_migrations(*db));
    CHECK(db->user_version() == store::kContentSchemaVersion);
}

TEST_CASE("a database from the future is refused, not guessed at") {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());
    REQUIRE(db->set_user_version(store::kContentSchemaVersion + 1));

    // An old binary writing to a newer schema is how a save gets mangled, so this
    // must fail loudly instead of proceeding.
    CHECK_FALSE(store::apply_content_migrations(*db));
}

TEST_CASE("seeding fills the catalogue once and is safe to repeat") {
    store::Db db = fresh_db();

    REQUIRE(store::seed_default_items(db));
    const std::optional<std::int64_t> first =
        db.query_int("SELECT COUNT(*) FROM items");
    REQUIRE(first.has_value());
    CHECK(*first == static_cast<std::int64_t>(
                        sim::build_default_registry().count()));

    REQUIRE(store::seed_default_items(db));
    CHECK(db.query_int("SELECT COUNT(*) FROM items") == *first);
}

TEST_CASE("the seeded catalogue matches build_default_registry exactly") {
    store::Db db = fresh_db();
    REQUIRE(store::seed_default_items(db));

    sim::ItemTypeRegistry loaded;
    REQUIRE(store::load_item_types(db, loaded));

    const sim::ItemTypeRegistry expected = sim::build_default_registry();
    REQUIRE(loaded.ids() == expected.ids());
    for (const sim::ItemTypeId id : expected.ids()) {
        const sim::ItemType& a = expected.get(id);
        const sim::ItemType& b = loaded.get(id);
        CHECK(b.flags == a.flags);
        CHECK(b.weight == a.weight);
        CHECK(b.max_stack == a.max_stack);
        CHECK(b.equippable == a.equippable);
        CHECK((b.slot == a.slot));
        CHECK(b.attack == a.attack);
        CHECK(b.defense == a.defense);
        CHECK((b.attack_kind == a.attack_kind));
        CHECK(b.attack_range == a.attack_range);
        CHECK(b.effect == a.effect);
    }
}

TEST_CASE("an item round-trips through the database with every field") {
    store::Db db = fresh_db();
    const store::ItemRow axe = sample_axe(1723U);
    REQUIRE(store::save_item(db, axe));

    std::vector<store::ItemRow> rows;
    REQUIRE(store::load_item_rows(db, rows));
    REQUIRE(rows.size() == 1U);

    CHECK(rows[0].name == "great axe");
    CHECK(rows[0].type.id == axe.type.id);
    CHECK(rows[0].type.flags == axe.type.flags);
    CHECK(rows[0].type.weight == axe.type.weight);
    CHECK(rows[0].type.equippable == axe.type.equippable);
    CHECK((rows[0].type.slot == axe.type.slot));
    CHECK(rows[0].type.attack == axe.type.attack);
    CHECK(rows[0].type.defense == axe.type.defense);
    CHECK((rows[0].type.attack_kind == axe.type.attack_kind));
    CHECK(rows[0].type.attack_range == axe.type.attack_range);
    CHECK(rows[0].type.effect == axe.type.effect);
}

TEST_CASE("saving the same id again edits it instead of duplicating") {
    store::Db db = fresh_db();
    REQUIRE(store::save_item(db, sample_axe(1723U)));

    store::ItemRow edited = sample_axe(1723U);
    edited.name = "rusty axe";
    edited.type.attack = 4;
    REQUIRE(store::save_item(db, edited));

    std::vector<store::ItemRow> rows;
    REQUIRE(store::load_item_rows(db, rows));
    REQUIRE(rows.size() == 1U);
    CHECK(rows[0].name == "rusty axe");
    CHECK(rows[0].type.attack == 4);
}

TEST_CASE("a ranged weapon keeps its tile range through the database") {
    store::Db db = fresh_db();
    store::ItemRow bow = sample_axe(1800U);
    bow.name = "long bow";
    bow.type.attack_kind = sim::AttackKind::Ranged;
    bow.type.attack_range = 7U;
    REQUIRE(store::save_item(db, bow));

    sim::ItemTypeRegistry loaded;
    REQUIRE(store::load_item_types(db, loaded));
    CHECK((loaded.get(1800U).attack_kind == sim::AttackKind::Ranged));
    CHECK(loaded.get(1800U).attack_range == 7U);
}

TEST_CASE("ids are allocated per band and never recycled") {
    store::Db db = fresh_db();
    REQUIRE(store::seed_default_items(db));

    // Ground 1..4 are seeded, so the next one is 5; objects reach 102, but 200 is
    // reserved for the actor id, which must push allocation past it.
    CHECK(store::next_free_item_id(db, store::ItemCategory::Ground) == 5U);
    CHECK(store::next_free_item_id(db, store::ItemCategory::Object) ==
          sim::tiles::kActor + 1U);
    CHECK(store::next_free_item_id(db, store::ItemCategory::Equipment) ==
          sim::tiles::kWoodenStaff + 1U);
}

TEST_CASE("retiring an item reserves its id forever") {
    store::Db db = fresh_db();
    REQUIRE(store::save_item(db, sample_axe(1723U)));
    CHECK(store::next_free_item_id(db, store::ItemCategory::Equipment) == 1724U);

    REQUIRE(store::retire_item(db, 1723U));

    // Gone from the catalogue...
    std::vector<store::ItemRow> rows;
    REQUIRE(store::load_item_rows(db, rows));
    CHECK(rows.empty());

    // ...but the id is still spent. Handing 1723 out again would silently turn any
    // saved map or older client that references it into something else.
    CHECK(store::next_free_item_id(db, store::ItemCategory::Equipment) == 1724U);
    CHECK(db.query_int("SELECT COUNT(*) FROM retired_item_ids WHERE id = 1723") ==
          1);
}

TEST_CASE("retiring an item that does not exist fails") {
    store::Db db = fresh_db();
    CHECK_FALSE(store::retire_item(db, 999U));
}

TEST_CASE("open_content_db creates, migrates and seeds in one call") {
    // ":memory:" gets a fresh database, which is exactly the "file was absent"
    // path the server hits on a clean clone.
    std::optional<store::Db> db = store::open_content_db(":memory:");
    REQUIRE(db.has_value());
    CHECK(db->user_version() == store::kContentSchemaVersion);

    sim::ItemTypeRegistry registry;
    REQUIRE(store::load_item_types(*db, registry));
    CHECK(registry.count() == sim::build_default_registry().count());
    // The rule that matters most downstream: water is ground AND blocks.
    CHECK(registry.get(sim::tiles::kWater).is_ground());
    CHECK(registry.get(sim::tiles::kWater).blocks_walk());
}

TEST_CASE("a catalogue holding item id 0 is rejected") {
    store::Db db = fresh_db();
    // The CHECK constraint stops it at the database level...
    CHECK_FALSE(db.exec("INSERT INTO items (id, name) VALUES (0, 'nothing')"));

    // ...and save_item refuses before even asking, because id 0 means "no item"
    // in every tile slot in the game.
    store::ItemRow bad = sample_axe(0U);
    CHECK_FALSE(store::save_item(db, bad));
}
