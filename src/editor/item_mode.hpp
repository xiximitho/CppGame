#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "client/renderer2d.hpp"
#include "client/tileset.hpp"
#include "atlas_meta.hpp"
#include "store/content.hpp"
#include "store/db.hpp"

// The item editor: the panel that makes adding and tuning an item authoring rather
// than programming.
//
// Before this existed, a new item with game rules meant editing sim/tile_ids.hpp and
// sim/src/item_type.cpp and rebuilding — docs/authoring.md said so in as many words.
// Now it is a row in content.db, and this is the thing that writes the row.
//
// Saving does two writes on purpose: the database (the source of truth, which the
// server reads) and the baked blob (which the client reads, because it cannot open a
// database — see docs/content.md). Leaving the second to a terminal command would
// mean the person authoring items needs a terminal, which defeats the point.

namespace editor {

/// Owns its own selection and edit state; the map editor around it keeps owning the
/// map. Nothing here touches the map.
class ItemMode {
public:
    /// `on_atlas_changed` is called after atlas.txt is written, so the editor can
    /// reload the tileset and have the new sprite show up immediately instead of on
    /// the next launch.
    ItemMode(store::Db& db, const client::Tileset& tileset, SDL_Window* window,
             std::string blob_path, std::string atlas_path,
             std::function<void()> on_atlas_changed);

    /// Re-reads every row from the database, discarding unsaved edits.
    bool reload();

    /// Returns true when the event was consumed and must not reach the map editor.
    /// Needs the renderer because the sprite picker's hit test and its drawing must
    /// use one geometry, and that geometry depends on the viewport size.
    bool handle_event(const SDL_Event& event,
                      const client::Renderer2D& renderer);

    void draw(client::Renderer2D& renderer) const;

    /// True when the draft differs from what is stored.
    bool dirty() const { return dirty_; }

    /// Writes the draft to the database and re-bakes the client's blob.
    bool save();

    /// Called when the mode is left, so a half-typed name does not keep the
    /// keyboard captured.
    void on_exit();

    /// Text for the editor's title bar / status line.
    std::string status() const;

    /// Opens the sprite picker. Exists so a headless screenshot can reach it:
    /// the dummy video driver delivers no keystrokes.
    void open_picker() { picking_ = true; }

    /// Binds a cell without going through the picker. Two uses: verifying the write
    /// path headlessly, and scripting a batch of bindings when a sheet of real art
    /// arrives and clicking 40 cells by hand is the wrong tool.
    bool bind_from_command(sim::ItemTypeId id, const std::string& kind, int cell_x,
                           int cell_y);

private:
    /// One editable property. Order is the order they are drawn and navigated in.
    enum class Field {
        Name,
        FlagBlocksWalk,
        FlagBlocksSight,
        FlagGround,
        FlagPickable,
        FlagStackable,
        FlagContainer,
        FlagStairsUp,
        FlagStairsDown,
        Weight,
        MaxStack,
        Equippable,
        Slot,
        Attack,
        Defense,
        AttackKind,
        AttackRange,
        Effect,
        SpriteKind,
        Sprite,
        Count,
    };

    static constexpr int kFieldCount = static_cast<int>(Field::Count);

    /// Fields that do not apply to the current draft are skipped when navigating
    /// and dimmed when drawn: an attack range on a melee weapon is noise, and a
    /// slot on something you cannot wear is worse than noise.
    bool applicable(Field field) const;

    /// Moves focus by `delta`, skipping fields that do not apply.
    void move_focus(int delta);

    /// Applies +/- to the focused field. Booleans toggle, enums cycle, numbers step
    /// by `delta` (times ten when `coarse`).
    void adjust(int delta, bool coarse);

    /// Types a digit into the focused numeric field.
    void type_digit(int digit);

    void select(std::size_t index);

    /// Which atlas.txt line kind this item's sprite is written as. Derived from the
    /// item, then overridable — an item can legitimately need both a map sprite and
    /// an inventory icon, and only the author knows which one is being set.
    std::string derived_sprite_kind() const;
    /// Re-reads the binding for the current item and sprite kind from atlas.txt.
    void refresh_binding();
    /// Writes the picked cell into atlas.txt and reloads the tileset.
    bool bind_sprite(int cell_x, int cell_y);

    void draw_form(client::Renderer2D& renderer) const;
    void draw_picker(client::Renderer2D& renderer) const;
    /// Atlas cell under a window point, if the point is on the sheet.
    bool picker_cell_at(const client::Renderer2D& renderer, float mx, float my,
                        int& cell_x, int& cell_y) const;
    /// Where the atlas sheet is drawn, and at what magnification.
    void picker_geometry(const client::Renderer2D& renderer, float& x, float& y,
                         float& scale) const;
    bool create_new();
    bool retire_selected();

    /// Regenerates the client's blob from the database.
    bool bake();

    std::string label_of(Field field) const;
    std::string value_of(Field field) const;

    store::Db*             db_;
    const client::Tileset* tileset_;
    SDL_Window*            window_;
    std::string            blob_path_;

    std::vector<store::ItemRow> rows_;
    store::ItemRow              draft_{};
    std::size_t                 selected_ = 0;
    std::size_t                 scroll_ = 0;
    Field                       focus_ = Field::Name;
    bool                        editing_name_ = false;
    bool                        dirty_ = false;
    std::string                 message_;

    std::string                           atlas_path_;
    std::function<void()>                 on_atlas_changed_;
    /// Overrides derived_sprite_kind() when non-empty.
    std::string                           sprite_kind_;
    std::optional<AtlasBinding>           binding_;
    bool                                  picking_ = false;
    float                                 mouse_x_ = 0.0F;
    float                                 mouse_y_ = 0.0F;
};

}  // namespace editor
