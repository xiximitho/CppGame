#include "mob_mode.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

#include "client/animation.hpp"
#include "client/content.hpp"
#include "client/ui.hpp"
#include "core/log.hpp"

namespace editor {
namespace {

// Layout, in window pixels. Same proportions as the item form so switching between
// the two modes does not feel like switching tools.
constexpr float kMargin = 16.0F;
constexpr float kListW = 250.0F;
constexpr float kFormX = kMargin + kListW + kMargin;
constexpr float kFormW = 380.0F;
constexpr float kRowH = 20.0F;
constexpr float kTitleH = 30.0F;
constexpr float kTextScale = 2.0F;
constexpr float kHintScale = 1.0F;
constexpr float kValueX = 170.0F;

const client::Color kPanel{16, 18, 24, 240};
const client::Color kRowSelected{201, 162, 39, 255};
const client::Color kBright{236, 240, 248, 255};
const client::Color kDim{120, 126, 140, 255};
const client::Color kLabel{164, 172, 188, 255};
const client::Color kFocusRow{44, 48, 58, 255};
const client::Color kWhite{255, 255, 255, 255};

/// How long one previewed step takes, in milliseconds. The player's step is 9 ticks
/// at 30 Hz, so 300 ms is what walking actually looks like — a preview at some
/// arbitrary speed would have you tuning frames against the wrong thing.
constexpr std::uint64_t kPreviewStepMs = 300;

/// Names for the art directions, in the atlas' order. Spelled out in the panel
/// because "direction 0" tells you nothing about which sprite should be in it.
constexpr const char* kDirNames4[] = {"back", "right", "front", "left"};
constexpr const char* kDirNames8[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

int clamp_int(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

MobMode::MobMode(const client::Tileset& tileset, std::string atlas_path,
                 std::function<void()> on_atlas_changed)
    : tileset_(&tileset),
      atlas_path_(std::move(atlas_path)),
      on_atlas_changed_(std::move(on_atlas_changed)) {
    reload();
}

bool MobMode::reload() {
    // The same loader the client and the solo session use, so the list here is the
    // list the game will have. A second parser for monsters.txt is a format that
    // drifts (the comment in client/content.hpp says so, and this is why it is
    // reused rather than re-read).
    const sim::MonsterRegistry registry = client::load_monster_catalogue();
    rows_.clear();
    for (const sim::MonsterTypeId id : registry.ids()) {
        const sim::MonsterType& type = registry.get(id);
        rows_.push_back(MobRow{id, type.name, type.appearance});
    }
    std::sort(rows_.begin(), rows_.end(),
              [](const MobRow& a, const MobRow& b) { return a.id < b.id; });
    if (rows_.empty()) {
        message_ = "no monster classes in monsters.txt";
        return false;
    }
    selected_ = std::min(selected_, rows_.size() - 1U);
    refresh_binding();
    return true;
}

MobStrip MobMode::default_strip(std::uint16_t appearance,
                                const std::string& atlas_text) const {
    MobStrip strip;
    strip.appearance = appearance;
    strip.dirs = client::anim::kArtDirsTibia;
    strip.frames = 3;
    apply_canonical_mob_origin(strip);

    // A class with no strip yet may still have the older per-direction art, and its
    // first `mob` line says exactly where and at what size. Starting from that means
    // opening a static class shows the art it actually has instead of whatever
    // happens to sit at 0,0 — and turning it into an animation is then an edit rather
    // than a blank form. (Not read from the Tileset, whose AtlasEntry has already
    // been converted to normalised uv with a half-texel bias; the text is exact.)
    std::istringstream lines(atlas_text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string kind;
        int id = 0;
        int dir = 0;
        if (!(fields >> kind >> id >> dir) || kind != "mob" ||
            id != static_cast<int>(appearance) || dir != 0) {
            continue;
        }
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        float ox = 0.0F;
        float oy = 0.0F;
        if (fields >> x >> y >> w >> h >> ox >> oy) {
            strip.x = x;
            strip.y = y;
            strip.cell_w = w;
            strip.cell_h = h;
            // Its eight directions ARE a strip of eight one-frame cells, laid out
            // exactly the way a mobstrip line describes: saving without changing
            // anything else rebinds the same art, byte for byte.
            strip.dirs = client::anim::kArtDirsFull;
            strip.frames = 1;
            strip.origin_x = ox;
            strip.origin_y = oy;
        }
        break;
    }
    return strip;
}

void MobMode::refresh_binding() {
    stored_.reset();
    if (rows_.empty()) {
        return;
    }
    const std::uint16_t appearance = rows_[selected_].appearance;
    const std::string text = read_file(atlas_path_);
    stored_ = find_mob_strip(text, appearance);
    draft_ = stored_.value_or(default_strip(appearance, text));
    draft_.appearance = appearance;
    dirty_ = false;
}

void MobMode::select(std::size_t index) {
    if (rows_.empty()) {
        return;
    }
    selected_ = std::min(index, rows_.size() - 1U);
    message_.clear();
    refresh_binding();
}

bool MobMode::select_class(sim::MonsterTypeId id) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id == id) {
            select(i);
            return true;
        }
    }
    LOG_WARN("no monster class %u in monsters.txt", static_cast<unsigned>(id));
    return false;
}

void MobMode::move_focus(int delta) {
    int index = static_cast<int>(focus_) + delta;
    if (index < 0) {
        index = kFieldCount - 1;
    } else if (index >= kFieldCount) {
        index = 0;
    }
    focus_ = static_cast<Field>(index);
}

void MobMode::adjust(int delta, bool coarse) {
    const int step = coarse ? delta * 8 : delta;
    switch (focus_) {
        case Field::Sprite:
            // Nudges the strip one cell along the atlas row. Coarse jumps a whole
            // strip, which is how you walk through a band of imported creatures.
            draft_.x += (coarse ? draft_.dirs * draft_.frames : 1) * delta *
                        draft_.cell_w;
            draft_.x = std::max(draft_.x, 0);
            break;
        case Field::CellW:
            draft_.cell_w = clamp_int(draft_.cell_w + step * 8, 8, 128);
            apply_canonical_mob_origin(draft_);
            break;
        case Field::CellH:
            draft_.cell_h = clamp_int(draft_.cell_h + step * 8, 8, 128);
            apply_canonical_mob_origin(draft_);
            break;
        case Field::Dirs:
            // Only two values are meaningful, and both are real: 4 is what Tibia-style
            // art has, 8 is one sprite per grid direction like the player's.
            draft_.dirs = draft_.dirs == client::anim::kArtDirsTibia
                              ? client::anim::kArtDirsFull
                              : client::anim::kArtDirsTibia;
            break;
        case Field::Frames:
            draft_.frames =
                clamp_int(draft_.frames + delta, 1, client::anim::kMaxFrames);
            break;
        case Field::Tilt:
            // One degree at a time, eight with shift. Clamped well short of a
            // quarter turn: past that you are not correcting how the art was drawn,
            // you are drawing a creature lying down.
            draft_.tilt = static_cast<float>(
                clamp_int(static_cast<int>(draft_.tilt) + step, -60, 60));
            break;
        case Field::OriginX:
            draft_.origin_x += static_cast<float>(step);
            break;
        case Field::OriginY:
            draft_.origin_y += static_cast<float>(step);
            break;
        case Field::Count:
            break;
    }
    dirty_ = true;
}

bool MobMode::draft_fits() const {
    const int span = draft_.dirs * draft_.frames * draft_.cell_w;
    return draft_.x >= 0 && draft_.y >= 0 &&
           draft_.x + span <= tileset_->atlas_width() &&
           draft_.y + draft_.cell_h <= tileset_->atlas_height();
}

client::AtlasEntry MobMode::draft_cell(int dir, int frame) const {
    const float aw = static_cast<float>(tileset_->atlas_width());
    const float ah = static_cast<float>(tileset_->atlas_height());
    client::AtlasEntry entry;
    if (aw <= 0.0F || ah <= 0.0F) {
        return entry;
    }
    // Built here instead of asked of the Tileset on purpose: the preview has to show
    // the DRAFT, and the tileset only knows what has been saved.
    const int cell = dir * draft_.frames + frame;
    const float x = static_cast<float>(draft_.x + cell * draft_.cell_w);
    const float y = static_cast<float>(draft_.y);
    const float w = static_cast<float>(draft_.cell_w);
    const float h = static_cast<float>(draft_.cell_h);
    // Half-texel inset, the same reason as in tileset.cpp: without it a NEAREST sample
    // at the cell's edge picks up the neighbouring frame.
    const float half_u = 0.5F / aw;
    const float half_v = 0.5F / ah;
    entry.uv = client::Rect{x / aw + half_u, y / ah + half_v, w / aw - 2.0F * half_u,
                            h / ah - 2.0F * half_v};
    entry.width = w;
    entry.height = h;
    entry.origin_x = draft_.origin_x;
    entry.origin_y = draft_.origin_y;
    entry.valid = true;
    return entry;
}

bool MobMode::save() {
    if (rows_.empty()) {
        return false;
    }
    if (!draft_fits()) {
        message_ = "strip runs off the atlas — check cell size and first cell";
        return false;
    }
    const std::string text = read_file(atlas_path_);
    if (text.empty()) {
        message_ = "cannot read atlas.txt";
        return false;
    }
    const std::string updated = upsert_mob_strip(text, draft_);
    if (updated == text) {
        message_ = "nothing to write";
        dirty_ = false;
        return true;
    }
    {
        std::ofstream out(atlas_path_, std::ios::binary | std::ios::trunc);
        if (!out) {
            message_ = "cannot write atlas.txt";
            return false;
        }
        out << updated;
        // Closed here, before the reload below re-reads this same file: leaving it to
        // the destructor let the tileset read a truncated atlas.txt and fall back to
        // procedural art, which looks like every sprite in the editor breaking at once
        // (the same trap ItemMode::bind_sprite documents).
        out.close();
        if (!out.good()) {
            message_ = "failed writing atlas.txt";
            return false;
        }
    }
    stored_ = draft_;
    dirty_ = false;
    if (on_atlas_changed_) {
        on_atlas_changed_();
    }
    char note[128];
    std::snprintf(note, sizeof note,
                  "bound appearance %u: %dx%d, %d dirs, %d frames, tilt %d",
                  static_cast<unsigned>(draft_.appearance), draft_.cell_w,
                  draft_.cell_h, draft_.dirs, draft_.frames,
                  static_cast<int>(draft_.tilt));
    message_ = note;
    return true;
}

bool MobMode::bind_from_command(std::uint16_t appearance, int cell_x, int cell_y,
                                int dirs, int frames, int cell_w, int cell_h) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].appearance != appearance) {
            continue;
        }
        select(i);
        draft_.cell_w = cell_w;
        draft_.cell_h = cell_h;
        draft_.dirs = dirs;
        draft_.frames = frames;
        draft_.x = cell_x * cell_w;
        draft_.y = cell_y * cell_h;
        apply_canonical_mob_origin(draft_);
        if (!save()) {
            LOG_ERROR("%s", message_.c_str());
            return false;
        }
        return true;
    }
    LOG_ERROR("no monster class uses appearance %u", static_cast<unsigned>(appearance));
    return false;
}

