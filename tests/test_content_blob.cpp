#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "sim/content_blob.hpp"
#include "sim/item_type.hpp"
#include "sim/tile_ids.hpp"

using namespace sim;

namespace {

/// Every field set to something distinct from its default, so a field dropped or
/// misordered in the record shows up as a value mismatch rather than passing by
/// accident on a zero.
ItemType loaded_weapon() {
    ItemType type;
    type.id = 1723;
    type.flags = ItemFlag::Pickable | ItemFlag::Stackable;
    type.weight = 4321U;
    type.max_stack = 17U;
    type.equippable = true;
    type.slot = EquipSlot::Amulet;  // not the default (Weapon)
    type.attack = 12345;
    type.defense = -321;  // negative: write_ranged carries the sign
    type.attack_kind = AttackKind::Ranged;
    type.attack_range = 9U;
    type.effect = 2U;
    return type;
}

}  // namespace

TEST_CASE("content blob round-trips every ItemType field") {
    ItemTypeRegistry original;
    original.add(loaded_weapon());

    const std::vector<std::uint8_t> bytes = write_content_blob(original);
    REQUIRE(!bytes.empty());

    ItemTypeRegistry parsed;
    REQUIRE(read_content_blob(bytes.data(), bytes.size(), parsed));

    const ItemType& in = original.get(1723);
    const ItemType& out = parsed.get(1723);
    CHECK(out.id == in.id);
    CHECK(out.flags == in.flags);
    CHECK(out.weight == in.weight);
    CHECK(out.max_stack == in.max_stack);
    CHECK(out.equippable == in.equippable);
    CHECK((out.slot == in.slot));
    CHECK(out.attack == in.attack);
    CHECK(out.defense == in.defense);
    CHECK((out.attack_kind == in.attack_kind));
    CHECK(out.attack_range == in.attack_range);
    CHECK(out.effect == in.effect);
}

TEST_CASE("content blob round-trips the whole default catalogue") {
    const ItemTypeRegistry original = build_default_registry();
    const std::vector<std::uint8_t> bytes = write_content_blob(original);
    REQUIRE(!bytes.empty());

    ItemTypeRegistry parsed;
    REQUIRE(read_content_blob(bytes.data(), bytes.size(), parsed));
    CHECK(parsed.count() == original.count());
    CHECK(parsed.ids() == original.ids());

    // Spot-check the two that matter to the rules: water blocks despite being
    // ground, and the bow's range survives (it is what the editor will edit).
    CHECK(parsed.get(tiles::kWater).blocks_walk());
    CHECK(parsed.get(tiles::kWater).is_ground());
    CHECK(parsed.get(tiles::kBow).attack_range ==
          original.get(tiles::kBow).attack_range);
    CHECK((parsed.get(tiles::kBow).attack_kind == AttackKind::Ranged));
}

TEST_CASE("content blob rejects a bad header instead of misparsing") {
    const ItemTypeRegistry original = build_default_registry();
    std::vector<std::uint8_t> bytes = write_content_blob(original);
    REQUIRE(bytes.size() > 8U);

    SUBCASE("wrong magic") {
        std::vector<std::uint8_t> bad = bytes;
        bad[0] = 'X';
        ItemTypeRegistry parsed;
        CHECK_FALSE(read_content_blob(bad.data(), bad.size(), parsed));
    }

    SUBCASE("unknown version") {
        std::vector<std::uint8_t> bad = bytes;
        // Version is the 16 bits right after the 4-byte magic.
        bad[4] = 0xFFU;
        bad[5] = 0xFFU;
        ItemTypeRegistry parsed;
        CHECK_FALSE(read_content_blob(bad.data(), bad.size(), parsed));
    }

    SUBCASE("empty buffer") {
        ItemTypeRegistry parsed;
        CHECK_FALSE(read_content_blob(bytes.data(), 0U, parsed));
    }
}

TEST_CASE("a truncated content blob leaves the registry untouched") {
    const std::vector<std::uint8_t> bytes =
        write_content_blob(build_default_registry());
    REQUIRE(bytes.size() > 16U);

    // Keep the header (so it gets past magic and version) but cut the records.
    ItemTypeRegistry parsed;
    parsed.add(ItemType{4242U, ItemFlag::Ground, 0U, 1U});

    CHECK_FALSE(read_content_blob(bytes.data(), 12U, parsed));
    // Not merely "failed": the pre-existing content is still there, because a
    // rejected blob must never half-replace a catalogue.
    CHECK(parsed.count() == 1U);
    CHECK(parsed.contains(4242U));
}

TEST_CASE("an empty registry still produces a readable blob") {
    const ItemTypeRegistry empty;
    const std::vector<std::uint8_t> bytes = write_content_blob(empty);
    REQUIRE(!bytes.empty());

    ItemTypeRegistry parsed;
    REQUIRE(read_content_blob(bytes.data(), bytes.size(), parsed));
    CHECK(parsed.count() == 0U);
}
