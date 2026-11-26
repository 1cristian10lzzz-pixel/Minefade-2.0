#pragma once

#include "JsonLite.h"
#include "../blocks/BlockRegistry.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace minefade {

struct ModRecipeIngredient {
    std::string itemId;
    std::uint16_t count{1u};
};

struct ModRecipeDefinition {
    std::string id;
    std::string name;
    std::string outputId;
    std::uint16_t outputCount{1u};
    std::string station;
    std::vector<ModRecipeIngredient> ingredients;
};

struct ModBiomeDefinition {
    std::string id;
    std::string name;
    float temperatureMin{-1.0f};
    float temperatureMax{1.0f};
    float humidityMin{-1.0f};
    float humidityMax{1.0f};
    std::string surfaceBlock;
    std::string fillerBlock;
};

struct ModStructureBlock {
    int x{0}, y{0}, z{0};
    std::string blockId;
};

struct ModStructureDefinition {
    std::string id;
    float chance{0.08f};
    bool replaceExisting{false};
    std::vector<ModStructureBlock> blocks;
};

struct ModEntityDefinition {
    std::string id;
    std::string name;
    std::string modelPath;
    std::string texturePath;
    std::string animationPath;
    std::string controllerPath;
    float width{0.8f};
    float height{1.0f};
    float eyeHeight{0.7f};
    float health{10.0f};
    float damage{1.0f};
    bool hostile{false};
};

class ModContentRegistry {
public:
    static ModContentRegistry& Instance();

    bool Load(const std::filesystem::path& modsDirectory,
              const std::uint8_t* baseAtlasPixels, std::size_t baseAtlasBytes,
              int baseAtlasWidth, int baseAtlasHeight,
              std::vector<std::uint8_t>& expandedAtlas,
              int& expandedAtlasWidth, int& expandedAtlasHeight,
              std::string& error);

    void Clear() noexcept;
    const std::vector<ModRecipeDefinition>& Recipes() const noexcept { return recipes_; }
    const std::vector<ModBiomeDefinition>& Biomes() const noexcept { return biomes_; }
    const std::vector<ModStructureDefinition>& Structures() const noexcept { return structures_; }
    const std::vector<ModEntityDefinition>& Entities() const noexcept { return entities_; }
    const ModRecipeDefinition* FindRecipe(std::string_view id) const noexcept;

private:
    bool LoadPackage(const std::filesystem::path& packageRoot,
                     std::vector<std::uint8_t>& atlas, int atlasWidth,
                     int& atlasHeight, std::uint16_t& nextTile,
                     std::string& error);
    bool RegisterTexture(const std::filesystem::path& packageRoot, std::string_view relativePath,
                         std::vector<std::uint8_t>& atlas, int atlasWidth, int& atlasHeight,
                         std::uint16_t& nextTile, std::uint16_t& tile,
                         std::string& error);

    std::vector<ModRecipeDefinition> recipes_;
    std::vector<ModBiomeDefinition> biomes_;
    std::vector<ModStructureDefinition> structures_;
    std::vector<ModEntityDefinition> entities_;
};

BlockId ResolveModBlockId(std::string_view namespacedId) noexcept;
const ModBiomeDefinition* FindModBiomeForClimate(float temperature, float humidity) noexcept;

} // namespace minefade