bool MobMode::picker_cell_at(const client::Renderer2D& renderer, float mx, float my,
                             int& cell_x, int& cell_y) const {
    if (draft_.cell_w <= 0 || draft_.cell_h <= 0 || tileset_->atlas_width() <= 0) {
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
    cell_x = static_cast<int>(rel_x) / draft_.cell_w;
    cell_y = static_cast<int>(rel_y) / draft_.cell_h;
    return true;
}

bool MobMode::handle_event(const SDL_Event& event,
                           const client::Renderer2D& renderer) {
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
                    draft_.x = cell_x * draft_.cell_w;
                    draft_.y = cell_y * draft_.cell_h;
                    dirty_ = true;
                    picking_ = false;
                    // Deliberately NOT saved here, unlike the item picker: a strip is
                    // a first cell plus a geometry, and the geometry is what you are
                    // usually still adjusting. The preview shows the pick immediately
                    // and S commits it.
                    message_ = draft_fits() ? "S to save" : "strip runs off the atlas";
                }
                return true;
            }
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_F4) {
                    picking_ = false;
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
            if (focus_ == Field::Sprite) {
                picking_ = true;
            }
            return true;
        case SDLK_S:
            save();
            return true;
        case SDLK_R:
            reload();
            message_ = "reloaded from monsters.txt and atlas.txt";
            return true;
        default:
            return false;
    }
}

