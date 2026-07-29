#include "sim/map_io.hpp"

#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sim {
namespace {

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream stream(text);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {  // CRLF tolerance
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

bool is_comment_or_blank(const std::string& line) {
    for (const char ch : line) {
        if (ch == ' ' || ch == '\t') {
            continue;
        }
        return ch == '#';
    }
    return true;  // all whitespace
}

}  // namespace

std::optional<ParsedMap> parse_text_map(const std::string& text,
                                        const ItemTypeRegistry& items,
                                        std::string* error) {
    const auto fail = [&](const std::string& why) -> std::optional<ParsedMap> {
        if (error != nullptr) {
            *error = why;
        }
        return std::nullopt;
    };

    const std::vector<std::string> lines = split_lines(text);

    ParsedMap out;
    bool sized = false;
    int width = 0;
    int height = 0;
    int floors = 0;

    // char -> (ground id, object id). object id 0 means "no object".
    std::unordered_map<char, std::pair<TileId, TileId>> legend;
    char spawn_char = '\0';

    std::size_t i = 0;
    while (i < lines.size()) {
        const std::string& line = lines[i];
        if (is_comment_or_blank(line)) {
            ++i;
            continue;
        }

        std::istringstream fields(line);
        std::string keyword;
        fields >> keyword;

        if (keyword == "size") {
            if (!(fields >> width >> height >> floors) || width <= 0 ||
                height <= 0 || floors <= 0) {
                return fail("bad 'size' line");
            }
            out.map = TileMap(width, height, floors);
            sized = true;
            ++i;
        } else if (keyword == "legend") {
            std::string glyph;
            int ground = 0;
            if (!(fields >> glyph >> ground) || glyph.size() != 1) {
                return fail("bad 'legend' line: " + line);
            }
            int object = 0;
            fields >> object;  // optional; leaves 0 if absent
            legend[glyph[0]] = {static_cast<TileId>(ground),
                                static_cast<TileId>(object)};
            ++i;
        } else if (keyword == "spawn") {
            std::string glyph;
            if (!(fields >> glyph) || glyph.size() != 1) {
                return fail("bad 'spawn' line");
            }
            spawn_char = glyph[0];
            ++i;
        } else if (keyword == "floor") {
            int z = 0;
            if (!(fields >> z)) {
                return fail("bad 'floor' line");
            }
            if (!sized) {
                return fail("'floor' before 'size'");
            }
            if (z < 0 || z >= floors) {
                return fail("'floor' index out of range");
            }
            ++i;  // grid rows start on the next line and are read verbatim

            for (int y = 0; y < height; ++y) {
                if (i >= lines.size()) {
                    return fail("map grid ends early");
                }
                const std::string& row = lines[i];
                ++i;
                for (int x = 0; x < width; ++x) {
                    const auto col = static_cast<std::size_t>(x);
                    const char ch = col < row.size() ? row[col] : ' ';
                    if (ch == ' ') {
                        continue;  // void: unwalkable hole
                    }
                    const auto found = legend.find(ch);
                    if (found == legend.end()) {
                        return fail(std::string("unknown map char '") + ch + "'");
                    }
                    const TileId ground = found->second.first;
                    const TileId object = found->second.second;
                    const TilePos pos{static_cast<std::int16_t>(x),
                                      static_cast<std::int16_t>(y),
                                      static_cast<std::int8_t>(z)};
                    out.map.set_ground(pos, ground);
                    // Same derivation as the generator: either slot can block.
                    const bool blocking = items.get(ground).blocks_walk() ||
                                          items.get(object).blocks_walk();
                    out.map.set_object(pos, object, blocking);
                    if (ch == spawn_char) {
                        out.spawn = pos;
                    }
                }
            }
        } else {
            return fail("unknown directive: " + keyword);
        }
    }

    if (!sized) {
        return fail("map has no 'size' directive");
    }
    return out;
}

std::string write_text_map(const TileMap& map,
                           const std::optional<TilePos>& spawn) {
    // Printable glyphs assigned to distinct (ground, object) pairs as they are
    // first seen. Space and '@' are reserved (void and spawn).
    const std::string pool = ".#~oTn,-=+:*%wsxde";
    const auto key = [](TileId g, TileId o) {
        return (static_cast<std::uint32_t>(g) << 16U) |
               static_cast<std::uint32_t>(o);
    };

    const bool has_spawn = spawn.has_value() && map.in_bounds(*spawn);
    std::uint32_t spawn_key = 0;
    if (has_spawn) {
        const Tile& t = map.at(*spawn);
        spawn_key = key(t.ground, t.object);
    }

    std::unordered_map<std::uint32_t, char> glyph;
    std::vector<std::uint32_t> order;
    std::size_t pool_i = 0;

    const auto at = [&](int x, int y, int z) -> const Tile& {
        return map.at(TilePos{static_cast<std::int16_t>(x),
                              static_cast<std::int16_t>(y),
                              static_cast<std::int8_t>(z)});
    };

    for (int z = 0; z < map.floors(); ++z) {
        for (int y = 0; y < map.height(); ++y) {
            for (int x = 0; x < map.width(); ++x) {
                const Tile& t = at(x, y, z);
                if (t.ground == kTileEmpty && t.object == kTileEmpty) {
                    continue;  // void
                }
                const std::uint32_t k = key(t.ground, t.object);
                if (glyph.find(k) == glyph.end()) {
                    glyph[k] = pool_i < pool.size() ? pool[pool_i] : '?';
                    ++pool_i;
                    order.push_back(k);
                }
            }
        }
    }

    std::ostringstream out;
    out << "# saved by game_editor\n";
    out << "size " << map.width() << ' ' << map.height() << ' ' << map.floors()
        << '\n';
    const auto legend_line = [&](char ch, std::uint32_t k) {
        const auto ground = static_cast<int>(k >> 16U);
        const auto object = static_cast<int>(k & 0xFFFFU);
        out << "legend " << ch << ' ' << ground;
        if (object != 0) {
            out << ' ' << object;
        }
        out << '\n';
    };
    for (const std::uint32_t k : order) {
        legend_line(glyph[k], k);
    }
    if (has_spawn) {
        legend_line('@', spawn_key);
        out << "spawn @\n";
    }

    for (int z = 0; z < map.floors(); ++z) {
        out << "floor " << z << '\n';
        for (int y = 0; y < map.height(); ++y) {
            std::string row;
            for (int x = 0; x < map.width(); ++x) {
                const Tile& t = at(x, y, z);
                if (has_spawn && x == spawn->x && y == spawn->y &&
                    z == spawn->z) {
                    row += '@';
                } else if (t.ground == kTileEmpty && t.object == kTileEmpty) {
                    row += ' ';
                } else {
                    row += glyph[key(t.ground, t.object)];
                }
            }
            while (!row.empty() && row.back() == ' ') {  // trim trailing void
                row.pop_back();
            }
            out << row << '\n';
        }
    }

    return out.str();
}

}  // namespace sim
