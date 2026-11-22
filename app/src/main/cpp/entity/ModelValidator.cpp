#include "ModelValidator.h"

#include <cmath>
#include <functional>
#include <unordered_set>

namespace minefade {
namespace {
bool Finite(const ModelVec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool Finite(const ModelQuaternion& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) &&
           std::isfinite(q.z) && std::isfinite(q.w);
}
}

bool ModelValidator::Validate(const ModelData& model, std::string& error) {
    error.clear();
    if (model.bones.empty() || model.bones.size() > 128u) {
        error = "model bone count must be 1..128"; return false;
    }
    if (model.cubes.empty() || model.cubes.size() > 4096u) {
        error = "model cube count must be 1..4096"; return false;
    }
    if (model.materials.empty() || model.materials.size() > 64u) {
        error = "model material count must be 1..64"; return false;
    }
    if (model.locators.size() > 128u || model.clips.size() > 64u ||
        model.tracks.size() > 512u || model.states.size() > 32u) {
        error = "model section count exceeds validated limits"; return false;
    }
    if (!Finite(model.boundsMin) || !Finite(model.boundsMax) ||
        model.boundsMin.x > model.boundsMax.x ||
        model.boundsMin.y > model.boundsMax.y ||
        model.boundsMin.z > model.boundsMax.z) {
        error = "model bounds are invalid"; return false;
    }

    std::unordered_set<std::string> boneNames;
    std::vector<std::uint8_t> visit(model.bones.size(), 0u);
    for (std::size_t i = 0; i < model.bones.size(); ++i) {
        const ModelBone& bone = model.bones[i];
        if (bone.name.empty() || !boneNames.insert(bone.name).second ||
            !Finite(bone.pivot) || !Finite(bone.bindRotationDegrees)) {
            error = "bone has an empty/duplicate name or non-finite transform"; return false;
        }
        if (bone.parentIndex != 0xFFFFFFFFu &&
            (bone.parentIndex >= model.bones.size() || bone.parentIndex == i ||
             bone.parentIndex > i)) {
            error = "bone parent must be a valid earlier topological entry"; return false;
        }
    }
    std::function<bool(std::size_t)> visitBone = [&](std::size_t index) {
        if (visit[index] == 1u) return false;
        if (visit[index] == 2u) return true;
        visit[index] = 1u;
        const std::uint32_t parent = model.bones[index].parentIndex;
        if (parent != 0xFFFFFFFFu && !visitBone(static_cast<std::size_t>(parent))) return false;
        visit[index] = 2u;
        return true;
    };
    for (std::size_t i = 0; i < model.bones.size(); ++i) {
        if (!visitBone(i)) { error = "bone hierarchy contains a cycle"; return false; }
    }

    for (const ModelCube& cube : model.cubes) {
        if (cube.boneIndex >= model.bones.size() || cube.materialIndex >= model.materials.size() ||
            !Finite(cube.origin) || !Finite(cube.size) || !Finite(cube.pivot) ||
            !Finite(cube.rotationDegrees) || cube.size.x <= 0.0f ||
            cube.size.y <= 0.0f || cube.size.z <= 0.0f) {
            error = "cube has an invalid bone/material reference or dimensions"; return false;
        }
        for (const auto& uv : cube.faceUv) {
            for (float value : uv) {
                if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
                    error = "cube UV is outside normalized texture coordinates"; return false;
                }
            }
            if (uv[0] > uv[2] || uv[1] > uv[3]) {
                error = "cube UV rectangle has negative area"; return false;
            }
        }
    }
    for (const ModelMaterial& material : model.materials) {
        if (material.name.empty() || material.texturePath.empty()) {
            error = "material is missing its name or texture path"; return false;
        }
        for (float value : material.baseColor) {
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
                error = "material base colour is invalid"; return false;
            }
        }
    }
    for (const ModelLocator& locator : model.locators) {
        if (locator.name.empty() || locator.boneIndex >= model.bones.size() || !Finite(locator.position)) {
            error = "locator has an invalid bone reference or position"; return false;
        }
    }
    for (std::size_t clipIndex = 0; clipIndex < model.clips.size(); ++clipIndex) {
        const ModelAnimationClip& clip = model.clips[clipIndex];
        if (clip.name.empty() || !std::isfinite(clip.duration) || clip.duration <= 0.0f ||
            static_cast<std::size_t>(clip.firstTrack) + clip.trackCount > model.tracks.size()) {
            error = "animation clip has invalid duration or track range"; return false;
        }
    }
    for (const ModelTrack& track : model.tracks) {
        if (track.clipIndex >= model.clips.size() || track.boneIndex >= model.bones.size() ||
            track.keyframes.empty() || track.keyframes.size() > 256u) {
            error = "animation track has an invalid reference or keyframe count"; return false;
        }
        float previousTime = -1.0f;
        for (const ModelKeyframe& key : track.keyframes) {
            if (!std::isfinite(key.time) || key.time < 0.0f || key.time < previousTime ||
                !Finite(key.translation) || !Finite(key.rotation) ||
                !Finite(key.scale) || key.scale.x <= 0.0f ||
                key.scale.y <= 0.0f || key.scale.z <= 0.0f) {
                error = "animation contains invalid or unordered keyframes"; return false;
            }
            previousTime = key.time;
        }
    }
    for (const ModelAnimationState& state : model.states) {
        if (state.name.empty() || state.clipIndex >= model.clips.size() ||
            !std::isfinite(state.blendDuration) || state.blendDuration < 0.0f ||
            state.blendDuration > 2.0f) {
            error = "controller state has invalid clip or blend duration"; return false;
        }
    }
    return true;
}

} // namespace minefade