std::string MobMode::label_of(Field field) const {
    switch (field) {
        case Field::Sprite:  return "first cell";
        case Field::CellW:   return "cell width";
        case Field::CellH:   return "cell height";
        case Field::Dirs:    return "directions";
        case Field::Frames:  return "frames";
        case Field::Tilt:    return "tilt";
        case Field::OriginX: return "origin x";
        case Field::OriginY: return "origin y";
        case Field::Count:   break;
    }
    return "";
}

std::string MobMode::value_of(Field field) const {
    char text[64];
    switch (field) {
        case Field::Sprite:
            std::snprintf(text, sizeof text, "%d,%d px  (enter to pick)", draft_.x,
                          draft_.y);
            return text;
        case Field::CellW:
            return std::to_string(draft_.cell_w);
        case Field::CellH:
            return std::to_string(draft_.cell_h);
        case Field::Dirs:
            std::snprintf(text, sizeof text, "%d %s", draft_.dirs,
                          draft_.dirs == client::anim::kArtDirsTibia
                              ? "(back right front left)"
                              : "(one per grid direction)");
            return text;
        case Field::Frames:
            std::snprintf(text, sizeof text, "%d %s", draft_.frames,
                          draft_.frames == 1 ? "(static)" : "");
            return text;
        case Field::Tilt:
            std::snprintf(text, sizeof text, "%d deg%s",
                          static_cast<int>(draft_.tilt),
                          draft_.tilt == 0.0F ? " (upright)" : "");
            return text;
        case Field::OriginX:
            return std::to_string(static_cast<int>(draft_.origin_x));
        case Field::OriginY:
            return std::to_string(static_cast<int>(draft_.origin_y));
        case Field::Count:
            break;
    }
    return "";
}

