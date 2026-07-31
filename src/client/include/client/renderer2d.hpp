#pragma once

#include <cstdint>
#include <memory>

namespace client {

/// Deliberately free of SDL types.
///
/// The implementation today is SDL_Render, which is batched and portable but
/// accepts no custom shaders. The day this project needs dynamic light, palette
/// swaps, outlines or shader-driven fog of war, a second implementation on
/// SDL_GPU slots in behind this same interface and no game code moves. Keeping
/// that swap to ~10 functions is the entire point.

struct TextureHandle {
    std::uint32_t id = 0;
    bool valid() const { return id != 0; }
};

struct Rect {
    float x = 0.0F;
    float y = 0.0F;
    float w = 0.0F;
    float h = 0.0F;
};

struct Color {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

struct SpriteCmd {
    TextureHandle texture;
    /// Destination in world-screen pixels, before the camera transform.
    Rect dst;
    /// Source region in normalised atlas coordinates.
    Rect uv;
    /// Painter's-algorithm sort key; see iso::depth_key.
    float depth = 0.0F;
    Color tint;
    /// Clockwise rotation in radians, about the BOTTOM CENTRE of `dst`.
    ///
    /// That pivot, and not the middle, because the only thing a rotated sprite must
    /// keep is its contact with the ground: an actor's feet sit at the bottom centre
    /// of its cell (that is what AtlasEntry::origin is chosen for), so rotating there
    /// leans the creature and leaves it standing on its tile. Rotating about the
    /// middle slides the feet off it by half the sprite.
    ///
    /// Costs nothing: the backend already emits four free vertices per sprite through
    /// SDL_RenderGeometry, so a rotated quad is the same one draw call. Sampling stays
    /// NEAREST, so a rotated sprite has hard stair-stepped edges — which is why this
    /// is a per-sprite-set authoring knob (`mobstrip`'s tilt) and not something the
    /// renderer applies on its own.
    float rotation = 0.0F;
};

class Renderer2D {
public:
    Renderer2D() = default;
    virtual ~Renderer2D() = default;

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;
    Renderer2D(Renderer2D&&) = delete;
    Renderer2D& operator=(Renderer2D&&) = delete;

    /// `pixels` is tightly packed RGBA bytes, `width * height * 4` of them.
    virtual TextureHandle create_texture(const void* pixels, int width,
                                         int height) = 0;

    /// World-screen point that ends up at the centre of the window.
    virtual void set_camera(float center_x, float center_y, float zoom) = 0;

    virtual void begin_frame(Color clear) = 0;
    virtual void submit(const SpriteCmd& sprite) = 0;
    /// Sorts, batches and draws everything submitted this frame.
    virtual void end_frame() = 0;

    virtual int  viewport_width() const = 0;
    virtual int  viewport_height() const = 0;
    virtual float camera_zoom() const = 0;

    /// Converts a window pixel to the world-screen space that sprites and
    /// iso::screen_to_tile use. Needed for click targeting.
    virtual void window_to_world(float win_x, float win_y, float& out_x,
                                 float& out_y) const = 0;

    /// Draw calls issued by the last end_frame(). Batching health at a glance.
    virtual int last_draw_calls() const = 0;
};

}  // namespace client
