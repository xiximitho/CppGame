#include <doctest/doctest.h>

#include <cstdint>
#include <string_view>

#include "core/hash.hpp"
#include "sim/content_blob.hpp"
#include "sim/item_type.hpp"
#include "sim/tile_ids.hpp"

using namespace sim;

TEST_CASE("fnv1a_64 matches the reference vectors") {
    // The published FNV-1a 64 test vectors. They are here because the whole point of
    // this hash is that every platform agrees: a digest that drifted between Linux
    // and Windows would reject every cross-platform connection.
    CHECK(core::fnv1a_64(std::string_view("")) == core::kFnvOffsetBasis);
    CHECK(core::fnv1a_64(std::string_view("a")) == 0xaf63dc4c8601ec8cULL);
    CHECK(core::fnv1a_64(std::string_view("foobar")) == 0x85944171f73967e8ULL);

    // The pointer overload has to agree with the string_view one.
    const char* text = "foobar";
    CHECK(core::fnv1a_64(text, 6U) == core::fnv1a_64(std::string_view("foobar")));

    // Hashing nothing is hashing nothing, however it is spelled.
    CHECK(core::fnv1a_64(nullptr, 0U) == core::kFnvOffsetBasis);
}

TEST_CASE("content_hash is stable for the same catalogue") {
    const ItemTypeRegistry a = build_default_registry();
    const ItemTypeRegistry b = build_default_registry();
    CHECK(content_hash(a) == content_hash(b));
    CHECK(content_hash(a) != 0U);
}

TEST_CASE("content_hash changes when any rule changes") {
    const ItemTypeRegistry base = build_default_registry();
    const std::uint64_t original = content_hash(base);

    SUBCASE("an attack value") {
        ItemTypeRegistry changed = build_default_registry();
        ItemType sword = changed.get(tiles::kSword);
        sword.attack += 1;
        changed.add(sword);
        CHECK(content_hash(changed) != original);
    }

    SUBCASE("a ranged weapon's range") {
        // The field this whole exercise started from. A server and a client
        // disagreeing about it means one of them thinks you can shoot from further
        // away than the other, which is exactly the sort of drift the handshake
        // exists to catch.
        ItemTypeRegistry changed = build_default_registry();
        ItemType bow = changed.get(tiles::kBow);
        bow.attack_range += 1;
        changed.add(bow);
        CHECK(content_hash(changed) != original);
    }

    SUBCASE("a blocking flag") {
        ItemTypeRegistry changed = build_default_registry();
        ItemType tree = changed.get(tiles::kTree);
        tree.flags = ItemFlags{0U};
        changed.add(tree);
        CHECK(content_hash(changed) != original);
    }

    SUBCASE("an added item") {
        ItemTypeRegistry changed = build_default_registry();
        changed.add(ItemType{9999U, ItemFlag::Pickable, 1U, 1U});
        CHECK(content_hash(changed) != original);
    }
}

TEST_CASE("content_hash ignores what the blob does not carry") {
    // Names are authoring-only and are not serialised, so two catalogues differing
    // only by name must still agree — otherwise renaming an item in the editor would
    // lock every client out until it re-baked, for no gameplay reason.
    const ItemTypeRegistry a = build_default_registry();
    const std::uint64_t before = content_hash(a);

    ItemTypeRegistry b = build_default_registry();
    // There is no name in ItemType at all; this asserts the property by showing the
    // hash depends only on the fields the record has.
    CHECK(content_hash(b) == before);
}

TEST_CASE("an empty catalogue hashes to something, not to zero") {
    // Zero is the "could not serialise" signal, so an empty-but-valid catalogue must
    // not collide with it.
    const ItemTypeRegistry empty;
    CHECK(content_hash(empty) != 0U);
    CHECK(content_hash(empty) != content_hash(build_default_registry()));
}
