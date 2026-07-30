#include "item_mode.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

#include "client/ui.hpp"
#include "core/log.hpp"
#include "sim/content_blob.hpp"

namespace editor {
namespace {

// Layout, in window pixels. The list is narrow because an id and a name is all it
// needs to show; the form is wide because a label plus a value is not.
constexpr float kMargin = 16.0F;
constexpr float kListW = 250.0F;
constexpr float kFormX = kMargin + kListW + kMargin;
constexpr float kFormW = 430.0F;
constexpr float kRowH = 20.0F;
constexpr float kTitleH = 30.0F;
constexpr float kTextScale = 2.0F;
constexpr float kHintScale = 1.0F;
constexpr float kValueX = 210.0F;  ///< value column, relative to the form's left

const client::Color kPanel{16, 18, 24, 240};
const client::Color kRowSelected{201, 162, 39, 255};
const client::Color kBright{236, 240, 248, 255};
const client::Color kDim{120, 126, 140, 255};
const client::Color kLabel{164, 172, 188, 255};
const client::Color kFocusRow{44, 48, 58, 255};

/// Ordered exactly like sim::EquipSlot.
constexpr const char* kSlotNames[] = {"weapon", "shield", "helmet", "body",
                                      "legs",   "boots",  "ring",   "amulet"};

/// Clamps into a range without pulling in <algorithm> semantics on mixed types.
std::int64_t clamp_i64(std::int64_t value, std::int64_t low, std::int64_t high) {
    return value < low ? low : (value > high ? high : value);
}

/// Toggles one flag bit in place, since ItemFlags is deliberately immutable-ish.
sim::ItemFlags toggled(sim::ItemFlags flags, sim::ItemFlag flag) {
    const std::uint32_t bit = static_cast<std::uint32_t>(flag);
    return sim::ItemFlags{flags.bits() ^ bit};
}

}  // namespace

ItemMode::ItemMode(store::Db& db, const client::Tileset& tileset,
                   SDL_Window* window, std::string blob_path)
    : db_(&db),
      tileset_(&tileset),
      window_(window),
      blob_path_(std::move(blob_path)) {
    reload();
}

bool ItemMode::reload() {
    std::vector<store::ItemRow> rows;
    if (!store::load_item_rows(*db_, rows)) {
        message_ = "cannot read items";
        return false;
    }
    rows_ = std::move(rows);
    if (selected_ >= rows_.size()) {
        selected_ = rows_.empty() ? 0 : rows_.size() - 1;
    }
    if (!rows_.empty()) {
        draft_ = rows_[selected_];
    } else {
        draft_ = store::ItemRow{};
    }
    dirty_ = false;
    return true;
}

void ItemMode::select(std::size_t index) {
    if (rows_.empty()) {
        return;
    }
    selected_ = std::min(index, rows_.size() - 1);
    draft_ = rows_[selected_];
    dirty_ = false;
    editing_name_ = false;
    message_.clear();
}

bool ItemMode::applicable(Field field) const {
    switch (field) {
        case Field::Slot:
            return draft_.type.equippable;
        case Field::Attack:
        case Field::AttackKind:
            // Attack belongs to a weapon; armour uses defense.
            return draft_.type.is_weapon();
        case Field::AttackRange:
            // The field the whole exercise started from: a melee weapon's range is
            // always one tile, so showing it as editable would be a lie.
            return draft_.type.is_weapon() &&
                   draft_.type.attack_kind == sim::AttackKind::Ranged;
        default:
            return true;
    }
}

void ItemMode::move_focus(int delta) {
    int index = static_cast<int>(focus_);
    for (int step = 0; step < kFieldCount; ++step) {
        index += delta;
        if (index < 0) {
            index = kFieldCount - 1;
        } else if (index >= kFieldCount) {
            index = 0;
        }
        if (applicable(static_cast<Field>(index))) {
            focus_ = static_cast<Field>(index);
            return;
        }
    }
}

void ItemMode::adjust(int delta, bool coarse) {
    const std::int64_t step = coarse ? delta * 10 : delta;
    sim::ItemType& t = draft_.type;

    switch (focus_) {
        case Field::Name:
            return;  // text, not a number
        case Field::FlagBlocksWalk:
            t.flags = toggled(t.flags, sim::ItemFlag::BlocksWalk);
            break;
        case Field::FlagBlocksSight:
            t.flags = toggled(t.flags, sim::ItemFlag::BlocksSight);
            break;
        case Field::FlagGround:
            t.flags = toggled(t.flags, sim::ItemFlag::Ground);
            break;
        case Field::FlagPickable:
            t.flags = toggled(t.flags, sim::ItemFlag::Pickable);
            break;
        case Field::FlagStackable:
            t.flags = toggled(t.flags, sim::ItemFlag::Stackable);
            break;
        case Field::FlagContainer:
            t.flags = toggled(t.flags, sim::ItemFlag::Container);
            break;
        case Field::Weight:
            t.weight = static_cast<std::uint16_t>(
                clamp_i64(static_cast<std::int64_t>(t.weight) + step, 0, 65535));
            break;
        case Field::MaxStack:
            // Floor of one: the database CHECK enforces it too, and a zero here
            // would be rejected at save time instead of at edit time.
            t.max_stack = static_cast<std::uint8_t>(
                clamp_i64(static_cast<std::int64_t>(t.max_stack) + step, 1, 255));
            break;
        case Field::Equippable:
            t.equippable = !t.equippable;
            break;
        case Field::Slot: {
            const int count = static_cast<int>(sim::kEquipSlotCount);
            int slot = static_cast<int>(t.slot) + delta;
            slot = ((slot % count) + count) % count;
            t.slot = static_cast<sim::EquipSlot>(slot);
            break;
        }
        case Field::Attack:
            t.attack = static_cast<std::int16_t>(
                clamp_i64(static_cast<std::int64_t>(t.attack) + step, -32768,
                          32767));
            break;
        case Field::Defense:
            t.defense = static_cast<std::int16_t>(
                clamp_i64(static_cast<std::int64_t>(t.defense) + step, -32768,
                          32767));
            break;
        case Field::AttackKind:
            t.attack_kind = t.attack_kind == sim::AttackKind::Melee
                                ? sim::AttackKind::Ranged
                                : sim::AttackKind::Melee;
            // Switching to ranged with a melee range of 1 leaves a weapon that is
            // ranged in name only, so give it a usable default the first time.
            if (t.attack_kind == sim::AttackKind::Ranged && t.attack_range <= 1U) {
                t.attack_range = 4U;
            }
            break;
        case Field::AttackRange:
            t.attack_range = static_cast<std::uint8_t>(clamp_i64(
                static_cast<std::int64_t>(t.attack_range) + step, 1, 255));
            break;
        case Field::Effect:
            t.effect = static_cast<std::uint8_t>(
                clamp_i64(static_cast<std::int64_t>(t.effect) + step, 0, 255));
            break;
        case Field::Count:
            return;
    }
    dirty_ = true;

    // Toggling equippable or the flags can make the focused field meaningless;
    // move off it rather than leaving focus on a dimmed row.
    if (!applicable(focus_)) {
        move_focus(1);
    }
}

void ItemMode::type_digit(int digit) {
    sim::ItemType& t = draft_.type;
    const auto append = [digit](std::int64_t current, std::int64_t limit) {
        const std::int64_t grown = current * 10 + digit;
        return grown > limit ? static_cast<std::int64_t>(digit) : grown;
    };

    switch (focus_) {
        case Field::Weight:
            t.weight = static_cast<std::uint16_t>(append(t.weight, 65535));
            break;
        case Field::MaxStack:
            t.max_stack = static_cast<std::uint8_t>(
                std::max<std::int64_t>(1, append(t.max_stack, 255)));
            break;
        case Field::Attack:
            t.attack = static_cast<std::int16_t>(append(t.attack, 32767));
            break;
        case Field::Defense:
            t.defense = static_cast<std::int16_t>(append(t.defense, 32767));
            break;
        case Field::AttackRange:
            t.attack_range = static_cast<std::uint8_t>(
                std::max<std::int64_t>(1, append(t.attack_range, 255)));
            break;
        case Field::Effect:
            t.effect = static_cast<std::uint8_t>(append(t.effect, 255));
            break;
        default:
            return;
    }
    dirty_ = true;
}

bool ItemMode::create_new() {
    // The new id comes from the band of whatever is selected, so picking a sword and
    // pressing N gives an equipment id while picking grass gives a ground one. The
    // band is a human convention (docs/authoring.md); nothing reads meaning from an
    // id's magnitude.
    store::ItemCategory category = store::ItemCategory::Equipment;
    if (!rows_.empty()) {
        const sim::ItemTypeId current = rows_[selected_].type.id;
        if (current < 100U) {
            category = store::ItemCategory::Ground;
        } else if (current < 300U) {
            category = store::ItemCategory::Object;
        }
    }

    const std::optional<sim::ItemTypeId> id =
        store::next_free_item_id(*db_, category);
    if (!id.has_value()) {
        message_ = "no free id in that band";
        return false;
    }

    store::ItemRow row;
    row.type.id = *id;
    row.name = "new item";
    if (category == store::ItemCategory::Ground) {
        row.type.flags = sim::ItemFlag::Ground;
    } else if (category == store::ItemCategory::Equipment) {
        row.type.flags = sim::ItemFlag::Pickable;
        row.type.equippable = true;
    }

    if (!store::save_item(*db_, row) || !reload()) {
        message_ = "could not create the item";
        return false;
    }
    // Select what was just created rather than whatever ended up at the old index.
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].type.id == *id) {
            select(i);
            break;
        }
    }
    bake();
    message_ = "created id " + std::to_string(*id);
    return true;
}

