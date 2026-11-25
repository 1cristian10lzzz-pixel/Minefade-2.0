#pragma once

#include "ModelData.h"

#include <string>

namespace minefade {

class ModelValidator {
public:
    static bool Validate(const ModelData& model, std::string& error);
};

} // namespace minefade
