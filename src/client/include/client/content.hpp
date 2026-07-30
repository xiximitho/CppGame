#pragma once

#include "sim/item_type.hpp"

// How the client gets its item catalogue.
//
// The client reads the BAKED BLOB, never the authoring database: on Android and iOS
// its assets live inside the application package and are not files, while SQLite
// needs a real path to open and seek (see docs/content.md and check-layering.sh,
// which enforces it). tools/bake — or saving in the editor's item mode — produces
// that blob.
//
// Both sessions need this, not just the solo one: the remote session has no use for
// item rules itself, but it has to send the catalogue's fingerprint in the handshake
// so the server can refuse a client whose content has drifted from its own.

namespace client {

/// Loads the catalogue from `content.bin` through platform::vfs.
///
/// Falls back to the compiled-in table when the blob is absent, which keeps a fresh
/// clone runnable before anyone has baked — the same reasoning as the procedural-art
/// fallback in Tileset::load. A blob that exists but does not parse is a different
/// matter and logs loudly: that is corruption or a version mismatch, not an absence.
sim::ItemTypeRegistry load_item_catalogue();

}  // namespace client
