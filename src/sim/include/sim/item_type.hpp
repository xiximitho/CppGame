#pragma once

#include <cstdint>
#include <vector>

// The item/type catalogue: the gameplay half of what a TileId means.
//
// A TileId stored in a tile (ground or object) is an *item type id*. The
// simulation needs a little more about it than "blocks or not" as content grows:
// whether it can be walked on, whether it stops line of sight, whether it is
// pickable, stackable, a container, its weight. That is what lives here.
//
// HARD RULE, same as the rest of sim/: this is pure data. No I/O, no SDL, no
// clock, no <random>. The registry is filled once at boot from baked content
// (see docs/content.md) and handed to the World as a const reference; the
// simulation only ever reads it. The PRESENTATION half of an item (its sprite,
// animation frames, UI name) is a client concern and lives in client/, keyed by
// the same id — the server never sees it, which is what keeps server-only free
// of any tileset definition.

namespace sim {

/// An item type id. Same width and value space as TileId on purpose: the number
/// in a tile's ground/object slot indexes straight into the registry.
using ItemTypeId = std::uint16_t;

/// The empty/unknown type. A tile slot holding this is "nothing here".
constexpr ItemTypeId kItemNone = 0;

/// Gameplay properties, as bit flags. Only things the SERVER must reason about
/// belong here; anything purely visual does not.
enum class ItemFlag : std::uint32_t {
    None        = 0U,
    /// Cannot be stepped onto. Replaces the standalone Tile::blocking bool; the
    /// tile still caches the result so the movement hot loop never touches the
    /// registry (see docs/content.md, migration step 2).
    BlocksWalk  = 1U << 0U,
    /// Stops line of sight. No system reads this yet; reserved for LOS.
    BlocksSight = 1U << 1U,
    /// May act as a tile's ground layer (something to stand on).
    Ground      = 1U << 2U,
    /// Can be picked up into an inventory.
    Pickable    = 1U << 3U,
    /// Identical stacks merge; see ItemType::max_stack.
    Stackable   = 1U << 4U,
    /// Holds other items (backpack, chest).
    Container   = 1U << 5U,
};

/// A small, strongly-typed bitset over ItemFlag. Kept trivial and constexpr so
/// the default content table can be built at compile time and the baked loader
/// can fill it with a single u32.
class ItemFlags {
public:
    constexpr ItemFlags() = default;
    // Intentionally implicit: a single ItemFlag is a valid ItemFlags value, which
    // lets `ItemType{.flags = ItemFlag::Ground}` read naturally.
    constexpr ItemFlags(ItemFlag flag)  // NOLINT(google-explicit-constructor)
        : bits_(static_cast<std::uint32_t>(flag)) {}
    explicit constexpr ItemFlags(std::uint32_t bits) : bits_(bits) {}

    constexpr bool has(ItemFlag flag) const {
        return (bits_ & static_cast<std::uint32_t>(flag)) != 0U;
    }
    constexpr std::uint32_t bits() const { return bits_; }

    friend constexpr bool operator==(ItemFlags a, ItemFlags b) {
        return a.bits_ == b.bits_;
    }
    friend constexpr bool operator!=(ItemFlags a, ItemFlags b) {
        return !(a == b);
    }

private:
    std::uint32_t bits_ = 0U;
};

constexpr ItemFlags operator|(ItemFlag a, ItemFlag b) {
    return ItemFlags{static_cast<std::uint32_t>(a) |
                     static_cast<std::uint32_t>(b)};
}
constexpr ItemFlags operator|(ItemFlags a, ItemFlag b) {
    return ItemFlags{a.bits() | static_cast<std::uint32_t>(b)};
}

/// One entry in the catalogue. Deliberately POD-like and free of any pointer to
/// presentation, so it serialises to the content blob as a fixed record.
struct ItemType {
    ItemTypeId    id        = kItemNone;
    ItemFlags     flags{};
    std::uint16_t weight    = 0U;   ///< in centi-oz; 0 for scenery
    std::uint8_t  max_stack = 1U;   ///< meaningful only with ItemFlag::Stackable

    constexpr bool blocks_walk()  const { return flags.has(ItemFlag::BlocksWalk); }
    constexpr bool blocks_sight() const { return flags.has(ItemFlag::BlocksSight); }
    constexpr bool is_ground()    const { return flags.has(ItemFlag::Ground); }
};

/// The catalogue itself: id -> ItemType, dense so lookup is a bounds check and an
/// index. Filled once at boot and then read-only for the lifetime of the World.
class ItemTypeRegistry {
public:
    /// Registers a type. `type.id` must not be kItemNone. Re-adding an id
    /// overwrites it, which is what lets a content patch override a base type.
    void add(const ItemType& type);

    /// Unknown ids (never registered, or kItemNone) return a safe empty type:
    /// id == kItemNone and no flags set. Callers can branch on that or just read
    /// the flags, which default to permissive-but-harmless.
    const ItemType& get(ItemTypeId id) const;

    bool contains(ItemTypeId id) const;

    /// Number of registered types (excludes gaps and kItemNone).
    std::size_t count() const { return count_; }

private:
    std::vector<ItemType> by_id_;  ///< index == id; gaps hold a kItemNone entry
    ItemType              none_{};  ///< returned for unknown ids
    std::size_t           count_ = 0U;
};

/// The placeholder catalogue: the six tile types the procedural map generator and
/// the client atlas use today (grass/dirt/stone/water/wall/tree), re-expressed as
/// item types. This exists so the registry can be wired in without a content blob
/// yet and without changing any observable behaviour — see docs/content.md,
/// migration step 3. It is the last hardcoded copy; the baked loader replaces it.
ItemTypeRegistry build_default_registry();

}  // namespace sim
