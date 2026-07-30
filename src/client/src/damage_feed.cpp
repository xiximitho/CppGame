#include "client/damage_feed.hpp"

#include <string>

#include "client/iso.hpp"
#include "sim/snapshot.hpp"

namespace client {
namespace {

// Seven-segment masks, bit 0..6 = segments a,b,c,d,e,f,g:
//   aaa
//  f   b
//   ggg
//  e   c
//   ddd
constexpr std::uint8_t kSegments[10] = {
    0x3F,  // 0: a b c d e f
    0x06,  // 1: b c
    0x5B,  // 2: a b d e g
    0x4F,  // 3: a b c d g
    0x66,  // 4: b c f g
    0x6D,  // 5: a c d f g
    0x7D,  // 6: a c d e f g
    0x07,  // 7: a b c
    0x7F,  // 8: all
    0x6F,  // 9: a b c d f g
};

constexpr float kDigitW = 5.0F;
constexpr float kDigitH = 9.0F;
constexpr float kThick  = 1.6F;
constexpr float kGap    = 2.0F;

void fill(Renderer2D& renderer, TextureHandle texture, Rect uv, float x, float y,
          float w, float h, float depth, Color tint) {
    SpriteCmd sprite;
    sprite.texture = texture;
    sprite.uv = uv;
    sprite.dst = Rect{x, y, w, h};
    sprite.depth = depth;
    sprite.tint = tint;
    renderer.submit(sprite);
}

void draw_digit(Renderer2D& renderer, TextureHandle texture, Rect uv, int digit,
                float x, float y, float depth, Color tint) {
    const std::uint8_t seg = kSegments[digit];
    const float mid = y + (kDigitH - kThick) * 0.5F;
    const float bottom = y + kDigitH - kThick;
    const float half = (kDigitH - kThick) * 0.5F + kThick;
    const auto on = [seg](int bit) { return ((seg >> bit) & 1U) != 0U; };

    if (on(0)) fill(renderer, texture, uv, x, y, kDigitW, kThick, depth, tint);       // a
    if (on(6)) fill(renderer, texture, uv, x, mid, kDigitW, kThick, depth, tint);     // g
    if (on(3)) fill(renderer, texture, uv, x, bottom, kDigitW, kThick, depth, tint);  // d
    if (on(5)) fill(renderer, texture, uv, x, y, kThick, half, depth, tint);          // f
    if (on(1)) fill(renderer, texture, uv, x + kDigitW - kThick, y, kThick, half, depth, tint);   // b
    if (on(4)) fill(renderer, texture, uv, x, mid, kThick, half, depth, tint);        // e
    if (on(2)) fill(renderer, texture, uv, x + kDigitW - kThick, mid, kThick, half, depth, tint); // c
}

void draw_number(Renderer2D& renderer, const Tileset& tileset, std::int32_t value,
                 float center_x, float top_y, float depth, Color tint) {
    const AtlasEntry& solid = tileset.solid();
    if (!solid.valid) {
        return;
    }
    const std::string text = std::to_string(value);
    const float width =
        static_cast<float>(text.size()) * (kDigitW + kGap) - kGap;
    float x = center_x - width * 0.5F;
    for (const char ch : text) {
        draw_digit(renderer, tileset.texture(), solid.uv, ch - '0', x, top_y,
                   depth, tint);
        x += kDigitW + kGap;
    }
}

}  // namespace

void DamageFeed::observe(const WorldView& view, float now_seconds) {
    for (const sim::ActorState& actor : view.actors) {
        const auto it = last_hp_.find(actor.net_id);
        if (it != last_hp_.end() && actor.hp < it->second) {
            const std::int32_t amount = it->second - actor.hp;
            const sim::InterpolatedPos pos = sim::interpolate(actor);
            const iso::ScreenPos apex =
                iso::tile_to_screen(pos.x, pos.y, pos.z);
            popups_.push_back(Popup{apex.x, apex.y - 46.0F, amount, now_seconds});
        }
        last_hp_[actor.net_id] = actor.hp;
    }

    // Drop actors that left the view so the table cannot grow without bound; a
    // re-entry then starts fresh and does not fire a spurious number.
    for (auto it = last_hp_.begin(); it != last_hp_.end();) {
        bool present = false;
        for (const sim::ActorState& actor : view.actors) {
            if (actor.net_id == it->first) {
                present = true;
                break;
            }
        }
        it = present ? std::next(it) : last_hp_.erase(it);
    }
}

void DamageFeed::render(Renderer2D& renderer, const Tileset& tileset,
                        float now_seconds) {
    constexpr float kLifetime = 1.1F;  // seconds
    constexpr float kRise = 26.0F;     // world-screen pixels over the lifetime
    constexpr float kDepth = 1.0e7F;   // above the world and the health bars

    std::vector<Popup> living;
    living.reserve(popups_.size());
    for (const Popup& popup : popups_) {
        const float age = now_seconds - popup.start;
        if (age < 0.0F || age >= kLifetime) {
            continue;
        }
        const float t = age / kLifetime;
        const auto alpha = static_cast<std::uint8_t>((1.0F - t) * 255.0F);
        draw_number(renderer, tileset, popup.amount, popup.x, popup.y - t * kRise,
                    kDepth, Color{232, 68, 58, alpha});
        living.push_back(popup);
    }
    popups_.swap(living);
}

}  // namespace client
