#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim/item_type.hpp"

// The runtime content format: an ItemTypeRegistry as a flat byte blob.
//
// This is the RUNTIME half of the pipeline in docs/content.md. Authoring happens
// in a SQLite database that only tools/ ever links; tools/bake turns it into
// these bytes, and the game reads them back. Neither the client nor the server
// links SQLite, which is what keeps the server-only preset free of any dependency
// it does not need and keeps the read path inside platform::vfs (the only thing
// that works inside an Android APK).
//
// Both directions live here, in sim/, on purpose: the writer is what the bake
// tool calls and the reader is what the game calls, so a field can never be
// written in one layout and read in another. Parsing is pure — bytes in,
// registry out, no I/O — exactly like sim/map_io.hpp. The caller does the file
// reading.
//
// Only the GAMEPLAY half of an item is in here. Sprite, icon and display name are
// presentation, live on the client keyed by the same id, and the server never
// sees them (docs/content.md, "Três conhecimentos sobre o mesmo id").

namespace sim {

/// Sibling of net::kProtocolVersion, and enforced the same way: the reader
/// rejects a blob it does not understand instead of misparsing it silently. Bump
/// this whenever the record layout below changes.
constexpr std::uint16_t kContentVersion = 1;

/// Serialises every registered type. Returns an empty vector if serialisation
/// overflowed, which a caller must treat as failure rather than as "no content".
std::vector<std::uint8_t> write_content_blob(const ItemTypeRegistry& registry);

/// Parses a blob produced by write_content_blob. Returns false — leaving `out`
/// untouched — on a bad magic, an unknown version, a malformed record or a
/// truncated buffer. `out` is only assigned once the whole blob has been read and
/// validated, so a rejected blob can never leave a half-filled registry behind.
bool read_content_blob(const std::uint8_t* data, std::size_t size,
                       ItemTypeRegistry& out);

}  // namespace sim
