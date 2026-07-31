#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <vector>

#include "client/renderer2d.hpp"
#include "client/tileset.hpp"

// The "open map" overlay: which file am I editing?
//
// The editor has taken --map since it existed, but a flag only helps the person who
// already knows the file names and is willing to restart the tool to switch. Maps
// are content, and content gets browsed.
//
// Listing uses <filesystem> directly rather than platform::vfs, which cannot
// enumerate: the editor is a desktop authoring tool that writes into the source
// tree, so a real directory is the only thing it ever looks at. The client, which
// does have to read assets out of an APK, keeps taking a path.

namespace editor {

/// Map files in `dir`, file names only, sorted. Empty when the directory is
/// missing or unreadable. Only `.txt` is listed, which is what sim::parse_text_map
/// reads.
std::vector<std::string> list_map_files(const std::string& dir);

class MapBrowser {
public:
    explicit MapBrowser(const client::Tileset& tileset);

    /// Re-reads `dir` and shows the list with `current_file` preselected.
    /// `unsaved` arms the two-step confirmation, so a stray click cannot throw
    /// away an unsaved map.
    void open(const std::string& dir, const std::string& current_file,
              bool unsaved);
    void close();
    bool active() const { return active_; }

    /// True when the event was consumed and must not reach the map editor.
    bool handle_event(const SDL_Event& event, const client::Renderer2D& renderer);

    /// The chosen full path, taken exactly once.
    std::optional<std::string> take_choice();

    void draw(client::Renderer2D& renderer) const;

private:
    /// Row under a window point, if any.
    std::optional<std::size_t> row_hit(float y) const;
    void                       choose();
    void                       move_selection(int delta);

    const client::Tileset*   tileset_;
    bool                     active_ = false;
    std::string              dir_;
    std::vector<std::string> files_;
    std::size_t              selected_ = 0;
    std::size_t              scroll_ = 0;
    bool                     unsaved_ = false;
    /// Set by the first Enter when there are unsaved changes; the second confirms.
    bool                     confirmed_ = false;
    std::optional<std::string> choice_;
};

}  // namespace editor
