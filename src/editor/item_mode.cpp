#include "item_mode.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
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
                   SDL_Window* window, std::string blob_path,
                   std::string atlas_path,
                   std::function<void()> on_atlas_changed)
    : db_(&db),
      tileset_(&tileset),
      window_(window),
      blob_path_(std::move(blob_path)),
      atlas_path_(std::move(atlas_path)),
      on_atlas_changed_(std::move(on_atlas_changed)) {
    reload();
    refresh_binding();
}

std::string ItemMode::derived_sprite_kind() const {
    if (!sprite_kind_.empty()) {
        return sprite_kind_;
    }
    // Semantic, not id-band based: something you stand on is ground, something you
    // wear shows as an inventory icon, everything else is scenery on the map.
    if (draft_.type.is_ground()) {
        return "ground";
    }
    return draft_.type.equippable ? "item" : "object";
}

void ItemMode::refresh_binding() {
    binding_.reset();
    std::ifstream in(atlas_path_, std::ios::binary);
    if (!in) {
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    binding_ = find_binding(buffer.str(), derived_sprite_kind(), draft_.type.id);
}

bool ItemMode::bind_sprite(int cell_x, int cell_y) {
    std::string text;
    {
        std::ifstream in(atlas_path_, std::ios::binary);
        if (!in) {
            message_ = "cannot read atlas.txt";
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        text = buffer.str();
    }

    AtlasBinding binding;
    binding.kind = derived_sprite_kind();
    binding.id = draft_.type.id;
    cell_size_for(binding.kind, binding.w, binding.h);
    binding.x = cell_x * binding.w;
    binding.y = cell_y * binding.h;
    apply_canonical_origin(binding);

    const std::string updated = upsert_binding(text, binding);
    if (updated == text) {
        // Picking the cell it already has is a no-op, not a failure: nothing to
        // write, nothing to reload, and the picker should still close.
        message_ = "already bound there";
        return true;
    }

    {
        std::ofstream out(atlas_path_, std::ios::binary | std::ios::trunc);
        if (!out) {
            message_ = "cannot write atlas.txt";
            return false;
        }
        out << updated;
        // Closed HERE, explicitly, before the reload below re-reads this same file.
        // Letting the destructor do it at the end of the function meant the reload
        // read a file that had been truncated but not yet written — the atlas failed
        // to parse, the tileset fell back to procedural art, and every sprite in the
        // editor suddenly looked wrong.
        out.close();
        if (!out.good()) {
            message_ = "failed writing atlas.txt";
            return false;
        }
    }

    binding_ = binding;
    // The tileset holds the uploaded texture and the id->rect table, so it has to be
    // rebuilt for the new binding to be visible anywhere in this process.
    if (on_atlas_changed_) {
        on_atlas_changed_();
    }
    message_ = "bound sprite at " + std::to_string(binding.x) + "," +
               std::to_string(binding.y);
    return true;
}

bool ItemMode::bind_from_command(sim::ItemTypeId id, const std::string& kind,
                                 int cell_x, int cell_y) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].type.id == id) {
            select(i);
            sprite_kind_ = kind;
            refresh_binding();
            return bind_sprite(cell_x, cell_y);
        }
    }
    LOG_ERROR("no item %u in the catalogue", static_cast<unsigned>(id));
    return false;
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
    sprite_kind_.clear();  // back to the kind derived from the item
    refresh_binding();
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
        case Field::FlagStairsUp:
            t.flags = toggled(t.flags, sim::ItemFlag::StairsUp);
            break;
        case Field::FlagStairsDown:
            t.flags = toggled(t.flags, sim::ItemFlag::StairsDown);
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
        case Field::SpriteKind: {
            // Cycles independently of the item's own nature, because an item can
            // legitimately need both a map sprite and an inventory icon and only the
            // author knows which is being bound right now.
            static const char* const kKinds[] = {"ground", "object", "item"};
            const std::string current = derived_sprite_kind();
            int index = 0;
            for (int i = 0; i < 3; ++i) {
                if (current == kKinds[i]) {
                    index = i;
                    break;
                }
            }
            index = ((index + delta) % 3 + 3) % 3;
            sprite_kind_ = kKinds[index];
            refresh_binding();
            // Not a change to the item, so nothing to save.
            return;
        }
        case Field::Sprite:
            picking_ = true;
            return;
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

