#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sim/components.hpp"
#include "sim/item_type.hpp"
#include "sim/types.hpp"
#include "store/db.hpp"

// Player persistence: what survives a server restart.
//
// Server-side only, in a database of its own (players.db, gitignored) rather than
// alongside authored content — see store/schema.hpp for why that split exists.
//
// Deliberately NOT here: authentication. HelloMsg carries a name and no
// credential, so today the name IS the identity, which is fine for a scaffold on a
// trusted network and is not fine for anything else. Adding it means a password
// column, a hash with a real KDF, and a protocol change; it is a decision of its
// own rather than something to smuggle in with the save format.

namespace store {

/// One character, in the shape the server needs to put it back into the world.
///
/// Position is a tile, matching sim::CPosition — there is no continuous position in
/// this game to lose (CLAUDE.md: tile movement is the netcode contract).
struct CharacterSave {
    std::int64_t   id = 0;  ///< rowid; 0 for a character not yet stored
    std::string    name;
    sim::TilePos   tile;
    sim::Direction facing = sim::Direction::South;
    std::int32_t   hp = 100;
    std::int32_t   max_hp = 100;
    std::array<sim::ItemTypeId, sim::kEquipSlotCount> equipment{};
    std::vector<sim::ItemStack> inventory;
};

/// Opens (creating if absent) the player database and migrates it.
std::optional<Db> open_player_db(const std::string& path);

/// Loads a character by name. nullopt means "no such character", which is a first
/// login and not an error — the caller creates one.
std::optional<CharacterSave> load_character(Db& db, const std::string& name);

/// Inserts or updates a character, creating its account on first save.
///
/// Runs in one transaction: a crash halfway must not leave a character whose
/// position was written but whose inventory was not. The item rows are replaced
/// wholesale rather than diffed, because a backpack is small and a diff is a bug
/// surface for no measurable gain.
bool save_character(Db& db, const CharacterSave& save);

/// Number of stored characters. For logging and for tests.
std::optional<std::int64_t> character_count(Db& db);

}  // namespace store
