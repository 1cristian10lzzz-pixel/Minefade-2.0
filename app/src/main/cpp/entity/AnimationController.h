#pragma once

#include "ModelData.h"

#include <cstdint>
#include <vector>

namespace minefade {

struct EntityAnimationState {
    float time{0.0f};
    std::uint8_t condition{0}; // 0 idle, 1 moving, 2 eating
    std::uint8_t previousCondition{0};
    float transitionSeconds{0.0f};
};

struct AnimatedBonePose {
    ModelVec3 translation{};
    ModelQuaternion rotation{};
    ModelVec3 scale{1.0f, 1.0f, 1.0f};
};

class AnimationController {
public:
    static bool Evaluate(const ModelData& model, const EntityAnimationState& state,
                         std::vector<AnimatedBonePose>& output);
};

} // namespace minefade
