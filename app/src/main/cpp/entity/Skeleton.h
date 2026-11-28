#pragma once

#include "ModelData.h"

#include <array>
#include <vector>

namespace minefade {

using ModelMatrix = std::array<float, 16>;

class Skeleton {
public:
    static bool BuildMatrices(const ModelData& model,
                              const std::vector<ModelVec3>& translations,
                              const std::vector<ModelQuaternion>& rotations,
                              const std::vector<ModelVec3>& scales,
                              std::vector<ModelMatrix>& output);
    static ModelVec3 TransformPoint(const ModelMatrix& matrix, const ModelVec3& point);
    static ModelQuaternion FromEulerDegrees(const ModelVec3& degrees);
    static ModelQuaternion Multiply(const ModelQuaternion& a, const ModelQuaternion& b);
    static ModelQuaternion Slerp(const ModelQuaternion& a, const ModelQuaternion& b, float amount);
};

} // namespace minefade
