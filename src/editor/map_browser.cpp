#include "map_browser.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "client/ui.hpp"
#include "core/log.hpp"

namespace editor {
namespace {

// Layout in window pixels, matching the item form's palette so the two overlays
// read as the same tool.
constexpr float kPanelW = 520.0F;
constexpr float kMargin = 16.0F;
constexpr float kRowH = 22.0F;
constexpr float kTitleH = 34.0F;
constexpr float kFooterH = 46.0F;
constexpr float kTextScale = 2.0F;
constexpr float kHintScale = 1.0F;
constexpr float kRowTextScale = 1.5F;

/// Above the palette bar, which draws at kDepth + 400 and would otherwise show
/// through a modal list — the map editor draws its menu before this overlay.
constexpr float kBase = client::ui::kDepth + 1000.0F;

const client::Color kScrim{10, 11, 15, 235};
const client::Color kPanel{16, 18, 24, 250};
const client::Color kRowSelected{201, 162, 39, 255};
const client::Color kBright{236, 240, 248, 255};
const client::Color kDim{120, 126, 140, 255};
const client::Color kLabel{164, 172, 188, 255};

float panel_x(const client::Renderer2D& renderer) {
    return (static_cast<float>(renderer.viewport_width()) - kPanelW) * 0.5F;
}

float panel_h(const client::Renderer2D& renderer) {
    return static_cast<float>(renderer.viewport_height()) - 2.0F * kMargin;
}

std::size_t visible_rows(const client::Renderer2D& renderer) {
    const float body = panel_h(renderer) - kTitleH - kFooterH;
    const int rows = static_cast<int>(body / kRowH);
    return static_cast<std::size_t>(std::max(rows, 1));
}

}  // namespace

std::vector<std::string> list_map_files(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code error;
    // The non-throwing overload on purpose: "the directory is not there" is an
    // ordinary state for a fresh clone, not an exception.
    for (const auto& entry : std::filesystem::directory_iterator(dir, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".txt") {
            continue;
        }
        files.push_back(entry.path().filename().string());
    }
    if (error) {
        LOG_WARN("cannot list maps in '%s': %s", dir.c_str(),
                 error.message().c_str());
    }
    std::sort(files.begin(), files.end());
    return files;
}

MapBrowser::MapBrowser(const client::Tileset& tileset) : tileset_(&tileset) {}

void MapBrowser::open(const std::string& dir, const std::string& current_file,
                      bool unsaved) {
    dir_ = dir;
    files_ = list_map_files(dir);
    active_ = true;
    unsaved_ = unsaved;
    confirmed_ = false;
    scroll_ = 0;
    selected_ = 0;
    const auto it = std::find(files_.begin(), files_.end(), current_file);
    if (it != files_.end()) {
        selected_ = static_cast<std::size_t>(it - files_.begin());
    }
}

void MapBrowser::close() {
    active_ = false;
    confirmed_ = false;
}

std::optional<std::string> MapBrowser::take_choice() {
    std::optional<std::string> taken;
    choice_.swap(taken);
    return taken;
}

void MapBrowser::move_selection(int delta) {
    if (files_.empty()) {
        return;
    }
    const int last = static_cast<int>(files_.size()) - 1;
    const int next = std::clamp(static_cast<int>(selected_) + delta, 0, last);
    selected_ = static_cast<std::size_t>(next);
    // Moving the cursor is not a decision, so it disarms the confirmation: the
    // "enter again" prompt must always refer to the row it is shown next to.
    confirmed_ = false;
}

void MapBrowser::choose() {
    if (files_.empty()) {
        return;
    }
    if (unsaved_ && !confirmed_) {
        confirmed_ = true;
        return;
    }
    choice_ = (std::filesystem::path(dir_) / files_[selected_]).string();
    close();
}

std::optional<std::size_t> MapBrowser::row_hit(float y) const {
    const float top = kMargin + kTitleH;
    if (y < top) {
        return std::nullopt;
    }
    const auto row = static_cast<std::size_t>((y - top) / kRowH);
    const std::size_t index = scroll_ + row;
    if (index >= files_.size()) {
        return std::nullopt;
    }
    return index;
}

bool MapBrowser::handle_event(const SDL_Event& event,
                              const client::Renderer2D& renderer) {
    if (!active_) {
        return false;
    }

    switch (event.type) {
        case SDL_EVENT_KEY_DOWN: {
            const std::size_t page = visible_rows(renderer);
            switch (event.key.key) {
                case SDLK_ESCAPE:
                    close();
                    return true;
                case SDLK_UP:      move_selection(-1); return true;
                case SDLK_DOWN:    move_selection(1);  return true;
                case SDLK_PAGEUP:
                    move_selection(-static_cast<int>(page));
                    return true;
                case SDLK_PAGEDOWN:
                    move_selection(static_cast<int>(page));
                    return true;
                case SDLK_HOME:
                    move_selection(-static_cast<int>(files_.size()));
                    return true;
                case SDLK_END:
                    move_selection(static_cast<int>(files_.size()));
                    return true;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    choose();
                    return true;
                default:
                    // Every other key is swallowed: this is a modal list, and a
                    // keystroke that fell through to the map would paint behind it.
                    return true;
            }
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (event.button.button != SDL_BUTTON_LEFT) {
                return true;
            }
            const auto row = row_hit(event.button.y);
            if (!row.has_value()) {
                return true;
            }
            if (*row != selected_) {
                selected_ = *row;
                confirmed_ = false;
                return true;  // first click selects, second opens
            }
            choose();
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            const float rows = event.wheel.y > 0.0F ? -3.0F : 3.0F;
            const auto shift = static_cast<int>(rows);
            const int max_scroll = static_cast<int>(files_.size()) -
                                   static_cast<int>(visible_rows(renderer));
            const int next = std::clamp(static_cast<int>(scroll_) + shift, 0,
                                        std::max(max_scroll, 0));
            scroll_ = static_cast<std::size_t>(next);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return true;
        default:
            return false;
    }
}

void MapBrowser::draw(client::Renderer2D& renderer) const {
    if (!active_) {
        return;
    }
    const float vw = static_cast<float>(renderer.viewport_width());
    const float vh = static_cast<float>(renderer.viewport_height());
    client::ui::fill(renderer, *tileset_, 0.0F, 0.0F, vw, vh, kScrim, kBase);

    const float px = panel_x(renderer);
    const float ph = panel_h(renderer);
    client::ui::fill(renderer, *tileset_, px, kMargin, kPanelW, ph, kPanel,
                     kBase + 1.0F);
    client::ui::text(renderer, *tileset_, "open map", px + 10.0F, kMargin + 8.0F,
                     kBright, kTextScale, kBase + 4.0F);
    client::ui::text(renderer, *tileset_, dir_,
                     px + 10.0F + client::ui::text_width(*tileset_, "open map ",
                                                         kTextScale),
                     kMargin + 14.0F, kDim, kHintScale, kBase + 4.0F);

    const std::size_t rows = visible_rows(renderer);
    std::size_t first = scroll_;
    if (selected_ >= first + rows) {
        first = selected_ - rows + 1U;
    } else if (selected_ < first) {
        first = selected_;
    }

    if (files_.empty()) {
        client::ui::text(renderer, *tileset_, "no .txt maps in this directory",
                         px + 12.0F, kMargin + kTitleH, kDim, kRowTextScale,
                         kBase + 4.0F);
    }
    for (std::size_t i = 0; i < rows && first + i < files_.size(); ++i) {
        const float y = kMargin + kTitleH + static_cast<float>(i) * kRowH;
        const bool is_selected = (first + i) == selected_;
        if (is_selected) {
            client::ui::fill(renderer, *tileset_, px + 6.0F, y - 3.0F,
                             kPanelW - 12.0F, kRowH, kRowSelected, kBase + 2.0F);
        }
        client::ui::text(renderer, *tileset_, files_[first + i], px + 12.0F, y,
                         is_selected ? client::Color{20, 18, 10, 255} : kLabel,
                         kRowTextScale, kBase + 4.0F);
    }

    const float footer_y = kMargin + ph - kFooterH + 8.0F;
    if (unsaved_ && confirmed_) {
        client::ui::text(renderer, *tileset_,
                         "unsaved changes will be lost - enter again to open",
                         px + 12.0F, footer_y, kRowSelected, kRowTextScale,
                         kBase + 4.0F);
    } else if (unsaved_) {
        client::ui::text(renderer, *tileset_, "this map has unsaved changes",
                         px + 12.0F, footer_y, kRowSelected, kHintScale,
                         kBase + 4.0F);
    }
    client::ui::text(renderer, *tileset_,
                     "up/down select   enter open   click twice to open   "
                     "esc cancel",
                     px + 12.0F, footer_y + 20.0F, kDim, kHintScale, kBase + 4.0F);
}

}  // namespace editor
