#include "sim/monster_io.hpp"

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

/// Clamps into a field's range instead of wrapping. A hp of 70000 is a typo, and
/// silently becoming 4464 is worse than being pinned at the maximum.
std::int64_t clamped(std::int64_t value, std::int64_t low, std::int64_t high) {
    return value < low ? low : (value > high ? high : value);
}

}  // namespace

bool parse_monster_catalogue(const std::string& text, MonsterRegistry& out,
                             std::string* error) {
    const auto fail = [&](const std::string& why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };

    MonsterRegistry parsed;
    std::optional<MonsterType> current;

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
        if (!line.empty() && line.back() == '\r') {  // CRLF tolerance
            line.pop_back();
        }
        if (is_comment_or_blank(line)) {
            continue;
        }

        const std::string where = " (line " + std::to_string(line_number) + ")";
        std::istringstream fields(line);
        std::string key;
        fields >> key;

        if (key == "class") {
            int id = 0;
            std::string name;
            if (!(fields >> id) || id <= 0) {
                return fail("bad 'class' line, needs a positive id" + where);
            }
            fields >> name;  // optional; the client shows it, the sim ignores it
            flush();
            MonsterType type;
            type.id = static_cast<MonsterTypeId>(id);
            type.name = name;
            current = type;
            continue;
        }

        if (!current.has_value()) {
            return fail("'" + key + "' before any 'class'" + where);
        }

        // `kind` is the only non-numeric value, so it is handled before the read.
        if (key == "kind") {
            std::string value;
            if (!(fields >> value)) {
                return fail("'kind' needs melee or ranged" + where);
            }
            if (value == "melee") {
                current->attack_kind = AttackKind::Melee;
            } else if (value == "ranged") {
                current->attack_kind = AttackKind::Ranged;
            } else {
                return fail("'kind' must be melee or ranged, got '" + value + "'" +
                            where);
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
        } else if (key == "hp") {
            if (value <= 0) {
                return fail("'hp' must be positive" + where);
            }
            current->max_hp = static_cast<std::int32_t>(clamped(value, 1, 1000000));
        } else if (key == "attack") {
            current->attack = static_cast<std::int16_t>(clamped(value, 0, 32767));
        } else if (key == "defense") {
            current->defense = static_cast<std::int16_t>(clamped(value, 0, 32767));
        } else if (key == "range") {
            current->attack_range =
                static_cast<std::uint8_t>(clamped(value, 1, 255));
        } else if (key == "effect") {
            current->effect = static_cast<std::uint8_t>(clamped(value, 0, 255));
        } else if (key == "step_ticks") {
            // Zero would be a step that never ends (or ends instantly, depending on
            // which way the comparison rounds) — refused rather than clamped,
            // because a 0 in the file means the author meant something else.
            if (value <= 0) {
                return fail("'step_ticks' must be at least 1" + where);
            }
            current->step_ticks = static_cast<Tick>(clamped(value, 1, 1000));
        } else if (key == "aggro") {
            current->aggro_radius =
                static_cast<std::uint8_t>(clamped(value, 0, 255));
        } else if (key == "leash") {
            current->leash = static_cast<std::uint8_t>(clamped(value, 0, 255));
        } else if (key == "loot") {
            // Forms:
            //   loot <item_id>                         → max 1, chance 100%
            //   loot <item_id> <max_count>             → chance 100%
            //   loot <item_id> <max_count> <chance>    → chance / kLootChanceScale
            // loot 0 is "nothing" (no row).
            const auto item =
                static_cast<ItemTypeId>(clamped(value, 0, 65535));
            if (item == kItemNone) {
                continue;
            }
            std::int64_t max_count = 1;
            std::int64_t chance = static_cast<std::int64_t>(kLootChanceScale);
            std::int64_t second = 0;
            if (fields >> second) {
                max_count = second;
                std::int64_t third = 0;
                if (fields >> third) {
                    chance = third;
                }
            }
            if (max_count < 1) {
                return fail("'loot' max_count must be at least 1" + where);
            }
            if (chance < 0) {
                return fail("'loot' chance must be >= 0" + where);
            }
            current->loot_table.push_back(LootEntry{
                item,
                static_cast<std::uint16_t>(clamped(max_count, 1, 65535)),
                static_cast<std::uint32_t>(
                    clamped(chance, 0, static_cast<std::int64_t>(kLootChanceScale))),
            });
        } else {
            // Unknown keys are an error, not ignored: a silently dropped `speed`
            // where the field is called `step_ticks` is a mob that keeps its old
            // numbers while the author swears they changed them.
            return fail("unknown key '" + key + "'" + where);
        }
    }

    flush();
    if (parsed.count() == 0U) {
        return fail("no classes defined");
    }

    out = std::move(parsed);
    return true;
}

}  // namespace sim