std::string MobMode::status() const {
    if (rows_.empty()) {
        return "mobs: none";
    }
    const MobRow& row = rows_[selected_];
    char text[160];
    std::snprintf(text, sizeof text, "mob %u %s (appearance %u) %dx%d%s",
                  static_cast<unsigned>(row.id), row.name.c_str(),
                  static_cast<unsigned>(row.appearance), draft_.dirs, draft_.frames,
                  dirty_ ? " *" : "");
    return text;
}

void MobMode::draw_list(client::Renderer2D& renderer) const {
    const float vh = static_cast<float>(renderer.viewport_height());
    client::ui::fill(renderer, *tileset_, kMargin, kMargin, kListW, vh - 2.0F * kMargin,
                     kPanel);
    client::ui::text(renderer, *tileset_, "mob classes", kMargin + 8.0F,
                     kMargin + 8.0F, kBright, kTextScale);

    float y = kMargin + kTitleH + 8.0F;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool current = i == selected_;
        if (current) {
            client::ui::fill(renderer, *tileset_, kMargin + 4.0F, y - 2.0F,
                             kListW - 8.0F, kRowH, kFocusRow,
                             client::ui::kDepth + 1.0F);
        }
        char line[96];
        std::snprintf(line, sizeof line, "%u %s", static_cast<unsigned>(rows_[i].id),
                      rows_[i].name.c_str());
        client::ui::text(renderer, *tileset_, line, kMargin + 10.0F, y,
                         current ? kRowSelected : kLabel, kTextScale);

        // Whether that class has animated art, which is the one thing you want to see
        // without clicking through every row.
        const std::uint8_t frames = tileset_->frame_count(rows_[i].appearance);
        const char* mark = frames > 1 ? "anim" : "static";
        client::ui::text(renderer, *tileset_, mark,
                         kMargin + kListW - 8.0F -
                             client::ui::text_width(*tileset_, mark, kHintScale),
                         y + 4.0F, frames > 1 ? kBright : kDim, kHintScale);
        y += kRowH;
    }
}

void MobMode::draw_form(client::Renderer2D& renderer) const {
    const float height = kTitleH + kRowH * static_cast<float>(kFieldCount) + 24.0F;
    client::ui::fill(renderer, *tileset_, kFormX, kMargin, kFormW, height, kPanel);
    client::ui::text(renderer, *tileset_, "animation", kFormX + 8.0F, kMargin + 8.0F,
                     kBright, kTextScale);

    float y = kMargin + kTitleH + 8.0F;
    for (int i = 0; i < kFieldCount; ++i) {
        const auto field = static_cast<Field>(i);
        const bool focused = field == focus_;
        if (focused) {
            client::ui::fill(renderer, *tileset_, kFormX + 4.0F, y - 2.0F,
                             kFormW - 8.0F, kRowH, kFocusRow,
                             client::ui::kDepth + 1.0F);
        }
        client::ui::text(renderer, *tileset_, label_of(field), kFormX + 10.0F, y,
                         focused ? kBright : kLabel, kTextScale);
        client::ui::text(renderer, *tileset_, value_of(field), kFormX + kValueX, y,
                         focused ? kRowSelected : kBright, kTextScale);
        y += kRowH;
    }
}

