#pragma once

#include <string>

#include "sim/monster_type.hpp"

// Text format for the monster catalogue: `assets/monsters.txt`.
//
// Same shape and the same reasons as map_io.hpp. Parsing is pure — no I/O, no
// SDL, no clock — so the two edges can read the bytes their own way: the client
// through platform::vfs (the file is inside the APK on Android), the server
// through <fstream> (it links no SDL). One parser, so tuning a class can never
// mean two different things on the two sides.
//
// WHY A FILE AND NOT THE CONTENT DATABASE: monster stats are only ever read where
// the simulation runs — the server in network play, the client in solo. A client
// connected to a server never uses its own copy, so unlike item types there is
// nothing here for the content hash to protect, and nothing that a stale file can
// silently disagree about mid-game. That is what makes a plain text asset the
// right weight for this, and it is also why tuning speed does not need a rebake.
//
// Grammar, line-based, '#' starts a comment, blank lines ignored:
//
//   class <id> <name>          # starts a block; name is for humans only
//     <key> <value>            # any of the keys below, in any order
//
// Keys: appearance, hp, attack, defense, kind (melee|ranged), range, effect,
// step_ticks, aggro, leash, loot. A key left out keeps MonsterType's default.
//
// `step_ticks` is the one to reach for when a mob feels wrong: ticks per cardinal
// step at sim::kSimHz. The player walks at kDefaultStepTicks (9), so anything
// above that is slower than the player and anything below outruns them.

namespace sim {

/// Parses `text` into `out`. Returns false and, when `error` is non-null, a
/// human-readable reason on any malformed input (unknown key, class without an
/// id, a step_ticks of zero, ...). `out` is only replaced once the whole file
/// parses, so a typo halfway down cannot leave half a catalogue behind — the same
/// rule as read_content_blob.
bool parse_monster_catalogue(const std::string& text, MonsterRegistry& out,
                             std::string* error = nullptr);

}  // namespace sim
