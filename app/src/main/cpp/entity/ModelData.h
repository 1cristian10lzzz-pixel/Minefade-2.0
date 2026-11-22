#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace minefade {

struct ModelVec3 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
};

struct ModelQuaternion {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};
};

struct ModelBone {
    std::string name;
    std::uint32_t parentIndex{0xFFFFFFFFu};
    ModelVec3 pivot{};
    ModelVec3 bindRotationDegrees{};
};

struct ModelCube {
    std::uint32_t boneIndex{0};
    std::uint32_t materialIndex{0};
    ModelVec3 origin{};
    ModelVec3 size{};
    ModelVec3 pivot{};
    ModelVec3 rotationDegrees{};
    std::array<std::array<float, 4>, 6> faceUv{};
    std::array<std::uint8_t, 24> faceColors{};
};

struct ModelMaterial {
    std::string name;
    std::string texturePath;
    std::uint32_t flags{0};
    std::array<float, 4> baseColor{{1.0f, 1.0f, 1.0f, 1.0f}};
};

struct ModelLocator {
    std::string name;
    std::uint32_t boneIndex{0};
    ModelVec3 position{};
};

struct ModelKeyframe {
    float time{0.0f};
    ModelVec3 translation{};
    ModelQuaternion rotation{};
    ModelVec3 scale{1.0f, 1.0f, 1.0f};
};

struct ModelTrack {
    std::uint32_t clipIndex{0};
    std::uint32_t boneIndex{0};
    std::vector<ModelKeyframe> keyframes;
};

struct ModelAnimationClip {
    std::string name;
    bool loop{false};
    float duration{0.0f};
    std::uint32_t firstTrack{0};
    std::uint32_t trackCount{0};
};

enum class AnimationCondition : std::uint32_t {
    Idle = 0,
    Moving = 1,
    Eating = 2
};

struct ModelAnimationState {
    std::string name;
    std::uint32_t clipIndex{0};
    AnimationCondition condition{AnimationCondition::Idle};
    float blendDuration{0.15f};
};

struct ModelData {
    std::uint16_t versionMajor{0};
    std::uint16_t versionMinor{0};
    ModelVec3 boundsMin{};
    ModelVec3 boundsMax{};
    std::vector<ModelBone> bones;
    std::vector<ModelCube> cubes;
    std::vector<ModelMaterial> materials;
    std::vector<ModelLocator> locators;
    std::vector<ModelAnimationClip> clips;
    std::vector<ModelTrack> tracks;
    std::vector<ModelAnimationState> states;
};

} // namespace minefade
