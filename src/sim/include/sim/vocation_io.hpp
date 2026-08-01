#pragma once

#include <string>

#include "sim/vocation_type.hpp"

// Text format for player vocations: `assets/vocations.txt`.
// Same pure-parser pattern as monster_io.hpp.

namespace sim {

bool parse_vocation_catalogue(const std::string& text, VocationRegistry& out,
                              std::string* error = nullptr);

}  // namespace sim
