#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>

#include "store/db.hpp"

// Every case here opens ":memory:", so the suite still needs no writable
// directory and leaves nothing behind — same discipline as the rest of tests/.

TEST_CASE("store::Db opens, execs and reads back") {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());
    REQUIRE(db->valid());

    REQUIRE(db->exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)"));
    REQUIRE(db->exec("INSERT INTO t (name) VALUES ('sword')"));
    CHECK(db->last_insert_rowid() == 1);

    store::Stmt stmt = db->prepare("SELECT id, name FROM t WHERE name = ?1");
    REQUIRE(stmt.valid());
    stmt.bind_text(1, "sword");
    REQUIRE(stmt.step());
    CHECK(stmt.column_int(0) == 1);
    CHECK(stmt.column_text(1) == "sword");
    CHECK_FALSE(stmt.step());  // exactly one row
    CHECK_FALSE(stmt.failed());
}

TEST_CASE("a prepared statement can be reset and rebound") {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());
    REQUIRE(db->exec("CREATE TABLE items (id INTEGER, attack INTEGER)"));

    store::Stmt insert =
        db->prepare("INSERT INTO items (id, attack) VALUES (?1, ?2)");
    REQUIRE(insert.valid());
    for (int i = 1; i <= 3; ++i) {
        insert.reset();
        insert.bind_int(1, i);
        insert.bind_int(2, i * 10);
        REQUIRE(insert.run());
    }

    CHECK(db->query_int("SELECT COUNT(*) FROM items") == 3);
    CHECK(db->query_int("SELECT attack FROM items WHERE id = 2") == 20);
}

TEST_CASE("a bad statement fails to prepare instead of half-running") {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());

    const store::Stmt stmt = db->prepare("SELECT nope FROM nothing");
    CHECK_FALSE(stmt.valid());
    CHECK(db->last_error() != "no database");
}

TEST_CASE("user_version is how a migration knows where it is") {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());

    // Zero on a fresh database: that is what makes "never migrated" detectable
    // without a table to read first.
    CHECK(db->user_version() == 0);
    REQUIRE(db->set_user_version(7));
    CHECK(db->user_version() == 7);
}

TEST_CASE("a transaction that is not committed rolls back") {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());
    REQUIRE(db->exec("CREATE TABLE t (id INTEGER)"));

    SUBCASE("dropped without commit") {
        {
            store::Db::Transaction tx(*db);
            REQUIRE(tx.begun());
            REQUIRE(db->exec("INSERT INTO t VALUES (1)"));
            // No commit(): leaving the scope must undo the insert, which is the
            // whole reason a half-written save cannot survive an early return.
        }
        CHECK(db->query_int("SELECT COUNT(*) FROM t") == 0);
    }

    SUBCASE("committed") {
        {
            store::Db::Transaction tx(*db);
            REQUIRE(tx.begun());
            REQUIRE(db->exec("INSERT INTO t VALUES (1)"));
            REQUIRE(tx.commit());
        }
        CHECK(db->query_int("SELECT COUNT(*) FROM t") == 1);
    }
}

TEST_CASE("foreign keys are enforced, not merely declared") {
    // SQLite defaults foreign_keys OFF, so a schema that relies on them silently
    // does nothing unless the pragma is set. store::Db sets it for every
    // connection; this is the test that keeps that true.
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());
    REQUIRE(db->exec("CREATE TABLE parent (id INTEGER PRIMARY KEY)"));
    REQUIRE(db->exec(
        "CREATE TABLE child (id INTEGER PRIMARY KEY,"
        " parent_id INTEGER NOT NULL REFERENCES parent(id))"));

    CHECK_FALSE(db->exec("INSERT INTO child (parent_id) VALUES (42)"));
    REQUIRE(db->exec("INSERT INTO parent (id) VALUES (42)"));
    CHECK(db->exec("INSERT INTO child (parent_id) VALUES (42)"));
}

TEST_CASE("open_read_only refuses to invent a database") {
    // Opening a missing file read-only must fail rather than create an empty one:
    // the server reads content this way, and an empty catalogue would boot a world
    // where nothing blocks and nothing can be equipped.
    const std::optional<store::Db> db =
        store::Db::open_read_only("/nonexistent-dir-xyz/content.db");
    CHECK_FALSE(db.has_value());
}

TEST_CASE("moving a Db leaves the source empty and the target usable") {
    std::optional<store::Db> db = store::Db::open(":memory:");
    REQUIRE(db.has_value());
    REQUIRE(db->exec("CREATE TABLE t (id INTEGER)"));

    store::Db moved = std::move(*db);
    CHECK(moved.valid());
    CHECK_FALSE(db->valid());
    CHECK(moved.exec("INSERT INTO t VALUES (1)"));
}
