#include "client/effect_feed.hpp"

#include <algorithm>

#include "client/iso.hpp"
#include "sim/item_type.hpp"

namespace client {
namespace {

constexpr float kDepth = 9.5e6F;  // above the world, below the fixed UI (1e7)

// Body-centre screen point of a tile (the actor's chest, roughly).
void tile_anchor(const sim::TilePos& tile, float& x, float& y) {
    const iso::ScreenPos apex = iso::tile_to_screen(tile);
    x = apex.x;
    y = apex.y - 24.0F;
}

void blit(Renderer2D& renderer, const Tileset& tileset, const AtlasEntry& entry,
          float cx, float cy, float scale, Color tint) {
    if (!entry.valid) {
        return;
    }
    const float w = entry.width * scale;
    const float h = entry.height * scale;
    SpriteCmd sprite;
    sprite.texture = tileset.texture();
    sprite.uv = entry.uv;
    sprite.dst = Rect{cx - w * 0.5F, cy - h * 0.5F, w, h};
    sprite.depth = kDepth;
    sprite.tint = tint;
    renderer.submit(sprite);
}

}  // namespace

void EffectFeed::spawn(const sim::AttackEvent& event, float now_seconds) {
    Effect effect;
    tile_anchor(event.from, effect.from_x, effect.from_y);
    tile_anchor(event.to, effect.to_x, effect.to_y);
    effect.kind = event.effect;
    effect.start = now_seconds;
    effects_.push_back(effect);
}

void EffectFeed::render(Renderer2D& renderer, const Tileset& tileset,
                        float now_seconds) {
    constexpr float kLifetime = 0.35F;

    std::vector<Effect> living;
    living.reserve(effects_.size());
    for (const Effect& effect : effects_) {
        const float age = now_seconds - effect.start;
        if (age < 0.0F || age >= kLifetime) {
            continue;
        }
        const float t = std::clamp(age / kLifetime, 0.0F, 1.0F);

        if (effect.kind == sim::kEffectRangedShot) {
            // A projectile flying from attacker to target, then a small spark.
            const float x = effect.from_x + (effect.to_x - effect.from_x) * t;
            const float y = effect.from_y + (effect.to_y - effect.from_y) * t;
            blit(renderer, tileset, tileset.effect(sim::kEffectRangedShot), x, y,
                 2.0F, Color{255, 255, 255, 255});
        } else if (effect.kind == sim::kEffectFirebolt) {
            const float x = effect.from_x + (effect.to_x - effect.from_x) * t;
            const float y = effect.from_y + (effect.to_y - effect.from_y) * t;
            blit(renderer, tileset, tileset.effect(sim::kEffectRangedShot), x, y,
                 2.4F, Color{255, 140, 40, 255});
            const auto alpha = static_cast<std::uint8_t>((1.0F - t) * 200.0F);
            blit(renderer, tileset, tileset.effect(sim::kEffectMeleeGlow),
                 effect.to_x, effect.to_y, 0.5F + t * 0.5F,
                 Color{255, 90, 20, alpha});
        } else if (effect.kind == sim::kEffectNature) {
            const auto alpha = static_cast<std::uint8_t>((1.0F - t) * 255.0F);
            const float scale = 0.7F + t * 0.7F;
            blit(renderer, tileset, tileset.effect(sim::kEffectMeleeGlow),
                 effect.to_x, effect.to_y, scale, Color{80, 220, 120, alpha});
        } else {
            // Melee (or default): a glow blooming and fading on the target.
            const auto alpha = static_cast<std::uint8_t>((1.0F - t) * 255.0F);
            const float scale = 0.6F + t * 0.6F;
            blit(renderer, tileset, tileset.effect(sim::kEffectMeleeGlow),
                 effect.to_x, effect.to_y, scale, Color{255, 255, 255, alpha});
        }
        living.push_back(effect);
    }
    effects_.swap(living);
}

}  // namespace client
