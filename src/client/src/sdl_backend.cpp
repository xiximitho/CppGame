#include "client/sdl_backend.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

#include "core/log.hpp"

namespace client {
namespace {

class SdlRenderer2D final : public Renderer2D {
public:
    explicit SdlRenderer2D(SDL_Renderer* renderer) : renderer_(renderer) {
        sprites_.reserve(4096);
        vertices_.reserve(4096 * 4);
        indices_.reserve(4096 * 6);
    }

    ~SdlRenderer2D() override {
        for (SDL_Texture* texture : textures_) {
            if (texture != nullptr) {
                SDL_DestroyTexture(texture);
            }
        }
    }

    TextureHandle create_texture(const void* pixels, int width,
                                 int height) override {
        // RGBA32 is byte-order RGBA on every platform, so the caller's packing
        // needs no endian handling.
        SDL_Texture* texture =
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STATIC, width, height);
        if (texture == nullptr) {
            LOG_ERROR("SDL_CreateTexture failed: %s", SDL_GetError());
            return TextureHandle{};
        }

        if (!SDL_UpdateTexture(texture, nullptr, pixels, width * 4)) {
            LOG_ERROR("SDL_UpdateTexture failed: %s", SDL_GetError());
            SDL_DestroyTexture(texture);
            return TextureHandle{};
        }

        // Nearest keeps pixel art crisp; linear would blur tile seams and make
        // neighbouring atlas cells bleed into each other.
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

        textures_.push_back(texture);
        // Handle 0 means "invalid", so ids are 1-based.
        return TextureHandle{static_cast<std::uint32_t>(textures_.size())};
    }

    void set_camera(float center_x, float center_y, float zoom) override {
        camera_x_ = center_x;
        camera_y_ = center_y;
        zoom_ = zoom > 0.01F ? zoom : 1.0F;
    }

    void begin_frame(Color clear) override {
        SDL_GetCurrentRenderOutputSize(renderer_, &viewport_w_, &viewport_h_);

        SDL_SetRenderDrawColor(renderer_, clear.r, clear.g, clear.b, clear.a);
        SDL_RenderClear(renderer_);

        sprites_.clear();
        draw_calls_ = 0;
    }

    void submit(const SpriteCmd& sprite) override {
        if (!sprite.texture.valid()) {
            return;
        }
        sprites_.push_back(sprite);
    }

    void end_frame() override {
        // stable_sort, not sort: sprites with an identical depth key must keep
        // submission order, otherwise co-planar decals flicker between frames.
        std::stable_sort(sprites_.begin(), sprites_.end(),
                         [](const SpriteCmd& a, const SpriteCmd& b) {
                             return a.depth < b.depth;
                         });

        const float half_w = static_cast<float>(viewport_w_) * 0.5F;
        const float half_h = static_cast<float>(viewport_h_) * 0.5F;

        std::uint32_t batch_texture = 0;
        vertices_.clear();
        indices_.clear();

        for (const SpriteCmd& sprite : sprites_) {
            if (sprite.texture.id != batch_texture) {
                flush_batch(batch_texture);
                batch_texture = sprite.texture.id;
            }

            // World-screen -> window pixels.
            const float x0 = (sprite.dst.x - camera_x_) * zoom_ + half_w;
            const float y0 = (sprite.dst.y - camera_y_) * zoom_ + half_h;
            const float x1 = x0 + sprite.dst.w * zoom_;
            const float y1 = y0 + sprite.dst.h * zoom_;

            const SDL_FColor color{static_cast<float>(sprite.tint.r) / 255.0F,
                                   static_cast<float>(sprite.tint.g) / 255.0F,
                                   static_cast<float>(sprite.tint.b) / 255.0F,
                                   static_cast<float>(sprite.tint.a) / 255.0F};

            const auto base = static_cast<int>(vertices_.size());

            vertices_.push_back(SDL_Vertex{{x0, y0}, color, {sprite.uv.x, sprite.uv.y}});
            vertices_.push_back(SDL_Vertex{
                {x1, y0}, color, {sprite.uv.x + sprite.uv.w, sprite.uv.y}});
            vertices_.push_back(SDL_Vertex{
                {x1, y1},
                color,
                {sprite.uv.x + sprite.uv.w, sprite.uv.y + sprite.uv.h}});
            vertices_.push_back(SDL_Vertex{
                {x0, y1}, color, {sprite.uv.x, sprite.uv.y + sprite.uv.h}});

            indices_.push_back(base + 0);
            indices_.push_back(base + 1);
            indices_.push_back(base + 2);
            indices_.push_back(base + 0);
            indices_.push_back(base + 2);
            indices_.push_back(base + 3);
        }

        flush_batch(batch_texture);
        SDL_RenderPresent(renderer_);
    }

    int viewport_width() const override { return viewport_w_; }
    int viewport_height() const override { return viewport_h_; }
    float camera_zoom() const override { return zoom_; }

    void window_to_world(float win_x, float win_y, float& out_x,
                         float& out_y) const override {
        out_x = (win_x - static_cast<float>(viewport_w_) * 0.5F) / zoom_ + camera_x_;
        out_y = (win_y - static_cast<float>(viewport_h_) * 0.5F) / zoom_ + camera_y_;
    }

    int last_draw_calls() const override { return draw_calls_; }

private:
    void flush_batch(std::uint32_t texture_id) {
        if (indices_.empty() || texture_id == 0) {
            vertices_.clear();
            indices_.clear();
            return;
        }

        SDL_Texture* texture = textures_[texture_id - 1];
        if (!SDL_RenderGeometry(renderer_, texture, vertices_.data(),
                                static_cast<int>(vertices_.size()),
                                indices_.data(),
                                static_cast<int>(indices_.size()))) {
            LOG_ERROR("SDL_RenderGeometry failed: %s", SDL_GetError());
        }
        ++draw_calls_;

        vertices_.clear();
        indices_.clear();
    }

    SDL_Renderer* renderer_ = nullptr;
    std::vector<SDL_Texture*> textures_;

    std::vector<SpriteCmd>  sprites_;
    std::vector<SDL_Vertex> vertices_;
    std::vector<int>        indices_;

    float camera_x_ = 0.0F;
    float camera_y_ = 0.0F;
    float zoom_ = 1.0F;
    int   viewport_w_ = 1;
    int   viewport_h_ = 1;
    int   draw_calls_ = 0;
};

}  // namespace

std::unique_ptr<Renderer2D> make_sdl_renderer(SDL_Renderer* sdl_renderer) {
    if (sdl_renderer == nullptr) {
        return nullptr;
    }
    return std::make_unique<SdlRenderer2D>(sdl_renderer);
}

}  // namespace client