void MobMode::draw_preview(client::Renderer2D& renderer) const {
    const float vw = static_cast<float>(renderer.viewport_width());
    const float x = kFormX + kFormW + kMargin;
    const float w = std::max(vw - x - kMargin, 120.0F);
    const float y = kMargin;
    // Every frame of every direction, plus one animated cell. The grid is the part
    // that catches a wrong cell size or a wrong frame count; the animated cell is the
    // part that catches art whose frames are in the wrong order, which no static
    // picture shows.
    const float cell = static_cast<float>(std::max(draft_.cell_w, draft_.cell_h)) *
                       2.0F;
    const float height =
        kTitleH + cell * static_cast<float>(draft_.dirs) + 40.0F;
    client::ui::fill(renderer, *tileset_, x, y, w, height, kPanel);
    client::ui::text(renderer, *tileset_, "frames", x + 8.0F, y + 8.0F, kBright,
                     kTextScale);

    if (!draft_fits()) {
        client::ui::text(renderer, *tileset_, "outside the atlas", x + 8.0F,
                         y + kTitleH + 8.0F, client::Color{220, 120, 110, 255},
                         kTextScale);
        return;
    }

    // The animated frame, from the same function the game uses: a preview with its own
    // idea of how the cycle runs would be a preview of nothing.
    const std::uint64_t ms = SDL_GetTicks() % kPreviewStepMs;
    const auto progress = static_cast<std::uint8_t>(ms * 255U / kPreviewStepMs);
    const std::uint8_t animated = client::anim::walk_frame(
        true, progress, static_cast<std::uint8_t>(draft_.frames));

    float row_y = y + kTitleH + 8.0F;
    for (int dir = 0; dir < draft_.dirs; ++dir) {
        const char* name = draft_.dirs == client::anim::kArtDirsTibia
                               ? kDirNames4[dir]
                               : kDirNames8[dir];
        client::ui::text(renderer, *tileset_, name, x + 8.0F, row_y + cell * 0.5F,
                         kDim, kHintScale);
        for (int frame = 0; frame < draft_.frames; ++frame) {
            const float cx = x + 48.0F + static_cast<float>(frame) * cell;
            const bool live = frame == animated;
            client::ui::fill(renderer, *tileset_, cx, row_y, cell - 2.0F, cell - 2.0F,
                             live ? client::Color{52, 60, 46, 255}
                                  : client::Color{30, 32, 40, 255},
                             client::ui::kDepth + 1.0F);
            const client::AtlasEntry entry = draft_cell(dir, frame);
            client::ui::sprite(renderer, *tileset_, entry, cx, row_y, cell - 2.0F,
                               cell - 2.0F, kWhite, client::ui::kDepth + 2.0F,
                               draft_.tilt * 3.14159265F / 180.0F);
        }
        row_y += cell;
    }
}

void MobMode::picker_geometry(const client::Renderer2D& renderer, float& x, float& y,
                              float& scale) const {
    // Whole-number magnification only, so the cell grid keeps landing on pixel edges.
    const float aw = static_cast<float>(tileset_->atlas_width());
    const float ah = static_cast<float>(tileset_->atlas_height());
    scale = 1.0F;
    if (aw > 0.0F && ah > 0.0F) {
        const float fit_w =
            (static_cast<float>(renderer.viewport_width()) - 2.0F * kMargin) / aw;
        const float fit_h =
            (static_cast<float>(renderer.viewport_height()) - 120.0F) / ah;
        const float fit = std::min(fit_w, fit_h);
        scale = fit >= 3.0F ? 3.0F : (fit >= 2.0F ? 2.0F : 1.0F);
    }
    x = kMargin;
    y = kMargin + 40.0F;
}

