#include "client/tileset.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "client/iso.hpp"
#include "core/log.hpp"
#include "platform/vfs.hpp"
#include "sim/rng.hpp"
#include "sim/tile_ids.hpp"

namespace client {
namespace {

constexpr int kAtlasSize = 256;

// Atlas layout. Kept as explicit constants rather than a packer because the
// placeholder set is fixed; a real atlas gets packed offline by a tool.
constexpr int kGroundRowY = 0;    // four 64x32 diamonds
constexpr int kHighlightY = 32;   // one 64x32 diamond outline
constexpr int kBlockRowY  = 64;   // two 64x64 blocks (wall, tree)
constexpr int kActorRowY  = 128;  // eight 32x48 frames

constexpr int kActorFrameW = 32;
constexpr int kActorFrameH = 48;

struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

/// Tightly packed RGBA byte canvas, which is what Renderer2D::create_texture
/// expects on every platform.
class Canvas {
public:
    Canvas(int width, int height)
        : width_(width),
          height_(height),
          pixels_(static_cast<std::size_t>(width) *
                  static_cast<std::size_t>(height) * 4U, 0U) {}

    void set(int x, int y, Rgba color) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return;
        }
        const std::size_t offset =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
             static_cast<std::size_t>(x)) * 4U;
        pixels_[offset + 0] = color.r;
        pixels_[offset + 1] = color.g;
        pixels_[offset + 2] = color.b;
        pixels_[offset + 3] = color.a;
    }

    const std::uint8_t* data() const { return pixels_.data(); }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_;
    int height_;
    std::vector<std::uint8_t> pixels_;
};

Rgba shade(Rgba color, float factor) {
    const auto apply = [factor](std::uint8_t channel) {
        const float scaled = static_cast<float>(channel) * factor;
        const float clamped = scaled < 0.0F ? 0.0F : (scaled > 255.0F ? 255.0F : scaled);
        return static_cast<std::uint8_t>(clamped);
    };
    return Rgba{apply(color.r), apply(color.g), apply(color.b), color.a};
}

/// Horizontal extent of an isometric diamond at row `row`, for a w x h diamond.
/// For the canonical 64x32 tile this yields 4, 8, 12 ... 64 ... 12, 8, 4.
int diamond_span(int row, int width, int height) {
    const int half = height / 2;
    const int step = width / half;
    const int mirrored = row < half ? row : height - 1 - row;
    return step * (mirrored + 1);
}

void fill_diamond(Canvas& canvas, int origin_x, int origin_y, int width,
                  int height, Rgba fill, Rgba edge) {
    for (int row = 0; row < height; ++row) {
        const int span = diamond_span(row, width, height);
        const int start = (width - span) / 2;
        for (int col = start; col < start + span; ++col) {
            const bool on_edge = (col == start) || (col == start + span - 1);
            canvas.set(origin_x + col, origin_y + row, on_edge ? edge : fill);
        }
    }
}

/// Speckles a filled diamond so large areas of one ground type do not read as
/// flat colour. Deterministic: the same tile always gets the same noise.
void speckle_diamond(Canvas& canvas, int origin_x, int origin_y, int width,
                     int height, Rgba color, std::uint64_t seed, int count) {
    sim::Rng rng(seed);
    for (int i = 0; i < count; ++i) {
        const int row = rng.next_range(1, height - 2);
        const int span = diamond_span(row, width, height);
        const int start = (width - span) / 2;
        if (span <= 2) {
            continue;
        }
        const int col = rng.next_range(start + 1, start + span - 2);
        canvas.set(origin_x + col, origin_y + row, color);
    }
}

/// An isometric cube: top diamond plus the two visible side faces. Total height
/// is tile_height + wall_height, and the cube's BASE diamond sits at the bottom.
void fill_block(Canvas& canvas, int origin_x, int origin_y, Rgba top,
                Rgba left, Rgba right, int wall_height) {
    const int w = iso::kTileWidth;
    const int h = iso::kTileHeight;

    // Side faces first so the top diamond overwrites their upper edge cleanly.
    for (int col = 0; col < w; ++col) {
        const float distance =
            std::fabs(static_cast<float>(col) - (static_cast<float>(w) - 1.0F) * 0.5F);
        // Lower edge of the top diamond at this column.
        const float face_top =
            static_cast<float>(h) -
            (distance / (static_cast<float>(w) * 0.5F)) * static_cast<float>(h) * 0.5F;

        const int start = static_cast<int>(face_top);
        const Rgba face = col < w / 2 ? left : right;
        for (int row = start; row < start + wall_height; ++row) {
            canvas.set(origin_x + col, origin_y + row, face);
        }
    }

    fill_diamond(canvas, origin_x, origin_y, w, h, top, shade(top, 0.8F));
}

