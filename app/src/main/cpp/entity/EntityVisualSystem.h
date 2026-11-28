#pragma once

#include "EntityDefinition.h"
#include "LegacyModelAdapter.h"
#include "ModelData.h"

#include "../world/VoxelWorld.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace minefade {

struct Entity;

class EntityVisualSystem {
public:
    bool LoadSheepModel(const std::uint8_t* bytes, std::size_t size, std::string& error);
    bool LoadModEntityModel(int kind, const EntityDefinition& definition,
                            const std::uint8_t* bytes, std::size_t size,
                            std::string& error);
    void Clear();
    void SetAnimationOptions(std::uint8_t quality, bool enabled);
    bool IsSheepReady() const { return sheepValidated_; }
    bool IsModEntityReady(int kind) const { return modModels_.find(kind) != modModels_.end(); }
    const std::string& SheepDiagnostic() const { return sheepDiagnostic_; }

    void AppendMesh(const std::vector<Entity>& entities,
                    std::vector<WorldVertex>& output,
                    const CameraFrustum* frustum,
                    bool textureAvailable) const;

private:
    ModelData sheepModel_;
    EntityDefinition sheepDefinition_;
    EntityRegistry registry_;
    LegacyModelAdapter legacyAdapter_{LegacyModelMode::Auto};
    std::map<int, ModelData> modModels_;
    bool sheepValidated_{false};
    std::uint8_t animationQuality_{2};
    bool animationsEnabled_{true};
    std::string sheepDiagnostic_;
};

} // namespace minefade