bool ItemMode::picker_cell_at(const client::Renderer2D& renderer, float mx,
                              float my, int& cell_x, int& cell_y) const {
    int cell_w = 0;
    int cell_h = 0;
    cell_size_for(derived_sprite_kind(), cell_w, cell_h);
    if (cell_w <= 0 || cell_h <= 0 || tileset_->atlas_width() <= 0) {
        return false;
    }

    float ox = 0.0F;
    float oy = 0.0F;
    float scale = 1.0F;
    picker_geometry(renderer, ox, oy, scale);

    const float rel_x = (mx - ox) / scale;
    const float rel_y = (my - oy) / scale;
    if (rel_x < 0.0F || rel_y < 0.0F ||
        rel_x >= static_cast<float>(tileset_->atlas_width()) ||
        rel_y >= static_cast<float>(tileset_->atlas_height())) {
        return false;
    }
    cell_x = static_cast<int>(rel_x) / cell_w;
    cell_y = static_cast<int>(rel_y) / cell_h;
    return cell_x < tileset_->atlas_width() / cell_w &&
           cell_y < tileset_->atlas_height() / cell_h;
}

bool ItemMode::handle_event(const SDL_Event& event,
                            const client::Renderer2D& renderer) {
    // The picker is a sub-mode and takes everything while it is up.
    if (picking_) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION:
                mouse_x_ = event.motion.x;
                mouse_y_ = event.motion.y;
                return true;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                int cell_x = 0;
                int cell_y = 0;
                if (event.button.button == SDL_BUTTON_LEFT &&
                    picker_cell_at(renderer, mouse_x_, mouse_y_, cell_x, cell_y)) {
                    if (bind_sprite(cell_x, cell_y)) {
                        picking_ = false;
                    }
                }
                return true;
            }
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE ||
                    event.key.key == SDLK_F2) {
                    picking_ = false;
                    message_.clear();
                }
                return true;
            default:
                return true;
        }
    }

    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        mouse_x_ = event.motion.x;
        mouse_y_ = event.motion.y;
        return false;  // the map editor keeps its own copy too
    }

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
        case Field::FlagStairsUp:    return "stairs up";
        case Field::FlagStairsDown:  return "stairs down";
        case Field::Weight:          return "weight";
        case Field::MaxStack:        return "max stack";
        case Field::Equippable:      return "equippable";
        case Field::Slot:            return "slot";
        case Field::Attack:          return "attack";
        case Field::Defense:         return "defense";
        case Field::AttackKind:      return "attack kind";
        case Field::AttackRange:     return "range (tiles)";
        case Field::Effect:          return "effect id";
        case Field::SpriteKind:      return "sprite kind";
        case Field::Sprite:          return "sprite";
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
        case Field::FlagStairsUp:
            return yes_no(t.flags.has(sim::ItemFlag::StairsUp));
        case Field::FlagStairsDown:
            return yes_no(t.flags.has(sim::ItemFlag::StairsDown));
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
        case Field::SpriteKind:  return derived_sprite_kind();
        case Field::Sprite:
            if (!binding_.has_value()) {
                return "none - enter to pick";
            }
            return std::to_string(binding_->x) + "," + std::to_string(binding_->y) +
                   "  " + std::to_string(binding_->w) + "x" +
                   std::to_string(binding_->h);
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

void ItemMode::picker_geometry(const client::Renderer2D& renderer, float& x,
                               float& y, float& scale) const {
    // Magnify by whole numbers only: a 16x16 icon at a fractional scale lands the
    // grid lines between pixels and the cells stop lining up with what you see.
    const float aw = static_cast<float>(tileset_->atlas_width());
    const float ah = static_cast<float>(tileset_->atlas_height());
    scale = 2.0F;
    if (aw > 0.0F && ah > 0.0F) {
        const float fit_w = (static_cast<float>(renderer.viewport_width()) -
                             2.0F * kMargin) / aw;
        const float fit_h = (static_cast<float>(renderer.viewport_height()) -
                             120.0F) / ah;
        const float fit = std::min(fit_w, fit_h);
        scale = fit >= 3.0F ? 3.0F : (fit >= 2.0F ? 2.0F : 1.0F);
    }
    x = kMargin;
    y = kMargin + 40.0F;
}