void fill_rect(Canvas& canvas, int x, int y, int w, int h, Rgba color) {
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            canvas.set(x + col, y + row, color);
        }
    }
}

void fill_circle(Canvas& canvas, int cx, int cy, int radius, Rgba color) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= radius * radius) {
                canvas.set(cx + dx, cy + dy, color);
            }
        }
    }
}

/// Squashed ellipse used as a contact shadow so actors do not look like they are
/// floating above the tile.
void fill_shadow(Canvas& canvas, int cx, int cy, int radius_x, int radius_y) {
    for (int dy = -radius_y; dy <= radius_y; ++dy) {
        for (int dx = -radius_x; dx <= radius_x; ++dx) {
            const float nx = static_cast<float>(dx) / static_cast<float>(radius_x);
            const float ny = static_cast<float>(dy) / static_cast<float>(radius_y);
            if (nx * nx + ny * ny <= 1.0F) {
                canvas.set(cx + dx, cy + dy, Rgba{0, 0, 0, 70});
            }
        }
    }
}

void draw_diamond_outline(Canvas& canvas, int origin_x, int origin_y, int width,
                          int height, Rgba color) {
    for (int row = 0; row < height; ++row) {
        const int span = diamond_span(row, width, height);
        const int start = (width - span) / 2;
        canvas.set(origin_x + start, origin_y + row, color);
        canvas.set(origin_x + start + span - 1, origin_y + row, color);
        // Second pixel inward keeps the outline visible at 1x zoom, where a
        // single-pixel diagonal nearly disappears.
        canvas.set(origin_x + start + 1, origin_y + row, color);
        canvas.set(origin_x + start + span - 2, origin_y + row, color);
    }
}

/// A small humanoid, drawn once per grid direction. The facing is communicated
/// by a bright marker offset along the isometric projection of that direction,
/// which is enough to read direction at a glance without eight art passes.
void draw_actor_frame(Canvas& canvas, int origin_x, int origin_y,
                      sim::Direction facing) {
    const Rgba skin{236, 190, 150, 255};
    const Rgba tunic{70, 110, 200, 255};
    const Rgba tunic_dark{50, 80, 160, 255};
    const Rgba legs{60, 60, 80, 255};
    const Rgba marker{255, 235, 120, 255};

    const int cx = origin_x + kActorFrameW / 2;
    const int feet_y = origin_y + kActorFrameH - 2;

    // Radii chosen so the ellipse fits inside the frame: centred on feet_y - 2 with
    // a vertical radius of 3 spans rows 41..47 of a 48-row cell. Drawing it any
    // lower wrote pixels past the bottom of this cell — they vanished from the
    // sprite (a shadow with a hard flat edge) and landed loose in the atlas, where
    // a neighbouring cell could sample them.
    fill_shadow(canvas, cx, feet_y - 2, 11, 3);

    // Legs, torso, head, bottom-up.
    fill_rect(canvas, cx - 6, feet_y - 14, 4, 13, legs);
    fill_rect(canvas, cx + 2, feet_y - 14, 4, 13, legs);
    fill_rect(canvas, cx - 8, feet_y - 28, 16, 15, tunic);
    fill_rect(canvas, cx - 8, feet_y - 16, 16, 3, tunic_dark);
    fill_circle(canvas, cx, feet_y - 33, 6, skin);

    const sim::TileDelta delta = sim::direction_delta(facing);
    const iso::ScreenPos offset = iso::tile_to_screen(
        static_cast<float>(delta.dx), static_cast<float>(delta.dy), 0);
    const float length =
        std::sqrt(offset.x * offset.x + offset.y * offset.y);
    if (length > 0.0F) {
        const int mx = cx + static_cast<int>(std::lround(offset.x / length * 6.0F));
        const int my = feet_y - 33 +
                       static_cast<int>(std::lround(offset.y / length * 6.0F));
        fill_circle(canvas, mx, my, 2, marker);
    }
}

