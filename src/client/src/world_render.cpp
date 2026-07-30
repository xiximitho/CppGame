#include "client/world_render.hpp"

#include <algorithm>

#include "client/iso.hpp"
#include "sim/snapshot.hpp"

namespace client {
namespace {

/// Extra tiles drawn beyond the window edge. Tall sprites (a wall is twice a
/// tile's height) have their anchor tile off screen while still being visible,
/// so culling exactly at the viewport edge would pop them in and out.
constexpr int kCullMarginTiles = 4;

const sim::ActorState* find_actor(const WorldView& view, sim::NetId id) {
    if (id == sim::kInvalidNetId) {
        return nullptr;
    }
    const auto it = std::find_if(
        view.actors.begin(), view.actors.end(),
        [id](const sim::ActorState& actor) { return actor.net_id == id; });
    return it == view.actors.end() ? nullptr : &*it;
}

struct TileRange {
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
};

/// Tiles of floor `z` that can touch the window. Derived by inverse-projecting
/// the four window corners rather than by guessing a radius, so it stays correct
/// at any zoom and window size.
TileRange visible_tiles(const Renderer2D& renderer, int z) {
    float wx0 = 0.0F;
    float wy0 = 0.0F;
    float wx1 = 0.0F;
    float wy1 = 0.0F;
    renderer.window_to_world(0.0F, 0.0F, wx0, wy0);
    renderer.window_to_world(static_cast<float>(renderer.viewport_width()),
                             static_cast<float>(renderer.viewport_height()), wx1,
                             wy1);

    const sim::TilePos corners[4] = {
        iso::screen_to_tile(wx0, wy0, z),
        iso::screen_to_tile(wx1, wy0, z),
        iso::screen_to_tile(wx0, wy1, z),
        iso::screen_to_tile(wx1, wy1, z),
    };

    TileRange range{corners[0].x, corners[0].x, corners[0].y, corners[0].y};
    for (const sim::TilePos& corner : corners) {
        range.min_x = std::min(range.min_x, static_cast<int>(corner.x));
        range.max_x = std::max(range.max_x, static_cast<int>(corner.x));
        range.min_y = std::min(range.min_y, static_cast<int>(corner.y));
        range.max_y = std::max(range.max_y, static_cast<int>(corner.y));
    }

    range.min_x -= kCullMarginTiles;
    range.min_y -= kCullMarginTiles;
    range.max_x += kCullMarginTiles;
    range.max_y += kCullMarginTiles;
    return range;
}

void submit_entry(Renderer2D& renderer, TextureHandle texture,
                  const AtlasEntry& entry, iso::ScreenPos apex, float depth,
                  Color tint) {
    SpriteCmd sprite;
    sprite.texture = texture;
    sprite.uv = entry.uv;
    sprite.dst = Rect{apex.x + entry.origin_x, apex.y + entry.origin_y,
                      entry.width, entry.height};
    sprite.depth = depth;
    sprite.tint = tint;
    renderer.submit(sprite);
}

/// A solid-colour rectangle in world-screen space, using the atlas's white texel.
/// Used for the health bars; a proper HUD would grow more of these.
void submit_rect(Renderer2D& renderer, TextureHandle texture,
                 const AtlasEntry& solid, float x, float y, float w, float h,
                 float depth, Color tint) {
    if (!solid.valid || w <= 0.0F) {
        return;
    }
    SpriteCmd sprite;
    sprite.texture = texture;
    sprite.uv = solid.uv;
    sprite.dst = Rect{x, y, w, h};
    sprite.depth = depth;
    sprite.tint = tint;
    renderer.submit(sprite);
}

/// Floors above the actor are hidden so a roof does not cover the player. The
/// exception is standing in the open, where the floor above has nothing on it and
/// showing it costs nothing but reveals bridges and overhangs.
int top_visible_floor(const WorldView& view, int actor_floor) {
    const int above = actor_floor + 1;
    if (above >= view.map.floors()) {
        return actor_floor;
    }

    const sim::ActorState* local = find_actor(view, view.local_id);
    if (local == nullptr) {
        return actor_floor;
    }

    const sim::TilePos overhead{local->tile.x, local->tile.y,
                                static_cast<std::int8_t>(above)};
    const bool covered = view.map.at(overhead).ground != sim::kTileEmpty;
    return covered ? actor_floor : above;
}

}  // namespace

int local_floor(const WorldView& view) {
    const sim::ActorState* local = find_actor(view, view.local_id);
    return local != nullptr ? local->tile.z : 0;
}

bool camera_target(const WorldView& view, float& out_x, float& out_y) {
    const sim::ActorState* local = find_actor(view, view.local_id);
    if (local == nullptr) {
        return false;
    }

    const sim::InterpolatedPos pos = sim::interpolate(*local);
    const iso::ScreenPos screen = iso::tile_to_screen(pos.x, pos.y, pos.z);

    // Centre on the tile's middle, not its top vertex, so the actor sits in the
    // middle of the window rather than half a tile high.
    out_x = screen.x;
    out_y = screen.y + static_cast<float>(iso::kHalfTileHeight);
    return true;
}

void render_world(Renderer2D& renderer, const Tileset& tileset,
                  const WorldView& view, const RenderParams& params) {
    const TextureHandle texture = tileset.texture();
    if (!texture.valid() || !view.ready) {
        return;
    }

    const int actor_floor = local_floor(view);
    const int top_floor = top_visible_floor(view, actor_floor);

    for (int z = 0; z <= top_floor; ++z) {
        const TileRange range = visible_tiles(renderer, z);

        // Floors below the actor's are dimmed, which reads as depth and keeps the
        // eye on the floor being played.
        const int depth_below = actor_floor - z;
        const auto brightness = static_cast<std::uint8_t>(
            std::max(120, 255 - depth_below * 45));
        const Color floor_tint{brightness, brightness, brightness, 255};

        for (int y = range.min_y; y <= range.max_y; ++y) {
            for (int x = range.min_x; x <= range.max_x; ++x) {
                const sim::TilePos pos{static_cast<std::int16_t>(x),
                                       static_cast<std::int16_t>(y),
                                       static_cast<std::int8_t>(z)};
                if (!view.map.in_bounds(pos)) {
                    continue;
                }
                const sim::Tile& tile = view.map.at(pos);
                if (tile.ground == sim::kTileEmpty) {
                    continue;
                }

                const iso::ScreenPos apex = iso::tile_to_screen(pos);
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);

                const AtlasEntry& ground = tileset.ground(tile.ground);
                if (ground.valid) {
                    submit_entry(renderer, texture, ground, apex,
                                 iso::depth_key(fx, fy, z, iso::Layer::Ground),
                                 floor_tint);
                }

                if (tile.object != sim::kTileEmpty) {
                    const AtlasEntry& object = tileset.object(tile.object);
                    if (object.valid) {
                        submit_entry(renderer, texture, object, apex,
                                     iso::depth_key(fx, fy, z, iso::Layer::Object),
                                     floor_tint);
                    }
                }
            }
        }

        // Hover highlight, drawn above the ground of its own tile but below any
        // object standing on it.
        if (params.hover_valid && params.hover.z == z &&
            view.map.in_bounds(params.hover)) {
            const iso::ScreenPos apex = iso::tile_to_screen(params.hover);
            submit_entry(renderer, texture, tileset.highlight(), apex,
                         iso::depth_key(static_cast<float>(params.hover.x),
                                        static_cast<float>(params.hover.y), z,
                                        iso::Layer::Ground) +
                             0.5F,
                         Color{255, 255, 255, 255});
        }

        // Loot on the floor: the item icon at the tile centre, above the ground
        // but below anything standing on it, so an actor walks over it.
        for (const GroundItemView& loot : view.ground_items) {
            if (loot.tile.z != z) {
                continue;
            }
            const AtlasEntry& icon = tileset.icon(loot.id);
            if (!icon.valid) {
                continue;
            }
            const iso::ScreenPos apex = iso::tile_to_screen(loot.tile);
            const iso::ScreenPos at{
                apex.x - icon.width * 0.5F,
                apex.y + static_cast<float>(iso::kHalfTileHeight) -
                    icon.height * 0.5F};
            submit_entry(renderer, texture, icon, at,
                         iso::depth_key(static_cast<float>(loot.tile.x),
                                        static_cast<float>(loot.tile.y), z,
                                        iso::Layer::Ground) +
                             0.6F,
                         Color{255, 255, 255, 255});
        }

        for (const sim::ActorState& actor : view.actors) {
            if (actor.tile.z != z) {
                continue;
            }
            const sim::InterpolatedPos pos = sim::interpolate(actor);
            const iso::ScreenPos apex = iso::tile_to_screen(pos.x, pos.y, pos.z);

            // The local player is tinted warm so it is findable in a crowd
            // without a nameplate system existing yet.
            const Color tint = (actor.net_id == view.local_id)
                                   ? Color{255, 244, 205, 255}
                                   : floor_tint;

            const float actor_depth =
                iso::depth_key(pos.x, pos.y, pos.z, iso::Layer::Actor);
            submit_entry(renderer, texture, tileset.actor(actor.facing), apex,
                         actor_depth, tint);

            // Health bar above the head. Drawn from the hp already carried in the
            // snapshot, so it needs nothing server-side beyond what exists.
            if (actor.max_hp > 0) {
                const float frac = std::clamp(
                    static_cast<float>(actor.hp) /
                        static_cast<float>(actor.max_hp),
                    0.0F, 1.0F);
                constexpr float bar_w = 22.0F;
                constexpr float bar_h = 3.0F;
                const float bar_x = apex.x - bar_w * 0.5F;
                const float bar_y = apex.y - 42.0F;
                submit_rect(renderer, texture, tileset.solid(), bar_x - 1.0F,
                            bar_y - 1.0F, bar_w + 2.0F, bar_h + 2.0F,
                            actor_depth + 0.5F, Color{16, 16, 20, 230});
                const auto red =
                    static_cast<std::uint8_t>((1.0F - frac) * 210.0F + 30.0F);
                const auto green =
                    static_cast<std::uint8_t>(frac * 190.0F + 40.0F);
                submit_rect(renderer, texture, tileset.solid(), bar_x, bar_y,
                            bar_w * frac, bar_h, actor_depth + 0.6F,
                            Color{red, green, 48, 255});
            }
        }
    }
}

}  // namespace client
