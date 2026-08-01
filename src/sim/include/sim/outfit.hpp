#pragma once

#include <array>
#include <cstdint>

// Player outfit colours — same four channels as Tibia's looktype tint:
//   head = hair, body = armour/shirt, legs = pants, feet = boots.
// Indices index a fixed 16-colour palette. Real OTSP looktypes can replace the
// placeholder sprites later without changing this contract (still 4×u8).

namespace sim {

inline constexpr std::uint8_t kOutfitPaletteSize = 16;

struct OutfitColor {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    const char*  name = "";
};

/// Fixed palette shipped with the client and sim. Names are ASCII for the HUD
/// bitmap font.
inline constexpr std::array<OutfitColor, kOutfitPaletteSize> kOutfitPalette{{
    {230, 223, 208, "Ivory"},
    {201, 162, 39, "Gold"},
    {180, 90, 40, "Copper"},
    {140, 70, 50, "Brown"},
    {90, 55, 40, "Umber"},
    {60, 45, 35, "Char"},
    {40, 40, 45, "Ink"},
    {80, 100, 140, "Steel"},
    {50, 80, 160, "Blue"},
    {40, 120, 90, "Teal"},
    {50, 130, 50, "Green"},
    {180, 50, 50, "Red"},
    {160, 60, 120, "Rose"},
    {120, 80, 160, "Violet"},
    {200, 200, 210, "Silver"},
    {20, 20, 22, "Black"},
}};

inline constexpr std::uint8_t clamp_outfit_index(std::uint8_t index) {
    return index < kOutfitPaletteSize ? index : 0;
}

inline constexpr OutfitColor outfit_color(std::uint8_t index) {
    return kOutfitPalette[clamp_outfit_index(index)];
}

/// Layer order for drawing (boots under pants under armour under hair).
enum class OutfitLayer : std::uint8_t {
    Feet = 0,  ///< boots
    Legs = 1,  ///< pants
    Body = 2,  ///< armour / shirt
    Head = 3,  ///< hair
};
inline constexpr int kOutfitLayerCount = 4;

struct COutfit {
    std::uint8_t head = 3;   // brown hair
    std::uint8_t body = 7;   // steel armour
    std::uint8_t legs = 6;   // dark pants
    std::uint8_t feet = 5;   // charcoal boots

    [[nodiscard]] std::uint8_t index(OutfitLayer layer) const {
        switch (layer) {
        case OutfitLayer::Head:
            return clamp_outfit_index(head);
        case OutfitLayer::Body:
            return clamp_outfit_index(body);
        case OutfitLayer::Legs:
            return clamp_outfit_index(legs);
        case OutfitLayer::Feet:
            return clamp_outfit_index(feet);
        }
        return 0;
    }

    void set(OutfitLayer layer, std::uint8_t color_index) {
        const std::uint8_t c = clamp_outfit_index(color_index);
        switch (layer) {
        case OutfitLayer::Head:
            head = c;
            break;
        case OutfitLayer::Body:
            body = c;
            break;
        case OutfitLayer::Legs:
            legs = c;
            break;
        case OutfitLayer::Feet:
            feet = c;
            break;
        }
    }
};

/// Player-facing labels (Tibia vocabulary). Wire fields stay head/body/legs/feet.
inline constexpr const char* outfit_layer_label(OutfitLayer layer) {
    switch (layer) {
    case OutfitLayer::Head:
        return "HAIR";
    case OutfitLayer::Body:
        return "ARMOR";
    case OutfitLayer::Legs:
        return "LEGS";
    case OutfitLayer::Feet:
        return "BOOTS";
    }
    return "";
}

}  // namespace sim