AtlasEntry make_entry(int x, int y, int w, int h, float origin_x,
                      float origin_y) {
    const float atlas = static_cast<float>(kAtlasSize);
    AtlasEntry entry;
    entry.uv = Rect{static_cast<float>(x) / atlas, static_cast<float>(y) / atlas,
                    static_cast<float>(w) / atlas, static_cast<float>(h) / atlas};
    entry.width = static_cast<float>(w);
    entry.height = static_cast<float>(h);
    entry.origin_x = origin_x;
    entry.origin_y = origin_y;
    entry.valid = true;
    return entry;
}

/// Ground sprites cover exactly the tile diamond, so the top-left of the sprite
/// is half a tile left of the tile's top vertex.
AtlasEntry ground_entry(int slot) {
    return make_entry(slot * iso::kTileWidth, kGroundRowY, iso::kTileWidth,
                      iso::kTileHeight,
                      -static_cast<float>(iso::kHalfTileWidth), 0.0F);
}

/// A 64x64 block's base diamond is its bottom 32 rows, so it is lifted by 32.
AtlasEntry block_entry(int slot) {
    return make_entry(slot * iso::kTileWidth, kBlockRowY, iso::kTileWidth, 64,
                      -static_cast<float>(iso::kHalfTileWidth),
                      -static_cast<float>(iso::kTileHeight));
}

}  // namespace

Tileset Tileset::build_procedural(Renderer2D& renderer) {
    Canvas canvas(kAtlasSize, kAtlasSize);

    const Rgba grass{86, 148, 74, 255};
    const Rgba dirt{146, 116, 82, 255};
    const Rgba stone{136, 136, 146, 255};
    const Rgba water{62, 108, 178, 255};

    const int w = iso::kTileWidth;
    const int h = iso::kTileHeight;

    fill_diamond(canvas, 0 * w, kGroundRowY, w, h, grass, shade(grass, 0.85F));
    speckle_diamond(canvas, 0 * w, kGroundRowY, w, h, shade(grass, 1.18F), 11, 60);

    fill_diamond(canvas, 1 * w, kGroundRowY, w, h, dirt, shade(dirt, 0.85F));
    speckle_diamond(canvas, 1 * w, kGroundRowY, w, h, shade(dirt, 1.15F), 22, 45);

    fill_diamond(canvas, 2 * w, kGroundRowY, w, h, stone, shade(stone, 0.82F));
    speckle_diamond(canvas, 2 * w, kGroundRowY, w, h, shade(stone, 1.12F), 33, 35);

    fill_diamond(canvas, 3 * w, kGroundRowY, w, h, water, shade(water, 0.8F));
    speckle_diamond(canvas, 3 * w, kGroundRowY, w, h, shade(water, 1.3F), 44, 25);

    draw_diamond_outline(canvas, 0, kHighlightY, w, h,
                         Rgba{255, 240, 160, 220});

    // Wall: cool grey block. Tree: brown trunk with a green canopy, drawn inside
    // the same 64x64 cell so both share the block origin.
    fill_block(canvas, 0 * w, kBlockRowY, Rgba{170, 170, 180, 255},
               Rgba{104, 104, 116, 255}, Rgba{132, 132, 144, 255}, 32);

    const int tree_x = 1 * w;
    fill_rect(canvas, tree_x + 29, kBlockRowY + 30, 6, 18,
              Rgba{104, 74, 48, 255});
    fill_circle(canvas, tree_x + 32, kBlockRowY + 24, 14, Rgba{54, 118, 60, 255});
    fill_circle(canvas, tree_x + 23, kBlockRowY + 30, 9, Rgba{44, 100, 52, 255});
    fill_circle(canvas, tree_x + 41, kBlockRowY + 30, 9, Rgba{66, 132, 68, 255});
    fill_circle(canvas, tree_x + 30, kBlockRowY + 19, 7, Rgba{74, 142, 76, 255});

    for (int i = 0; i < 8; ++i) {
        draw_actor_frame(canvas, i * kActorFrameW, kActorRowY,
                         static_cast<sim::Direction>(i));
    }

    // Solid white swatch for tinted UI fills (see AtlasEntry solid()).
    fill_rect(canvas, 232, 40, 8, 8, Rgba{255, 255, 255, 255});

    // Loot bag: small brown pouch with a drawstring (16x16 at 240,240).
    constexpr int bag_x = 240;
    constexpr int bag_y = 240;
    fill_rect(canvas, bag_x + 3, bag_y + 5, 10, 9, Rgba{148, 106, 58, 255});
    fill_rect(canvas, bag_x + 4, bag_y + 6, 8, 7, Rgba{176, 128, 70, 255});
    fill_rect(canvas, bag_x + 5, bag_y + 3, 6, 3, Rgba{120, 84, 44, 255});
    fill_rect(canvas, bag_x + 7, bag_y + 1, 2, 3, Rgba{214, 170, 60, 255});

    Tileset tileset;
    tileset.texture_ =
        renderer.create_texture(canvas.data(), canvas.width(), canvas.height());
    tileset.atlas_width_ = canvas.width();
    tileset.atlas_height_ = canvas.height();

    tileset.ground_[sim::tiles::kGrass] = ground_entry(0);
    tileset.ground_[sim::tiles::kDirt]  = ground_entry(1);
    tileset.ground_[sim::tiles::kStone] = ground_entry(2);
    tileset.ground_[sim::tiles::kWater] = ground_entry(3);

    tileset.object_[sim::tiles::kWall] = block_entry(0);
    tileset.object_[sim::tiles::kTree] = block_entry(1);

    for (int i = 0; i < 8; ++i) {
        // Feet land on the tile centre, which is half a tile below its top vertex.
        tileset.actor_frames_[static_cast<std::size_t>(i)] = make_entry(
            i * kActorFrameW, kActorRowY, kActorFrameW, kActorFrameH,
            -static_cast<float>(kActorFrameW) * 0.5F,
            static_cast<float>(iso::kHalfTileHeight - kActorFrameH));
    }

    tileset.highlight_ =
        make_entry(0, kHighlightY, w, h,
                   -static_cast<float>(iso::kHalfTileWidth), 0.0F);

    tileset.solid_ = make_entry(234, 42, 4, 4, 0.0F, 0.0F);
    tileset.bag_ = make_entry(240, 240, 16, 16, 0.0F, 0.0F);

    return tileset;
}