bool ItemMode::retire_selected() {
    if (rows_.empty()) {
        return false;
    }
    const sim::ItemTypeId id = rows_[selected_].type.id;
    if (!store::retire_item(*db_, id)) {
        message_ = "could not retire that item";
        return false;
    }
    if (!reload()) {
        return false;
    }
    bake();
    // "Retired", not "deleted", and the wording is deliberate: the id stays spent
    // forever so an old map that references it can never mean something else.
    message_ = "retired id " + std::to_string(id) + " (id stays reserved)";
    return true;
}

bool ItemMode::save() {
    if (rows_.empty()) {
        return false;
    }
    if (draft_.name.empty()) {
        message_ = "a name is required";
        return false;
    }
    if (!store::save_item(*db_, draft_)) {
        message_ = "save failed";
        return false;
    }
    if (!reload()) {
        return false;
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].type.id == draft_.type.id) {
            selected_ = i;
            draft_ = rows_[i];
            break;
        }
    }
    const bool baked = bake();
    message_ = baked ? "saved and baked" : "saved (bake failed)";
    return true;
}

bool ItemMode::bake() {
    // Done in-process rather than by shelling out to game_bake: the person authoring
    // items should not need a terminal, and this target already links everything
    // required. Same code path as the tool, so the bytes are the same bytes.
    sim::ItemTypeRegistry registry;
    if (!store::load_item_types(*db_, registry)) {
        return false;
    }
    const std::vector<std::uint8_t> blob = sim::write_content_blob(registry);
    if (blob.empty()) {
        return false;
    }
    std::ofstream out(blob_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        LOG_WARN("cannot write '%s'", blob_path_.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(blob.data()),
              static_cast<std::streamsize>(blob.size()));
    if (!out.good()) {
        return false;
    }
    LOG_INFO("baked %zu item types into '%s'", registry.count(),
             blob_path_.c_str());
    return true;
}

void ItemMode::on_exit() {
    if (editing_name_) {
        editing_name_ = false;
        SDL_StopTextInput(window_);
    }
}

bool ItemMode::handle_event(const SDL_Event& event) {
    // While the name is being typed, text input owns the keyboard: otherwise typing
    // "n" in a name would create a new item.
    if (editing_name_) {
        switch (event.type) {
            case SDL_EVENT_TEXT_INPUT:
                draft_.name += event.text.text;
                dirty_ = true;
                return true;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_BACKSPACE) {
                    if (!draft_.name.empty()) {
                        draft_.name.pop_back();
                        dirty_ = true;
                    }
                } else if (event.key.key == SDLK_RETURN ||
                           event.key.key == SDLK_ESCAPE) {
                    editing_name_ = false;
                    SDL_StopTextInput(window_);
                }
                return true;
            default:
                return false;
        }
    }

    if (event.type != SDL_EVENT_KEY_DOWN) {
        return false;
    }

    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    switch (event.key.key) {
        case SDLK_UP:
            move_focus(-1);
            return true;
        case SDLK_DOWN:
            move_focus(1);
            return true;
        case SDLK_LEFT:
            adjust(-1, shift);
            return true;
        case SDLK_RIGHT:
            adjust(1, shift);
            return true;
        case SDLK_PAGEUP:
            if (selected_ > 0) {
                select(selected_ - 1);
            }
            return true;
        case SDLK_PAGEDOWN:
            select(selected_ + 1);
            return true;
        case SDLK_RETURN:
            if (focus_ == Field::Name) {
                editing_name_ = true;
                SDL_StartTextInput(window_);
            } else {
                adjust(1, shift);
            }
            return true;
        case SDLK_S:
            save();
            return true;
        case SDLK_N:
            create_new();
            return true;
        case SDLK_R:
            reload();
            message_ = "reloaded from the database";
            return true;
        case SDLK_DELETE:
            // Shift required: retiring spends an id permanently.
            if (shift) {
                retire_selected();
            } else {
                message_ = "shift+delete to retire (the id is spent forever)";
            }
            return true;
        default:
            break;
    }

    if (event.key.key >= SDLK_0 && event.key.key <= SDLK_9) {
        type_digit(static_cast<int>(event.key.key - SDLK_0));
        return true;
    }
    return false;
}

