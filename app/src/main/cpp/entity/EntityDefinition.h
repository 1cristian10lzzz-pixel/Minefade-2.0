#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace minefade {

struct EntityDefinition {
    std::string identifier;
    std::int32_t legacyKind{-1};
    std::string modelPath;
    std::string texturePath;
    std::string animationPath;
    std::string controllerPath;
    float width{0.0f};
    float height{0.0f};
    float eyeHeight{0.0f};
    std::uint32_t renderFlags{0};
    std::uint32_t modelVersion{1};
};

class EntityRegistry {
public:
    bool Register(EntityDefinition definition, std::string& error);
    const EntityDefinition* Find(const std::string& identifier) const;
    const EntityDefinition* FindByLegacyKind(std::int32_t kind) const;
    bool Exists(const std::string& identifier) const;
    void Clear();

private:
    std::unordered_map<std::string, EntityDefinition> definitions_;
};

EntityDefinition MakeSheepEntityDefinition();

} // namespace minefade