namespace {

/// Decodes an in-memory PNG (or any format SDL_image is built with) into a
/// tightly packed RGBA byte buffer, which is what Renderer2D::create_texture
/// wants everywhere. Row-copies because the decoded surface's pitch is not
/// guaranteed to equal width*4.
bool decode_atlas_rgba(const std::vector<std::uint8_t>& file_bytes,
                       std::vector<std::uint8_t>& out_pixels, int& out_w,
                       int& out_h) {
    SDL_IOStream* io = SDL_IOFromConstMem(file_bytes.data(), file_bytes.size());
    if (io == nullptr) {
        return false;
    }
    // closeio == true: IMG_Load_IO takes ownership of the stream either way.
    SDL_Surface* decoded = IMG_Load_IO(io, true);
    if (decoded == nullptr) {
        LOG_WARN("IMG_Load_IO failed: %s", SDL_GetError());
        return false;
    }

    SDL_Surface* rgba = SDL_ConvertSurface(decoded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(decoded);
    if (rgba == nullptr) {
        LOG_WARN("SDL_ConvertSurface failed: %s", SDL_GetError());
        return false;
    }

    out_w = rgba->w;
    out_h = rgba->h;
    const auto width = static_cast<std::size_t>(out_w);
    const auto height = static_cast<std::size_t>(out_h);
    out_pixels.resize(width * height * 4U);

    const auto* src = static_cast<const std::uint8_t*>(rgba->pixels);
    const auto pitch = static_cast<std::size_t>(rgba->pitch);
    for (std::size_t row = 0; row < height; ++row) {
        std::memcpy(out_pixels.data() + row * width * 4U, src + row * pitch,
                    width * 4U);
    }

    SDL_DestroySurface(rgba);
    return true;
}

AtlasEntry entry_from_pixels(int atlas_w, int atlas_h, int x, int y, int w,
                             int h, float origin_x, float origin_y) {
    // Half-texel inset. A region's edge in normalised uv is only exact when the
    // atlas dimension is a power of two; at 256x440 the bottom edge of the ground
    // row lands a hair past texel 31 and NEAREST sampling picks up row 32 — which
    // is the gold cursor band, so every floor tile grew a yellow speck at its
    // bottom tip. Biasing to texel centres samples the region and nothing else,
    // whatever the atlas measures.
    const float half_u = 0.5F / static_cast<float>(atlas_w);
    const float half_v = 0.5F / static_cast<float>(atlas_h);

    AtlasEntry entry;
    entry.uv = Rect{static_cast<float>(x) / static_cast<float>(atlas_w) + half_u,
                    static_cast<float>(y) / static_cast<float>(atlas_h) + half_v,
                    static_cast<float>(w) / static_cast<float>(atlas_w) - 2.0F * half_u,
                    static_cast<float>(h) / static_cast<float>(atlas_h) - 2.0F * half_v};
    entry.width = static_cast<float>(w);
    entry.height = static_cast<float>(h);
    entry.origin_x = origin_x;
    entry.origin_y = origin_y;
    entry.valid = true;
    return entry;
}

}  // namespace

bool Tileset::parse_atlas_meta(const std::string& text, int atlas_w,
                               int atlas_h, Tileset& out) {
    // Line format, whitespace-separated, '#' starts a comment:
    //   ground|object <id>    <x> <y> <w> <h> <origin_x> <origin_y>
    //   actor          <dir>  <x> <y> <w> <h> <origin_x> <origin_y>   (dir 0..7)
    //   highlight              <x> <y> <w> <h> <origin_x> <origin_y>
    // The id is the sim TileId; that binding is the whole point of the file.
    out.atlas_width_ = atlas_w;
    out.atlas_height_ = atlas_h;

    std::istringstream stream(text);
    std::string line;
    int bound = 0;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream fields(line);
        std::string kind;
        fields >> kind;

        int x = 0, y = 0, w = 0, h = 0;
        float ox = 0.0F, oy = 0.0F;

        if (kind == "highlight") {
            if (!(fields >> x >> y >> w >> h >> ox >> oy)) {
                return false;
            }
            out.highlight_ = entry_from_pixels(atlas_w, atlas_h, x, y, w, h, ox, oy);
            ++bound;
        } else if (kind == "bag") {
            // Floor loot-bag for phantom corpses. No origin: drawn like an item icon.
            if (!(fields >> x >> y >> w >> h)) {
                return false;
            }
            out.bag_ = entry_from_pixels(atlas_w, atlas_h, x, y, w, h, 0.0F, 0.0F);
            ++bound;
        } else if (kind == "solid") {
            if (!(fields >> x >> y >> w >> h)) {
                return false;
            }
            out.solid_ = entry_from_pixels(atlas_w, atlas_h, x, y, w, h, 0.0F, 0.0F);
            ++bound;
        } else if (kind == "item" || kind == "effect") {
            int id = 0;
            if (!(fields >> id >> x >> y >> w >> h)) {
                return false;
            }
            const AtlasEntry entry =
                entry_from_pixels(atlas_w, atlas_h, x, y, w, h, 0.0F, 0.0F);
            if (kind == "item") {
                out.icons_[static_cast<sim::TileId>(id)] = entry;
            } else {
                out.effects_[static_cast<std::uint8_t>(id)] = entry;
            }
            ++bound;
        } else if (kind == "ground" || kind == "object") {
            int id = 0;
            if (!(fields >> id >> x >> y >> w >> h >> ox >> oy)) {
                return false;
            }
            const AtlasEntry entry =
                entry_from_pixels(atlas_w, atlas_h, x, y, w, h, ox, oy);
            if (kind == "ground") {
                out.ground_[static_cast<sim::TileId>(id)] = entry;
            } else {
                out.object_[static_cast<sim::TileId>(id)] = entry;
            }
            ++bound;
        } else if (kind == "actor") {
            int dir = 0;
            if (!(fields >> dir >> x >> y >> w >> h >> ox >> oy)) {
                return false;
            }
            if (dir >= 0 && dir < static_cast<int>(out.actor_frames_.size())) {
                out.actor_frames_[static_cast<std::size_t>(dir)] =
                    entry_from_pixels(atlas_w, atlas_h, x, y, w, h, ox, oy);
                ++bound;
            }
        } else if (kind == "mob") {
            // One appearance's worth of directions, one line each:
            //   mob <appearance> <dir> <x> <y> <w> <h> <origin_x> <origin_y>
            // A separate kind from `actor` rather than an extra column on it, so
            // every atlas.txt written before monsters existed still parses.
            int appearance = 0;
            int dir = 0;
            if (!(fields >> appearance >> dir >> x >> y >> w >> h >> ox >> oy)) {
                return false;
            }
            if (dir >= 0 && dir < 8 && appearance > 0) {
                auto& set = out.mob_frames_[static_cast<std::uint16_t>(appearance)];
                set.dirs = anim::kArtDirsFull;
                set.frames = 1;
                set.entry[static_cast<std::size_t>(dir)][0] =
                    entry_from_pixels(atlas_w, atlas_h, x, y, w, h, ox, oy);
                ++bound;
            }
        } else if (kind == "mobstrip") {
            // A whole animated set on one line:
            //   mobstrip <appearance> <x> <y> <cell_w> <cell_h> <dirs> <frames>
            //            <origin_x> <origin_y>
            // Cells run left to right from (x, y) in one atlas row, direction-major:
            // index = dir * frames + frame. One line instead of dirs*frames of them,
            // because that is how the source art is packed and because a 4x3 set
            // written out longhand is twelve lines nobody can proofread.
            int appearance = 0;
            int dirs = 0;
            int frames = 0;
            if (!(fields >> appearance >> x >> y >> w >> h >> dirs >> frames >> ox >>
                  oy)) {
                return false;
            }
            // Tilt is optional and last, in DEGREES, so every mobstrip line written
            // before leaning existed still parses and reads as upright.
            float tilt_degrees = 0.0F;
            if (!(fields >> tilt_degrees)) {
                tilt_degrees = 0.0F;
            }
            const bool sane = appearance > 0 && w > 0 && h > 0 && frames >= 1 &&
                              frames <= anim::kMaxFrames &&
                              (dirs == anim::kArtDirsTibia ||
                               dirs == anim::kArtDirsFull);
            if (sane) {
                auto& set = out.mob_frames_[static_cast<std::uint16_t>(appearance)];
                set = MobSprites{};
                set.dirs = static_cast<std::uint8_t>(dirs);
                set.frames = static_cast<std::uint8_t>(frames);
                set.tilt = tilt_degrees * 3.14159265F / 180.0F;
                for (int dir = 0; dir < dirs; ++dir) {
                    for (int frame = 0; frame < frames; ++frame) {
                        const int cell = dir * frames + frame;
                        set.entry[static_cast<std::size_t>(dir)]
                                 [static_cast<std::size_t>(frame)] =
                            entry_from_pixels(atlas_w, atlas_h, x + cell * w, y, w,
                                              h, ox, oy);
                    }
                }
                ++bound;
            }
        } else if (kind == "font") {
            // One line describes the whole glyph grid: cells run in ASCII order
            // from `first`, `per_row` of them, then wrap to the next band.
            // Deriving every rect here is what keeps the layout out of both the
            // renderer and the callers.
            int first = 0, count = 0, per_row = 0;
            if (!(fields >> first >> count >> x >> y >> w >> h >> per_row)) {
                return false;
            }
            if (count <= 0 || per_row <= 0 || w <= 0 || h <= 0) {
                return false;
            }
            out.glyph_first_   = first;
            out.glyph_advance_ = static_cast<float>(w);
            out.glyph_height_  = static_cast<float>(h);
            out.glyphs_.assign(static_cast<std::size_t>(count), AtlasEntry{});
            for (int i = 0; i < count; ++i) {
                const int gx = x + (i % per_row) * w;
                const int gy = y + (i / per_row) * h;
                out.glyphs_[static_cast<std::size_t>(i)] = entry_from_pixels(
                    atlas_w, atlas_h, gx, gy, w, h, 0.0F, 0.0F);
            }
            ++bound;
        }
        // Unknown kinds are skipped so a newer atlas.txt does not break an older
        // client outright.
    }