std::string ItemMode::label_of(Field field) const {
    switch (field) {
        case Field::Name:            return "name";
        case Field::FlagBlocksWalk:  return "blocks walk";
        case Field::FlagBlocksSight: return "blocks sight";
        case Field::FlagGround:      return "is ground";
        case Field::FlagPickable:    return "pickable";
        case Field::FlagStackable:   return "stackable";
        case Field::FlagContainer:   return "container";
        case Field::Weight:          return "weight";
        case Field::MaxStack:        return "max stack";
        case Field::Equippable:      return "equippable";
        case Field::Slot:            return "slot";
        case Field::Attack:          return "attack";
        case Field::Defense:         return "defense";
        case Field::AttackKind:      return "attack kind";
        case Field::AttackRange:     return "range (tiles)";
        case Field::Effect:          return "effect id";
        case Field::Count:           break;
    }
    return "";
}

std::string ItemMode::value_of(Field field) const {
    const sim::ItemType& t = draft_.type;
    const auto yes_no = [](bool value) {
        return std::string(value ? "yes" : "no");
    };
    switch (field) {
        case Field::Name:            return draft_.name;
        case Field::FlagBlocksWalk:  return yes_no(t.blocks_walk());
        case Field::FlagBlocksSight: return yes_no(t.blocks_sight());
        case Field::FlagGround:      return yes_no(t.is_ground());
        case Field::FlagPickable:
            return yes_no(t.flags.has(sim::ItemFlag::Pickable));
        case Field::FlagStackable:
            return yes_no(t.flags.has(sim::ItemFlag::Stackable));
        case Field::FlagContainer:
            return yes_no(t.flags.has(sim::ItemFlag::Container));
        case Field::Weight:     return std::to_string(t.weight);
        case Field::MaxStack:   return std::to_string(t.max_stack);
        case Field::Equippable: return yes_no(t.equippable);
        case Field::Slot:
            return kSlotNames[static_cast<std::size_t>(t.slot)];
        case Field::Attack:  return std::to_string(t.attack);
        case Field::Defense: return std::to_string(t.defense);
        case Field::AttackKind:
            return t.attack_kind == sim::AttackKind::Ranged ? "ranged" : "melee";
        case Field::AttackRange: return std::to_string(t.attack_range);
        case Field::Effect:      return std::to_string(t.effect);
        case Field::Count:       break;
    }
    return "";
}

