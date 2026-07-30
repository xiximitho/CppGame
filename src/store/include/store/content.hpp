#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sim/item_type.hpp"
#include "store/db.hpp"

// Reading and writing the authored item catalogue.
//
// This is the layer where rows become sim::ItemType. Three consumers:
//   - the server, which loads the catalogue at boot;
//   - tools/bake, which loads it and writes the client's blob;
//   - the editor's item mode, which lists, edits and adds rows.
// The client is not one of them and must never be — see docs/content.md.

namespace store {

/// An authored item: the simulation's view of it plus the name, which exists only
/// for humans and tools. The name is not in the baked blob, because neither the
/// simulation nor the server has any use for it.
struct ItemRow {
    sim::ItemType type;
    std::string   name;
};

/// Which band a freshly allocated id comes from. The bands are a convention for
/// humans (docs/authoring.md documents them) — nothing in the simulation reads
/// meaning out of an id's magnitude, the flags decide behaviour.
enum class ItemCategory { Ground, Object, Equipment };

/// Fills `out` with every catalogued type. `out` is only replaced once the whole
/// table has been read, so a query that fails midway cannot leave a partial
/// catalogue — the same rule as read_content_blob.
bool load_item_types(Db& db, sim::ItemTypeRegistry& out);

/// Same, keeping the names. For the editor and the tools.
bool load_item_rows(Db& db, std::vector<ItemRow>& out);

/// Inserts or replaces one item by id. Replacing is how an edit is saved.
bool save_item(Db& db, const ItemRow& row);

/// Deletes an item and reserves its id forever.
///
/// The reservation is the point: a saved map or an older client refers to items by
/// number, so handing 1723 out again would quietly turn an old map into something
/// else. Deleting without reserving is the bug this function exists to prevent.
bool retire_item(Db& db, sim::ItemTypeId id);

/// Lowest id never used in `category` — greater than every id present AND every id
/// retired in that band, so nothing is ever recycled. nullopt if the band is full
/// or the query failed.
std::optional<sim::ItemTypeId> next_free_item_id(Db& db, ItemCategory category);

/// Writes the built-in catalogue into an empty `items` table and returns true.
/// Does nothing (and still returns true) when the table already has rows, so it is
/// safe to call on every boot.
///
/// This is the migration path off sim::build_default_registry(): the hardcoded
/// table becomes the seed of the database, once, and the database is the source of
/// truth from then on.
bool seed_default_items(Db& db);

/// Opens (creating if absent) the content database at `path`, migrates it and
/// seeds it when empty. This is what the server and the tools call.
///
/// Creating-and-seeding rather than failing is deliberate: a fresh clone must be
/// able to build and run, the same reason Tileset falls back to procedural art when
/// the atlas PNG is missing. It logs loudly when it does so.
std::optional<Db> open_content_db(const std::string& path);

}  // namespace store
