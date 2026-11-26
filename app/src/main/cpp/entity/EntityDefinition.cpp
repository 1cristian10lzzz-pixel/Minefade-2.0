#include "EntityDefinition.h"

#include <cmath>

namespace minefade {
namespace {
bool SafeResourcePath(const std::string& path) {
    if (path.empty()) return true; // Animation/controller paths are optional.
    if (path.front() == '/' || path.find("..") != std::string::npos ||
        path.find('\\') != std::string::npos) return false;
    return path.find(':') == std::string::npos;
}
}

bool EntityRegistry::Register(EntityDefinition definition, std::string& error) {
    error.clear();
    if (definition.identifier.empty() ||
        definition.identifier.find(':') == std::string::npos) {
        error = "entity identifier must use namespace:name"; return false;
    }
    if (definitions_.find(definition.identifier) != definitions_.end()) {
        error = "duplicate entity identifier: " + definition.identifier; return false;
    }
    if (!SafeResourcePath(definition.modelPath) ||
        !SafeResourcePath(definition.texturePath) ||
        !SafeResourcePath(definition.animationPath) ||
        !SafeResourcePath(definition.controllerPath)) {
        error = "entity resource path is empty or escapes the assets directory"; return false;
    }
    if (!std::isfinite(definition.width) || !std::isfinite(definition.height) ||
        !std::isfinite(definition.eyeHeight) || definition.width <= 0.0f ||
        definition.height <= 0.0f || definition.eyeHeight <= 0.0f ||
        definition.eyeHeight > definition.height) {
        error = "entity physical dimensions are invalid"; return false;
    }
    definitions_.emplace(definition.identifier, std::move(definition));
    return true;
}

const EntityDefinition* EntityRegistry::Find(const std::string& identifier) const {
    const auto found = definitions_.find(identifier);
    return found == definitions_.end() ? nullptr : &found->second;
}

const EntityDefinition* EntityRegistry::FindByLegacyKind(std::int32_t kind) const {
    for (const auto& entry : definitions_) {
        if (entry.second.legacyKind == kind) return &entry.second;
    }
    return nullptr;
}

bool EntityRegistry::Exists(const std::string& identifier) const {
    return Find(identifier) != nullptr;
}

void EntityRegistry::Clear() { definitions_.clear(); }

EntityDefinition MakeSheepEntityDefinition() {
    EntityDefinition definition;
    definition.identifier = "minefade:sheep";
    definition.legacyKind = 0;
    definition.modelPath = "entities/sheep/sheep.mfmodel";
    definition.texturePath = "entities/sheep/sheep.png";
    definition.animationPath = "entities/sheep/sheep.animations.json";
    definition.controllerPath = "entities/sheep/sheep.controller.json";
    definition.width = 0.95f;
    definition.height = 0.90f;
    definition.eyeHeight = 0.67f;
    definition.renderFlags = 0x7u;
    definition.modelVersion = 1u;
    return definition;
}

} // namespace minefade
