#include "atlas_meta.hpp"

#include <cstdio>
#include <sstream>
#include <vector>

namespace editor {
namespace {

/// True when `line` is the binding for (kind, id). Matching is done on the parsed
/// fields rather than on a text prefix, so spacing in a hand-edited file does not
/// decide whether the line is found.
bool matches(const std::string& line, std::string_view kind, sim::ItemTypeId id) {
    if (line.empty() || line[0] == '#') {
        return false;
    }
    std::istringstream fields(line);
    std::string line_kind;
    int line_id = 0;
    if (!(fields >> line_kind >> line_id)) {
        return false;
    }
    return line_kind == kind && line_id == static_cast<int>(id);
}

/// Splits keeping no trailing empty piece, and remembers whether the text ended in
/// a newline so the result can end the same way.
std::vector<std::string> split_lines(const std::string& text, bool& trailing_eol) {
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    trailing_eol = current.empty() && !text.empty();
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines, bool trailing_eol) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        if (i + 1 < lines.size() || trailing_eol) {
            out += '\n';
        }
    }
    return out;
}

bool usable(const AtlasBinding& binding) {
    return !binding.kind.empty() && binding.id != sim::kItemNone &&
           binding.w > 0 && binding.h > 0;
}

}  // namespace

void cell_size_for(std::string_view kind, int& w, int& h) {
    if (kind == "ground") {
        w = 64;  // the isometric diamond
        h = 32;
    } else if (kind == "object") {
        w = 64;  // a block: tile diamond plus wall height
        h = 64;
    } else {
        w = 16;  // inventory icon
        h = 16;
    }
}

void apply_canonical_origin(AtlasBinding& binding) {
    if (binding.kind == "item") {
        // Icons are drawn in UI space, not on a tile.
        binding.origin_x = 0.0F;
        binding.origin_y = 0.0F;
        return;
    }
    // Centre horizontally on the tile's top vertex, and hang the sprite upwards by
    // however much taller than the diamond it is. For the canonical sizes this
    // yields exactly the values in docs/sprites.md: (-32, 0) for 64x32 ground and
    // (-32, -32) for a 64x64 block.
    binding.origin_x = -static_cast<float>(binding.w) * 0.5F;
    binding.origin_y = -static_cast<float>(binding.h - 32);
}

std::string format_binding(const AtlasBinding& binding) {
    char line[128];
    if (binding.kind == "item") {
        // Icons carry no origin; writing zeros there would look like a real value.
        std::snprintf(line, sizeof line, "%-11s %-7u %-4d %-4d %-3d %d",
                      binding.kind.c_str(), static_cast<unsigned>(binding.id),
                      binding.x, binding.y, binding.w, binding.h);
    } else {
        std::snprintf(line, sizeof line,
                      "%-11s %-7u %-4d %-4d %-3d %-3d %-8d %d",
                      binding.kind.c_str(), static_cast<unsigned>(binding.id),
                      binding.x, binding.y, binding.w, binding.h,
                      static_cast<int>(binding.origin_x),
                      static_cast<int>(binding.origin_y));
    }
    return line;
}

std::optional<AtlasBinding> find_binding(const std::string& text,
                                         std::string_view kind,
                                         sim::ItemTypeId id) {
    bool trailing = false;
    for (const std::string& line : split_lines(text, trailing)) {
        if (!matches(line, kind, id)) {
            continue;
        }
        std::istringstream fields(line);
        AtlasBinding out;
        int parsed_id = 0;
        if (!(fields >> out.kind >> parsed_id >> out.x >> out.y >> out.w >> out.h)) {
            return std::nullopt;
        }
        out.id = static_cast<sim::ItemTypeId>(parsed_id);
        // Origins are absent on item lines and that is not a parse failure.
        if (!(fields >> out.origin_x >> out.origin_y)) {
            out.origin_x = 0.0F;
            out.origin_y = 0.0F;
        }
        return out;
    }
    return std::nullopt;
}

