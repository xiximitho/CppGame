#pragma once

#include <cstdint>

#include "store/db.hpp"

// The database schemas and their migrations.
//
// TWO DATABASES, not one, and the reason is what may be committed:
//
//   content.db  authored item types. This IS content — the thing an editor writes
//               and a team shares, so it belongs in version control.
//   players.db  accounts, characters, what they carry. Server-local state that
//               must never be committed anywhere.
//
// Putting both in one file would make "commit the content" and "do not commit
// players' saves" the same operation. Nothing references across the boundary
// (character_items stores an item id as a plain number, exactly as a tile does),
// so there is nothing to lose by splitting.
//
// Each file carries its own version in PRAGMA user_version, which is zero on a
// fresh file — so "has this ever been migrated?" is answerable without reading a
// table that might not exist. docs/roadmap.md is explicit that the save format has
// to be versioned from the first save written to a real player's disk; this is that
// mechanism, in place before the first save exists rather than retrofitted after.

namespace store {

/// Bump when adding a step to the corresponding migration function.
constexpr std::int64_t kContentSchemaVersion = 1;
constexpr std::int64_t kPlayerSchemaVersion = 1;

/// Brings a content database up to kContentSchemaVersion.
///
/// Runs in a transaction, so a failed migration leaves the file at its previous
/// version rather than half-upgraded. Returns false if the file is NEWER than this
/// build understands: an old binary writing a new schema is how a database gets
/// silently mangled, so it refuses instead of guessing.
bool apply_content_migrations(Db& db);

/// Same, for a player database.
bool apply_player_migrations(Db& db);

}  // namespace store
