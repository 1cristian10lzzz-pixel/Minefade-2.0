#pragma once

#include "ModelData.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace minefade {

class ModelLoader {
public:
    static bool Load(const std::uint8_t* bytes, std::size_t size,
                     ModelData& output, std::string& error);
};

} // namespace minefade