std::string ItemMode::status() const {
    if (rows_.empty()) {
        return "items: none (N to create)";
    }
    return "item " + std::to_string(draft_.type.id) + " " + draft_.name +
           (dirty_ ? " *" : "");
}

void ItemMode::draw(client::Renderer2D& renderer) const {
    const float vh = static_cast<float>(renderer.viewport_height());
    const float list_h = vh - 2.0F * kMargin - 40.0F;
    const auto visible_rows =
        static_cast<std::size_t>((list_h - kTitleH) / kRowH);

    // Whole-screen scrim: this is a modal mode, and a half-visible map behind an
    // item form reads as a rendering bug.
    client::ui::fill(renderer, *tileset_, 0.0F, 0.0F,
                     static_cast<float>(renderer.viewport_width()), vh,
                     client::Color{10, 11, 15, 245});

    // --- the item list ---------------------------------------------------------
    client::ui::fill(renderer, *tileset_, kMargin, kMargin, kListW, list_h,
                     kPanel);
    client::ui::text(renderer, *tileset_, "items", kMargin + 8.0F,
                     kMargin + 6.0F, kBright, kTextScale);

    std::size_t first = scroll_;
    if (selected_ >= first + visible_rows) {
        first = selected_ - visible_rows + 1U;
    } else if (selected_ < first) {
        first = selected_;
    }
    for (std::size_t i = 0; i < visible_rows && first + i < rows_.size(); ++i) {
        const store::ItemRow& row = rows_[first + i];
        const float y = kMargin + kTitleH + static_cast<float>(i) * kRowH;
        const bool is_selected = (first + i) == selected_;
        if (is_selected) {
            client::ui::fill(renderer, *tileset_, kMargin + 4.0F, y - 2.0F,
                             kListW - 8.0F, kRowH, kRowSelected,
                             client::ui::kDepth + 1.0F);
        }
        char line[64];
        std::snprintf(line, sizeof line, "%4u %s",
                      static_cast<unsigned>(row.type.id), row.name.c_str());
        client::ui::text(renderer, *tileset_, line, kMargin + 8.0F, y,
                         is_selected ? client::Color{20, 18, 10, 255} : kLabel,
                         kHintScale * 1.5F);
    }

    // --- the form --------------------------------------------------------------
    const float form_h = kTitleH + static_cast<float>(kFieldCount) * kRowH + 20.0F;
    client::ui::fill(renderer, *tileset_, kFormX, kMargin, kFormW, form_h, kPanel);

    char title[96];
    std::snprintf(title, sizeof title, "id %u",
                  static_cast<unsigned>(draft_.type.id));
    client::ui::text(renderer, *tileset_, title, kFormX + 8.0F, kMargin + 6.0F,
                     kBright, kTextScale);
    if (dirty_) {
        client::ui::text(renderer, *tileset_, "unsaved", kFormX + kFormW - 90.0F,
                         kMargin + 6.0F, kRowSelected, kTextScale * 0.75F);
    }

    for (int i = 0; i < kFieldCount; ++i) {
        const Field field = static_cast<Field>(i);
        const float y = kMargin + kTitleH + static_cast<float>(i) * kRowH;
        const bool focused = field == focus_;
        const bool active = applicable(field);

        if (focused) {
            client::ui::fill(renderer, *tileset_, kFormX + 4.0F, y - 2.0F,
                             kFormW - 8.0F, kRowH, kFocusRow,
                             client::ui::kDepth + 1.0F);
        }
        const client::Color label_colour = active ? kLabel : kDim;
        const client::Color value_colour =
            active ? (focused ? kRowSelected : kBright) : kDim;

        client::ui::text(renderer, *tileset_, label_of(field), kFormX + 10.0F, y,
                         label_colour, kHintScale * 1.5F);

        std::string value = value_of(field);
        if (field == Field::Name && editing_name_) {
            value += "_";  // a caret, so it is obvious the keyboard is captured
        }
        if (!active) {
            value = field == Field::AttackRange ? "- (melee is always 1)" : "-";
        }
        client::ui::text(renderer, *tileset_, value, kFormX + kValueX, y,
                         value_colour, kHintScale * 1.5F);
    }

    // --- hints and messages ----------------------------------------------------
    const float hint_y = vh - 34.0F;
    client::ui::text(renderer, *tileset_,
                     "up/down field   left/right change (shift x10)   digits type"
                     "   enter edit name",
                     kMargin, hint_y, kDim, kHintScale);
    client::ui::text(renderer, *tileset_,
                     "pgup/pgdn item   N new   S save+bake   R reload   "
                     "shift+del retire   F2 back to map",
                     kMargin, hint_y + 12.0F, kDim, kHintScale);
    if (!message_.empty()) {
        client::ui::text(renderer, *tileset_, message_, kFormX, hint_y - 18.0F,
                         kRowSelected, kHintScale * 1.5F);
    }
}

}  // namespace editor
