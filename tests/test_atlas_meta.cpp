#include <doctest/doctest.h>

#include <string>

#include "atlas_meta.hpp"

// atlas.txt is hand-editable and hand-edited. These cases exist because a writer
// that reformats, reorders or drops what it did not understand would quietly
// destroy someone's work, and that is not the sort of bug a screenshot finds.

namespace {

const std::string kSample =
    "# atlas.txt - binds a sprite region to a sim TileId\n"
    "# kind      id/dir  x    y    w   h   origin_x origin_y\n"
    "ground      1       0    0    64  32  -32      0\n"
    "object      100     0    64   64  64  -32      -32\n"
    "item        300     0    176  16  16\n"
    "highlight           0    32   64  32  -32      0\n"
    "actor       0       0    128  32  48  -16      -32\n";

}  // namespace

TEST_CASE("find_binding reads an existing line") {
    const auto ground = editor::find_binding(kSample, "ground", 1);
    REQUIRE(ground.has_value());
    CHECK(ground->x == 0);
    CHECK(ground->y == 0);
    CHECK(ground->w == 64);
    CHECK(ground->h == 32);
    CHECK(ground->origin_x == -32.0F);

    const auto object = editor::find_binding(kSample, "object", 100);
    REQUIRE(object.has_value());
    CHECK(object->y == 64);
    CHECK(object->origin_y == -32.0F);

    // An item line has no origin fields, and that is not a parse failure.
    const auto icon = editor::find_binding(kSample, "item", 300);
    REQUIRE(icon.has_value());
    CHECK(icon->w == 16);
    CHECK(icon->origin_x == 0.0F);
}

TEST_CASE("find_binding does not confuse kinds or ids") {
    CHECK_FALSE(editor::find_binding(kSample, "object", 1).has_value());
    CHECK_FALSE(editor::find_binding(kSample, "ground", 100).has_value());
    CHECK_FALSE(editor::find_binding(kSample, "ground", 999).has_value());
}

TEST_CASE("upsert replaces in place and leaves every other line alone") {
    editor::AtlasBinding binding;
    binding.kind = "object";
    binding.id = 100;
    binding.x = 192;
    binding.y = 64;
    binding.w = 64;
    binding.h = 64;
    editor::apply_canonical_origin(binding);

    const std::string out = editor::upsert_binding(kSample, binding);

    const auto found = editor::find_binding(out, "object", 100);
    REQUIRE(found.has_value());
    CHECK(found->x == 192);

    // Comments, the other kinds and the lines this does not target survive.
    CHECK(out.find("# atlas.txt - binds") != std::string::npos);
    CHECK(out.find("# kind      id/dir") != std::string::npos);
    CHECK(editor::find_binding(out, "ground", 1).has_value());
    CHECK(editor::find_binding(out, "item", 300).has_value());
    CHECK(out.find("highlight") != std::string::npos);
    CHECK(out.find("actor       0") != std::string::npos);

    // Replacing must not grow the file.
    std::size_t before = 0;
    std::size_t after = 0;
    for (const char c : kSample) { before += (c == '\n') ? 1U : 0U; }
    for (const char c : out) { after += (c == '\n') ? 1U : 0U; }
    CHECK(before == after);
}

TEST_CASE("upsert appends a binding the file does not have") {
    editor::AtlasBinding binding;
    binding.kind = "object";
    binding.id = 103;  // the barrel from docs/authoring.md
    binding.x = 192;
    binding.y = 64;
    binding.w = 64;
    binding.h = 64;
    editor::apply_canonical_origin(binding);

    const std::string out = editor::upsert_binding(kSample, binding);
    CHECK(editor::find_binding(out, "object", 103).has_value());
    CHECK(editor::find_binding(out, "object", 100).has_value());  // still there
    CHECK(out.size() > kSample.size());
}

TEST_CASE("upsert refuses a binding that is not usable") {
    editor::AtlasBinding empty;
    CHECK(editor::upsert_binding(kSample, empty) == kSample);

    editor::AtlasBinding no_size;
    no_size.kind = "object";
    no_size.id = 103;
    CHECK(editor::upsert_binding(kSample, no_size) == kSample);

    // Id 0 means "no item" everywhere in the game and can never have a sprite.
    editor::AtlasBinding zero_id;
    zero_id.kind = "object";
    zero_id.id = 0;
    zero_id.w = 64;
    zero_id.h = 64;
    CHECK(editor::upsert_binding(kSample, zero_id) == kSample);
}

TEST_CASE("remove_binding takes out one line and only that line") {
    const std::string out = editor::remove_binding(kSample, "object", 100);
    CHECK_FALSE(editor::find_binding(out, "object", 100).has_value());
    CHECK(editor::find_binding(out, "ground", 1).has_value());
    CHECK(editor::find_binding(out, "item", 300).has_value());
    CHECK(out.find("# atlas.txt") != std::string::npos);

    // Removing something absent is a no-op, not a corruption.
    CHECK(editor::remove_binding(kSample, "object", 999) == kSample);
}

TEST_CASE("canonical origins match the documented values") {
    editor::AtlasBinding ground;
    ground.kind = "ground";
    ground.w = 64;
    ground.h = 32;
    editor::apply_canonical_origin(ground);
    CHECK(ground.origin_x == -32.0F);
    CHECK(ground.origin_y == 0.0F);

    editor::AtlasBinding block;
    block.kind = "object";
    block.w = 64;
    block.h = 64;
    editor::apply_canonical_origin(block);
    CHECK(block.origin_x == -32.0F);
    CHECK(block.origin_y == -32.0F);

    // An icon is drawn in UI space and must not acquire a tile origin.
    editor::AtlasBinding icon;
    icon.kind = "item";
    icon.w = 16;
    icon.h = 16;
    icon.origin_x = 7.0F;
    editor::apply_canonical_origin(icon);
    CHECK(icon.origin_x == 0.0F);
    CHECK(icon.origin_y == 0.0F);
}

TEST_CASE("a written line is read back by the same parser") {
    // The round-trip that matters: what this writes, Tileset::parse_atlas_meta has
    // to be able to read. Same field order, same arity.
    editor::AtlasBinding binding;
    binding.kind = "ground";
    binding.id = 5;
    binding.x = 0;
    binding.y = 176;
    binding.w = 64;
    binding.h = 32;
    editor::apply_canonical_origin(binding);

    const std::string line = editor::format_binding(binding);
    const auto back = editor::find_binding(line + "\n", "ground", 5);
    REQUIRE(back.has_value());
    CHECK(back->x == binding.x);
    CHECK(back->y == binding.y);
    CHECK(back->w == binding.w);
    CHECK(back->h == binding.h);
    CHECK(back->origin_x == binding.origin_x);
    CHECK(back->origin_y == binding.origin_y);
}

TEST_CASE("a file with no trailing newline is still handled") {
    const std::string no_eol = "ground      1       0    0    64  32  -32      0";
    editor::AtlasBinding binding;
    binding.kind = "object";
    binding.id = 103;
    binding.x = 0;
    binding.y = 64;
    binding.w = 64;
    binding.h = 64;
    editor::apply_canonical_origin(binding);

    const std::string out = editor::upsert_binding(no_eol, binding);
    CHECK(editor::find_binding(out, "ground", 1).has_value());
    CHECK(editor::find_binding(out, "object", 103).has_value());
}
