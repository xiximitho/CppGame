#include "sim/vocation_io.hpp"

#include <optional>
#include <sstream>
#include <string>

namespace sim {
namespace {

bool is_comment_or_blank(const std::string& line) {
    for (const char ch : line) {
        if (ch == ' ' || ch == '\t') {
            continue;
        }
        return ch == '#';
    }
    return true;
}

std::int64_t clamped(std::int64_t value, std::int64_t low, std::int64_t high) {
    return value < low ? low : (value > high ? high : value);
}

}  // namespace

bool parse_vocation_catalogue(const std::string& text, VocationRegistry& out,
                              std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };

    VocationRegistry parsed;
    std::optional<VocationType> current;

    const auto flush = [&] {
        if (current.has_value()) {
            parsed.add(*current);
            current.reset();
        }
    };

    std::istringstream stream(text);
    std::string line;
    int line_number = 0;

    while (std::getline(stream, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (is_comment_or_blank(line)) {
            continue;
        }

        const std::string where = " (line " + std::to_string(line_number) + ")";
        std::istringstream fields(line);
        std::string key;
        fields >> key;

        if (key == "vocation") {
            int id = 0;
            std::string name;
            if (!(fields >> id) || id <= 0) {
                return fail("bad 'vocation' line, needs a positive id" + where);
            }
            // Rest of the line is the display name (may contain spaces).
            std::string rest;
            std::getline(fields, rest);
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
                rest.erase(rest.begin());
            }
            flush();
            VocationType type;
            type.id = static_cast<VocationId>(id);
            type.name = rest;
            current = type;
            continue;
        }

        if (!current.has_value()) {
            return fail("'" + key + "' before any 'vocation'" + where);
        }

        if (key == "kind") {
            std::string value;
            if (!(fields >> value)) {
                return fail("'kind' needs melee or ranged" + where);
            }
            if (value == "melee") {
                current->preferred_kind = AttackKind::Melee;
            } else if (value == "ranged") {
                current->preferred_kind = AttackKind::Ranged;
            } else {
                return fail("'kind' must be melee or ranged, got '" + value + "'" +
                            where);
            }
            continue;
        }

        if (key == "code") {
            std::string value;
            if (!(fields >> value)) {
                return fail("'code' needs a short token" + where);
            }
            current->code = value;
            continue;
        }

        if (key == "starter") {
            // One or more item ids on the same line.
            std::int64_t item = 0;
            bool any = false;
            while (fields >> item) {
                any = true;
                if (item < 0 || item > 65535) {
                    return fail("'starter' item id out of range" + where);
                }
                current->starter_items.push_back(
                    static_cast<ItemTypeId>(item));
            }
            if (!any) {
                return fail("'starter' needs at least one item id" + where);
            }
            continue;
        }

        std::int64_t value = 0;
        if (!(fields >> value)) {
            return fail("'" + key + "' needs a number" + where);
        }

        if (key == "appearance") {
            current->appearance =
                static_cast<std::uint16_t>(clamped(value, 0, 65535));
        } else if (key == "base_hp") {
            if (value <= 0) {
                return fail("'base_hp' must be positive" + where);
            }
            current->base_hp =
                static_cast<std::int32_t>(clamped(value, 1, 1000000));
        } else if (key == "hp_per_level") {
            current->hp_per_level =
                static_cast<std::int32_t>(clamped(value, 0, 10000));
        } else if (key == "base_mana") {
            current->base_mana =
                static_cast<std::int32_t>(clamped(value, 0, 1000000));
        } else if (key == "mana_per_level") {
            current->mana_per_level =
                static_cast<std::int32_t>(clamped(value, 0, 10000));
        } else if (key == "capacity") {
            current->capacity =
                static_cast<std::uint16_t>(clamped(value, 0, 65535));
        } else {
            return fail("unknown key '" + key + "'" + where);
        }
    }

    flush();
    if (parsed.count() == 0U) {
        return fail("no vocations defined");
    }

    out = std::move(parsed);
    return true;
}

}  // namespace sim