std::string upsert_binding(const std::string& text, const AtlasBinding& binding) {
    if (!usable(binding)) {
        return text;
    }
    bool trailing = false;
    std::vector<std::string> lines = split_lines(text, trailing);
    const std::string replacement = format_binding(binding);

    for (std::string& line : lines) {
        if (matches(line, binding.kind, binding.id)) {
            line = replacement;
            return join_lines(lines, trailing);
        }
    }

    // Not present: append. Appending rather than inserting near similar lines keeps
    // this from having an opinion about how someone has organised their file.
    lines.push_back(replacement);
    return join_lines(lines, true);
}

void apply_canonical_mob_origin(MobStrip& strip) {
    // Centre on the tile's top vertex horizontally, and hang the sprite up by
    // however much taller than half a tile it is, so its bottom edge lands on the
    // tile's centre. kHalfTileHeight is 16 (client/iso.hpp); spelled out here because
    // atlas_meta is deliberately free of client headers so the tests can compile it
    // without SDL.
    constexpr int kHalfTileHeight = 16;
    strip.origin_x = -static_cast<float>(strip.cell_w) * 0.5F;
    strip.origin_y = static_cast<float>(kHalfTileHeight - strip.cell_h);
}

std::string format_mob_strip(const MobStrip& strip) {
    char line[160];
    std::snprintf(line, sizeof line,
                  "%-11s %-3u %-4d %-4d %-3d %-3d %-2d %-2d %-4d %-4d %d", "mobstrip",
                  static_cast<unsigned>(strip.appearance), strip.x, strip.y,
                  strip.cell_w, strip.cell_h, strip.dirs, strip.frames,
                  static_cast<int>(strip.origin_x), static_cast<int>(strip.origin_y),
                  static_cast<int>(strip.tilt));
    return line;
}

std::optional<MobStrip> find_mob_strip(const std::string& text,
                                       std::uint16_t appearance) {
    bool trailing = false;
    for (const std::string& line : split_lines(text, trailing)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string kind;
        int id = 0;
        if (!(fields >> kind >> id) || kind != "mobstrip" ||
            id != static_cast<int>(appearance)) {
            continue;
        }
        MobStrip strip;
        strip.appearance = appearance;
        if (!(fields >> strip.x >> strip.y >> strip.cell_w >> strip.cell_h >>
              strip.dirs >> strip.frames >> strip.origin_x >> strip.origin_y)) {
            return std::nullopt;
        }
        // Optional tail: an older line without it reads as upright, not as a parse
        // failure.
        if (!(fields >> strip.tilt)) {
            strip.tilt = 0.0F;
        }
        return strip;
    }
    return std::nullopt;
}

std::string upsert_mob_strip(const std::string& text, const MobStrip& strip) {
    if (strip.appearance == 0 || strip.cell_w <= 0 || strip.cell_h <= 0 ||
        strip.dirs <= 0 || strip.frames <= 0) {
        return text;
    }
    bool trailing = false;
    const std::vector<std::string> lines = split_lines(text, trailing);
    const std::string replacement = format_mob_strip(strip);

    std::vector<std::string> kept;
    kept.reserve(lines.size());
    bool replaced = false;
    for (const std::string& line : lines) {
        std::istringstream fields(line);
        std::string kind;
        int id = 0;
        const bool mine = !line.empty() && line[0] != '#' &&
                          static_cast<bool>(fields >> kind >> id) &&
                          id == static_cast<int>(strip.appearance) &&
                          (kind == "mob" || kind == "mobstrip");
        if (mine && kind == "mobstrip") {
            kept.push_back(replacement);
            replaced = true;
        } else if (!mine) {
            kept.push_back(line);
        }
        // `mob` lines for this appearance are dropped; see the header.
    }
    if (!replaced) {
        kept.push_back(replacement);
        return join_lines(kept, true);
    }
    return join_lines(kept, trailing);
}

std::string remove_binding(const std::string& text, std::string_view kind,
                           sim::ItemTypeId id) {
    bool trailing = false;
    const std::vector<std::string> lines = split_lines(text, trailing);
    std::vector<std::string> kept;
    kept.reserve(lines.size());
    for (const std::string& line : lines) {
        if (!matches(line, kind, id)) {
            kept.push_back(line);
        }
    }
    return join_lines(kept, trailing);
}

}  // namespace editor
