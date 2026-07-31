#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "atlas_meta.hpp"
#include "client/renderer2d.hpp"
#include "client/tileset.hpp"
#include "sim/monster_type.hpp"

// The mob editor: which animation does this monster class use?
//
// A mob class is two halves that live in different files on purpose. The NUMBERS —
// hp, speed, aggro, loot — are simulation and live in assets/monsters.txt, which the
// server reads; changing them needs no build and no rebake, so there is nothing for a
// tool to add. The SPRITES are presentation and live in assets/tilesets/atlas.txt,
// bound to the class's `appearance`. This mode edits that second half, and only that:
// it never writes monsters.txt.
//
// Why it needs to exist: an animation is not one rectangle. It is a cell size, a
// number of directions, a number of frames and an origin, all of which have to agree
// with how the art was cut, and the only way to know they agree is to watch the thing
// walk. So the panel previews the whole set as a grid AND animates one cell with the
// same anim::walk_frame the game uses — if the preview limps, the game limps.
//
// Pixels are somebody else's job: tools/import_otsp.py cuts a creature out of a
// sprite sheet and pastes it into atlas.png. This binds what is already in the atlas.

namespace editor {

/// A mob class as this panel needs it: the parts of sim::MonsterType that name it,
/// plus its appearance id. Copied out of the registry so the panel holds no reference
/// into it and reloading is a plain assignment.
struct MobRow {
    sim::MonsterTypeId id = 0;
    std::string        name;
    std::uint16_t      appearance = 0;
};

class MobMode {
public:
    /// `on_atlas_changed` is called after atlas.txt is written, so the editor can
    /// rebuild the tileset and the new animation shows up at once rather than on the
    /// next launch — same contract as ItemMode.
    MobMode(const client::Tileset& tileset, std::string atlas_path,
            std::function<void()> on_atlas_changed);

    /// Re-reads the classes from monsters.txt and the bindings from atlas.txt,
    /// discarding unsaved edits.
    bool reload();

    /// True when the event was consumed and must not reach the map editor.
    bool handle_event(const SDL_Event& event, const client::Renderer2D& renderer);

    void draw(client::Renderer2D& renderer) const;

    bool dirty() const { return dirty_; }

    /// Writes the draft strip into atlas.txt and reloads the tileset.
    bool save();

    /// Text for the title bar.
    std::string status() const;

    /// Selects a class by its monsters.txt id. Exists for the same reason --brush
    /// does: under the dummy video driver no keystroke can move the selection, so a
    /// headless screenshot could only ever show the first class.
    bool select_class(sim::MonsterTypeId id);

    /// Opens the sprite picker, for the same reason ItemMode has this: the dummy
    /// video driver delivers no keystrokes, so a headless screenshot cannot reach it.
    void open_picker() { picking_ = true; }

    /// Binds a whole strip without the UI. Both a headless check of the write path
    /// and the way to script a batch of classes after a sheet import.
    bool bind_from_command(std::uint16_t appearance, int cell_x, int cell_y, int dirs,
                           int frames, int cell_w, int cell_h);

private:
    enum class Field {
        Sprite,   ///< the atlas cell the strip starts at (opens the picker)
        CellW,
        CellH,
        Dirs,
        Frames,
        Tilt,     ///< degrees of lean, for art drawn on an axis-aligned grid
        OriginX,
        OriginY,
        Count,
    };
    static constexpr int kFieldCount = static_cast<int>(Field::Count);

    void select(std::size_t index);
    void move_focus(int delta);
    void adjust(int delta, bool coarse);
    /// Re-reads the selected class's strip from atlas.txt into the draft.
    void refresh_binding();
    /// The strip a class with no `mobstrip` line should start editing from: the size
    /// its existing per-direction art uses, if it has any.
    MobStrip default_strip(std::uint16_t appearance,
                           const std::string& atlas_text) const;

    /// True when the draft's cells fit inside the atlas. A strip that runs off the
    /// right edge is the one mistake the picker cannot show you, because the cells
    /// that fell off are simply not drawn.
    bool draft_fits() const;

    /// One cell of the DRAFT (not of the loaded tileset), so the preview shows edits
    /// before they are saved.
    client::AtlasEntry draft_cell(int dir, int frame) const;

    void draw_list(client::Renderer2D& renderer) const;
    void draw_form(client::Renderer2D& renderer) const;
    void draw_preview(client::Renderer2D& renderer) const;
    void draw_picker(client::Renderer2D& renderer) const;
    void picker_geometry(const client::Renderer2D& renderer, float& x, float& y,
                         float& scale) const;
    bool picker_cell_at(const client::Renderer2D& renderer, float mx, float my,
                        int& cell_x, int& cell_y) const;

    std::string label_of(Field field) const;
    std::string value_of(Field field) const;

    const client::Tileset* tileset_;
    std::string            atlas_path_;
    std::function<void()>  on_atlas_changed_;

    std::vector<MobRow> rows_;
    std::size_t         selected_ = 0;
    MobStrip            draft_{};
    /// What atlas.txt currently says, to show what is being replaced and to tell a
    /// class with art from one without.
    std::optional<MobStrip> stored_;
    Field                   focus_ = Field::Sprite;
    bool                    dirty_ = false;
    bool                    picking_ = false;
    std::string             message_;
    float                   mouse_x_ = 0.0F;
    float                   mouse_y_ = 0.0F;
};

}  // namespace editor
