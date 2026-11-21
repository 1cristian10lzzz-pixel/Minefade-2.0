#pragma once

#include "../world/VoxelWorld.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace minefade {

class Player {
public:
    Vec3 position{8.0f, 13.0f, 8.0f}; // feet position
    float yaw{-1.5707963f};
    float pitch{-0.12f};
    float verticalVelocity{0.0f};
    float movementX{0.0f};
    float movementY{0.0f};
    bool grounded{false};
    bool crouching{false};
    float speedMultiplier{1.0f};
    float jumpMultiplier{1.0f};
    float crouchSpeedMultiplier{0.46f};
    float walkPhase{0.0f};
    float animationTime{0.0f};
    std::uint8_t animationQuality{2};
    bool animationsEnabled{true};
    std::array<int, 6> appearance{{0,0,0,0,0,0}};
    bool contactShadows{true};

    Vec3 Forward() const;
    Vec3 CameraPosition() const;
    void SetMovement(float x, float y);
    void Rotate(float deltaX, float deltaY);
    void Jump();
    void SetCrouching(bool enabled) { crouching = enabled; }
    void SetSpeedMultiplier(float multiplier) { speedMultiplier = multiplier; }
    void SetJumpMultiplier(float multiplier);
    void SetAppearance(const std::array<int, 6>& appearance);
    void SetAnimationOptions(std::uint8_t quality, bool enabled);
    void SetContactShadows(bool enabled) { contactShadows = enabled; }
    // Keeps saved player positions out of solid blocks and finds nearby open ground.
    bool ResolveSafeSpawn(const VoxelWorld& world);
    void Update(const VoxelWorld& world, float deltaSeconds);
    // Appends an animated, yaw-oriented voxel character mesh for third-person view.
    void AppendMesh(std::vector<WorldVertex>& output) const;
};

class Inventory {
public:
    static constexpr std::size_t kSlotCount = 36;
    static constexpr std::size_t kHotbarSlotCount = 9;
    std::array<std::uint32_t, kSlotCount> counts{{2016u}};
    std::uint16_t selectedSlot{0};
    std::array<std::uint16_t, 4> equippedArmor{}; // helmet, chestplate, leggings, boots

    bool EquipArmorFromSlot(std::size_t slot);
    bool UnequipArmor(std::size_t armorSlot);
    bool CanAdd(std::uint16_t itemId, std::uint16_t amount = 1) const;
    bool Add(std::uint16_t itemId, std::uint16_t amount = 1);
    bool ConsumeSelected(std::uint16_t amount = 1);
    std::uint16_t CountItem(std::uint16_t itemId) const;
    bool ConsumeItem(std::uint16_t itemId, std::uint16_t amount);
    std::uint16_t SelectedItemId() const;
};

struct Entity {
    Vec3 position;
    float phase{0.0f};
    float speed{0.5f};
    int kind{0};
    int behavior{0}; // 0 wander, 1 flee, 2 graze/eat, 3 pursue
    float behaviorTimer{0.0f};
};

struct ModEntityModelPart {
    Vec3 center;
    Vec3 halfExtents;
    std::array<float, 3> color{{0.7f, 0.7f, 0.7f}};
};

struct ModEntityDefinition {
    std::string key;
    bool hostile{false};
    float speed{0.2f};
    float spawnChance{0.1f};
    int behavior{0}; // 0 wander, 1 flee, 2 graze, 3 pursue
    std::vector<ModEntityModelPart> model;
};

struct DroppedItem {
    Vec3 position;
    Vec3 velocity;
    std::uint16_t itemId{0};
    std::uint16_t amount{1};
    float age{0.0f};
};

class DroppedItemSystem {
public:
    void Spawn(const Vec3& position, std::uint16_t itemId, std::uint16_t amount = 1);
    void SetModOptions(float pickupRadius, float lifetimeSeconds);
    // Returns the number of items picked up and writes the last picked item ID.
    std::size_t Update(const VoxelWorld& world, const Player& player, Inventory& inventory,
                       float deltaSeconds, std::uint16_t* lastPickedItemId = nullptr);
    void AppendMesh(std::vector<WorldVertex>& output, const CameraFrustum* frustum = nullptr) const;
    std::size_t Count() const { return drops_.size(); }
private:
    std::vector<DroppedItem> drops_;
    std::uint32_t spawnSequence_{0};
    float pickupRadius_{1.65f};
    float lifetimeSeconds_{300.0f};
};

class EntitySystem {
public:
    void SpawnInitial(const VoxelWorld& world);
    void SetModEntities(std::vector<ModEntityDefinition> definitions);
    void Update(const VoxelWorld& world, const Player& player, float deltaSeconds);
    void AppendMesh(std::vector<WorldVertex>& output, const CameraFrustum* frustum = nullptr) const;
    void SetContactShadows(bool enabled) { contactShadows_ = enabled; }
    void SetSpeedMultiplier(float multiplier);
    void SetModOptions(float passiveSpeed, float hostileSpeed, float fleeRadius,
                       float alertRadius, float spawnRadius, int maxEntities,
                       float ambientSpawnInterval, float ambientSpawnDistance, int passiveEntityCap);
    void SetAnimationOptions(std::uint8_t quality, bool enabled) { animationQuality_ = quality > 2 ? 2 : quality; animationsEnabled_ = enabled; }
    std::size_t Count() const { return entities_.size(); }
    bool SpawnEntity(const Vec3& nearPosition, int kind, const VoxelWorld& world);
private:
    std::vector<Entity> entities_;
    std::vector<ModEntityDefinition> modEntities_;
    bool initialized_{false};
    bool contactShadows_{true};
    std::uint8_t animationQuality_{2};
    bool animationsEnabled_{true};
    float speedMultiplier_{1.0f};
    float passiveSpeedMultiplier_{1.0f};
    float hostileSpeedMultiplier_{1.0f};
    float fleeRadius_{6.0f};
    float alertRadius_{15.0f};
    float spawnRadius_{1.8f};
    std::size_t maxEntities_{128u};
    float ambientSpawnInterval_{0.0f};
    float ambientSpawnDistance_{18.0f};
    int passiveEntityCap_{24};
    float ambientSpawnElapsed_{0.0f};
    std::uint32_t ambientSpawnSequence_{0u};
    float animationTime_{0.0f};
};

class EnvironmentSystem {
public:
    void Update(float deltaSeconds);
    std::array<float, 3> SkyColor() const;
    std::array<float, 3> SunDirection() const;
    float Daylight() const { return daylight_; }
    float TimeOfDay() const { return timeOfDay_; }
    void SetTimeOfDay(float value);
private:
    float timeOfDay_{0.25f};
    float daylight_{1.0f};
};

} // namespace minefade