    return bound > 0;
}

Tileset Tileset::load(Renderer2D& renderer) {
    std::vector<std::uint8_t> file_bytes;
    std::string meta;
    if (platform::vfs::read_asset("tilesets/atlas.png", file_bytes) &&
        platform::vfs::read_asset_text("tilesets/atlas.txt", meta)) {
        std::vector<std::uint8_t> pixels;
        int w = 0;
        int h = 0;
        if (decode_atlas_rgba(file_bytes, pixels, w, h)) {
            Tileset tileset;
            tileset.texture_ = renderer.create_texture(pixels.data(), w, h);
            if (tileset.texture_.valid() &&
                parse_atlas_meta(meta, w, h, tileset)) {
                LOG_INFO("loaded sprite atlas from tilesets/atlas.png (%dx%d)", w,
                         h);
                return tileset;
            }
        }
        LOG_WARN("tilesets/atlas.png present but failed to load; "
                 "falling back to procedural art");
    } else {
        LOG_INFO("no tilesets/atlas.png found; using procedural art");
    }

    return build_procedural(renderer);
}

const AtlasEntry& Tileset::ground(sim::TileId id) const {
    const auto it = ground_.find(id);
    return it == ground_.end() ? invalid_ : it->second;
}

const AtlasEntry& Tileset::object(sim::TileId id) const {
    const auto it = object_.find(id);
    return it == object_.end() ? invalid_ : it->second;
}