void ItemMode::draw_picker(client::Renderer2D& renderer) const {
    const float vw = static_cast<float>(renderer.viewport_width());
    const float vh = static_cast<float>(renderer.viewport_height());
    client::ui::fill(renderer, *tileset_, 0.0F, 0.0F, vw, vh,
                     client::Color{10, 11, 15, 250});

    const std::string kind = derived_sprite_kind();
    int cell_w = 0;
    int cell_h = 0;
    cell_size_for(kind, cell_w, cell_h);

    char header[128];
    std::snprintf(header, sizeof header,
                  "pick a %s sprite for id %u  (%dx%d cells)", kind.c_str(),
                  static_cast<unsigned>(draft_.type.id), cell_w, cell_h);
    client::ui::text(renderer, *tileset_, header, kMargin, kMargin, kBright,
                     kTextScale);

    float ox = 0.0F;
    float oy = 0.0F;
    float scale = 1.0F;
    picker_geometry(renderer, ox, oy, scale);

    const float aw = static_cast<float>(tileset_->atlas_width());
    const float ah = static_cast<float>(tileset_->atlas_height());
    if (aw <= 0.0F || ah <= 0.0F) {
        client::ui::text(renderer, *tileset_, "no atlas loaded", kMargin,
                         oy, kDim, kTextScale);
        return;
    }

    // A dark plate behind the sheet: the atlas is mostly transparent, and without
    // this the sprites float on the scrim and the grid is unreadable.
    client::ui::fill(renderer, *tileset_, ox, oy, aw * scale, ah * scale,
                     client::Color{30, 32, 40, 255});

    client::AtlasEntry sheet;
    sheet.uv = client::Rect{0.0F, 0.0F, 1.0F, 1.0F};
    sheet.width = aw;
    sheet.height = ah;
    sheet.valid = true;
    client::ui::sprite(renderer, *tileset_, sheet, ox, oy, aw * scale, ah * scale,
                       client::Color{255, 255, 255, 255},
                       client::ui::kDepth + 1.0F);

    // Grid. One thin quad per line; at 64px cells that is a handful, at 16px it is
    // still well under a hundred and they all batch with everything else.
    const client::Color grid{255, 255, 255, 40};
    const float step_x = static_cast<float>(cell_w) * scale;
    const float step_y = static_cast<float>(cell_h) * scale;
    for (float gx = ox; gx <= ox + aw * scale + 0.5F; gx += step_x) {
        client::ui::fill(renderer, *tileset_, gx, oy, 1.0F, ah * scale, grid,
                         client::ui::kDepth + 2.0F);
    }
    for (float gy = oy; gy <= oy + ah * scale + 0.5F; gy += step_y) {
        client::ui::fill(renderer, *tileset_, ox, gy, aw * scale, 1.0F, grid,
                         client::ui::kDepth + 2.0F);
    }

    // The cell currently bound, so it is obvious what is being replaced.
    if (binding_.has_value() && binding_->kind == kind) {
        client::ui::fill(renderer, *tileset_,
                         ox + static_cast<float>(binding_->x) * scale,
                         oy + static_cast<float>(binding_->y) * scale,
                         static_cast<float>(binding_->w) * scale,
                         static_cast<float>(binding_->h) * scale,
                         client::Color{90, 200, 120, 70},
                         client::ui::kDepth + 3.0F);
    }

    // Hover.
    const int cols = tileset_->atlas_width() / cell_w;
    const int rows = tileset_->atlas_height() / cell_h;
    const float rel_x = (mouse_x_ - ox) / scale;
    const float rel_y = (mouse_y_ - oy) / scale;
    if (rel_x >= 0.0F && rel_y >= 0.0F && rel_x < aw && rel_y < ah) {
        const int cx = static_cast<int>(rel_x) / cell_w;
        const int cy = static_cast<int>(rel_y) / cell_h;
        if (cx < cols && cy < rows) {
            client::ui::fill(renderer, *tileset_,
                             ox + static_cast<float>(cx * cell_w) * scale,
                             oy + static_cast<float>(cy * cell_h) * scale,
                             step_x, step_y, client::Color{201, 162, 39, 90},
                             client::ui::kDepth + 4.0F);
        }
    }

    client::ui::text(renderer, *tileset_,
                     "click a cell to bind   esc cancel", kMargin, vh - 22.0F,
                     kDim, kHintScale);
    if (!message_.empty()) {
        client::ui::text(renderer, *tileset_, message_, kMargin, vh - 38.0F,
                         kRowSelected, kHintScale * 1.5F);
    }
}

void ItemMode::draw(client::Renderer2D& renderer) const {
    if (picking_) {
        draw_picker(renderer);
        return;
    }
    draw_form(renderer);
}

void ItemMode::draw_form(client::Renderer2D& renderer) const {
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