void MobMode::draw_picker(client::Renderer2D& renderer) const {
    const float vw = static_cast<float>(renderer.viewport_width());
    const float vh = static_cast<float>(renderer.viewport_height());
    client::ui::fill(renderer, *tileset_, 0.0F, 0.0F, vw, vh,
                     client::Color{10, 11, 15, 250});

    char header[160];
    std::snprintf(header, sizeof header,
                  "pick the FIRST cell of the strip for appearance %u "
                  "(%d cells of %dx%d)",
                  static_cast<unsigned>(draft_.appearance),
                  draft_.dirs * draft_.frames, draft_.cell_w, draft_.cell_h);
    client::ui::text(renderer, *tileset_, header, kMargin, kMargin, kBright,
                     kTextScale);

    float ox = 0.0F;
    float oy = 0.0F;
    float scale = 1.0F;
    picker_geometry(renderer, ox, oy, scale);
    const float aw = static_cast<float>(tileset_->atlas_width());
    const float ah = static_cast<float>(tileset_->atlas_height());
    if (aw <= 0.0F || ah <= 0.0F) {
        client::ui::text(renderer, *tileset_, "no atlas loaded", kMargin, oy, kDim,
                         kTextScale);
        return;
    }

    // A plate behind the sheet: the atlas is mostly transparent and the sprites would
    // otherwise float on the scrim with no readable grid.
    client::ui::fill(renderer, *tileset_, ox, oy, aw * scale, ah * scale,
                     client::Color{30, 32, 40, 255});
    client::AtlasEntry sheet;
    sheet.uv = client::Rect{0.0F, 0.0F, 1.0F, 1.0F};
    sheet.width = aw;
    sheet.height = ah;
    sheet.valid = true;
    client::ui::sprite(renderer, *tileset_, sheet, ox, oy, aw * scale, ah * scale,
                       kWhite, client::ui::kDepth + 1.0F);

    const client::Color grid{255, 255, 255, 40};
    const float step_x = static_cast<float>(draft_.cell_w) * scale;
    const float step_y = static_cast<float>(draft_.cell_h) * scale;
    for (float gx = ox; gx <= ox + aw * scale + 0.5F; gx += step_x) {
        client::ui::fill(renderer, *tileset_, gx, oy, 1.0F, ah * scale, grid,
                         client::ui::kDepth + 2.0F);
    }
    for (float gy = oy; gy <= oy + ah * scale + 0.5F; gy += step_y) {
        client::ui::fill(renderer, *tileset_, ox, gy, aw * scale, 1.0F, grid,
                         client::ui::kDepth + 2.0F);
    }

    // The strip currently bound, whole, so it is obvious what is being replaced.
    const float span =
        static_cast<float>(draft_.dirs * draft_.frames * draft_.cell_w) * scale;
    if (stored_.has_value()) {
        client::ui::fill(
            renderer, *tileset_, ox + static_cast<float>(stored_->x) * scale,
            oy + static_cast<float>(stored_->y) * scale,
            static_cast<float>(stored_->dirs * stored_->frames * stored_->cell_w) *
                scale,
            static_cast<float>(stored_->cell_h) * scale,
            client::Color{90, 200, 120, 60}, client::ui::kDepth + 3.0F);
    }

    // Hover, highlighting the WHOLE run that would be taken rather than the one cell
    // under the cursor: the run is what gets bound, and a strip that would spill off
    // the right edge is only visible if you can see its extent.
    const float rel_x = (mouse_x_ - ox) / scale;
    const float rel_y = (mouse_y_ - oy) / scale;
    if (rel_x >= 0.0F && rel_y >= 0.0F && rel_x < aw && rel_y < ah) {
        const int cx = static_cast<int>(rel_x) / draft_.cell_w;
        const int cy = static_cast<int>(rel_y) / draft_.cell_h;
        const float hx = ox + static_cast<float>(cx * draft_.cell_w) * scale;
        const float hy = oy + static_cast<float>(cy * draft_.cell_h) * scale;
        const bool fits =
            static_cast<float>(cx * draft_.cell_w) * scale + span <= aw * scale + 0.5F;
        client::ui::fill(renderer, *tileset_, hx, hy, fits ? span : step_x, step_y,
                         fits ? client::Color{201, 162, 39, 90}
                              : client::Color{220, 90, 80, 110},
                         client::ui::kDepth + 4.0F);
    }

    client::ui::text(renderer, *tileset_,
                     "click the first cell   esc cancel", kMargin, vh - 22.0F, kDim,
                     kHintScale);
}

void MobMode::draw(client::Renderer2D& renderer) const {
    if (picking_) {
        draw_picker(renderer);
        return;
    }
    const float vh = static_cast<float>(renderer.viewport_height());
    draw_list(renderer);
    if (!rows_.empty()) {
        draw_form(renderer);
        draw_preview(renderer);
    }

    client::ui::text(renderer, *tileset_,
                     "PgUp/PgDn class   up/down field   left/right value   "
                     "enter pick cell   S save   R reload   F4 back to map",
                     kFormX, vh - 34.0F, kDim, kHintScale);
    if (!message_.empty()) {
        client::ui::text(renderer, *tileset_, message_, kFormX, vh - 20.0F,
                         kRowSelected, kHintScale);
    }
}

}  // namespace editor