const AtlasEntry& Tileset::icon(sim::TileId id) const {
    const auto it = icons_.find(id);
    return it == icons_.end() ? invalid_ : it->second;
}

const AtlasEntry& Tileset::effect(std::uint8_t id) const {
    const auto it = effects_.find(id);
    return it == effects_.end() ? invalid_ : it->second;
}

const AtlasEntry& Tileset::glyph(char c) const {
    // Widened through unsigned char on purpose: a signed char would wrap on any
    // byte >= 0x80 from a UTF-8 string, and a negative index is worse than a miss.
    const auto code  = static_cast<int>(static_cast<unsigned char>(c));
    const auto index = static_cast<std::size_t>(code - glyph_first_);
    if (code < glyph_first_ || index >= glyphs_.size()) {
        return invalid_;
    }
    return glyphs_[index];
}

const MobSprites* Tileset::mob_sprites(std::uint16_t appearance) const {
    const auto found = mob_frames_.find(appearance);
    return found == mob_frames_.end() ? nullptr : &found->second;
}

float Tileset::tilt(std::uint16_t appearance) const {
    const MobSprites* set = appearance == 0 ? nullptr : mob_sprites(appearance);
    return set == nullptr ? 0.0F : set->tilt;
}

std::uint8_t Tileset::frame_count(std::uint16_t appearance) const {
    const MobSprites* set = appearance == 0 ? nullptr : mob_sprites(appearance);
    return set == nullptr ? 1U : set->frames;
}

const AtlasEntry& Tileset::actor(sim::Direction facing, std::uint16_t appearance,
                                 std::uint8_t frame) const {
    if (appearance != 0) {
        const MobSprites* set = mob_sprites(appearance);
        // Falls through to the player frames when the atlas has no art for this
        // appearance: a mob drawn as a knight is a bug you can see, and an
        // invisible mob that still hits you is one you cannot.
        if (set != nullptr) {
            const auto dir = static_cast<std::size_t>(
                anim::art_direction(facing, set->dirs));
            // A frame past the end of the cycle draws the first one rather than
            // nothing: a set edited down from 3 frames to 2 while the game is
            // running would otherwise make the mob blink out for a third of a step.
            const std::size_t f = frame < set->frames ? frame : 0U;
            if (dir < set->entry.size() && set->entry[dir][f].valid) {
                return set->entry[dir][f];
            }
        }
    }
    auto index = static_cast<std::size_t>(facing);
    if (index >= actor_frames_.size()) {
        index = 0;
    }
    return actor_frames_[index];
}

}  // namespace client
