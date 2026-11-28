#include "ModContentRegistry.h"

#include "../blocks/BlockAtlasManifest.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <system_error>
#include <unordered_map>

namespace minefade {
namespace {

bool ReadFile(const std::filesystem::path& path, std::size_t maximum,
              std::vector<std::uint8_t>& bytes) {
    std::error_code error;
    if (std::filesystem::is_symlink(path, error) || error ||
        !std::filesystem::is_regular_file(path, error) || error) return false;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size == 0u || size > maximum) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof() ? bytes.size() == size : false;
}

bool ReadJson(const std::filesystem::path& path, JsonValue& json) {
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(path, 1024u * 1024u, bytes)) return false;
    std::string error;
    return ParseJson(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                     json, error);
}

bool ReadOptionalJson(const std::filesystem::path& path, std::string_view label,
                      JsonValue& json, std::string& error) {
    std::error_code statusError;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(path, statusError);
    if (statusError == std::errc::no_such_file_or_directory) {
        // Optional sections may be omitted. A present but invalid section must
        // never be treated as if the mod author had intentionally omitted it.
        json.value = JsonValue::Array{};
        return true;
    }
    if (statusError) {
        error = "no se pudo comprobar " + std::string(label);
        return false;
    }
    if (status.type() == std::filesystem::file_type::not_found) {
        json.value = JsonValue::Array{};
        return true;
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        error = std::string(label) + ": se esperaba un archivo regular";
        return false;
    }
    if (!ReadJson(path, json)) {
        error = std::string(label) + ": JSON vacío, demasiado grande o mal formado";
        return false;
    }
    return true;
}

std::string GetString(const JsonValue& value, std::string_view key,
                      std::string fallback = {}) {
    const JsonValue* child = value.Find(key);
    const std::string* string = child != nullptr ? child->String() : nullptr;
    return string != nullptr ? *string : std::move(fallback);
}

bool GetNumber(const JsonValue& value, std::string_view key, double fallback,
               double minimum, double maximum, double& output) {
    const JsonValue* child = value.Find(key);
    if (child == nullptr) { output = fallback; return true; }
    double number = 0.0;
    if (!child->Number(number) || number < minimum || number > maximum) return false;
    output = number;
    return true;
}

bool GetInteger(const JsonValue& value, std::string_view key, int fallback,
                int minimum, int maximum, int& output) {
    double number = 0.0;
    if (!GetNumber(value, key, static_cast<double>(fallback),
                   static_cast<double>(minimum), static_cast<double>(maximum), number) ||
        std::floor(number) != number) return false;
    output = static_cast<int>(number);
    return true;
}

bool NamespacedId(std::string_view value, std::string_view expectedNamespace) {
    const std::size_t split = value.find(':');
    if (split == std::string_view::npos || split == 0u || split + 1u >= value.size() ||
        value.size() > 129u) return false;
    if (value.substr(0u, split) != expectedNamespace) return false;
    for (const unsigned char c : value) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                             c == '_' || c == '-' || c == '.' || c == ':';
        if (!allowed) return false;
    }
    return true;
}

bool NamespacedReference(std::string_view value, std::string_view expectedNamespace) {
    const std::size_t split = value.find(':');
    if (split == std::string_view::npos) return false;
    const std::string_view nameSpace = value.substr(0u, split);
    if (nameSpace != expectedNamespace && nameSpace != "minecraft" && nameSpace != "minefade")
        return false;
    return NamespacedId(value, nameSpace);
}

bool SafeAssetPath(std::string_view path) {
    if (path.size() < 10u || path.size() > 240u ||
        path.substr(0u, 7u) != "assets/" ||
        path.find("..") != std::string_view::npos ||
        path.find('\\\\') != std::string_view::npos ||
        path.find(':') != std::string_view::npos) return false;
    return true;
}

BlockShape ParseShape(std::string_view shape, bool& valid) {
    valid = true;
    if (shape == "cube") return BlockShape::Cube;
    if (shape == "slab") return BlockShape::Slab;
    if (shape == "stairs") return BlockShape::Stairs;
    if (shape == "cross_plant") return BlockShape::CrossPlant;
    if (shape == "thin") return BlockShape::Thin;
    if (shape == "pane") return BlockShape::Pane;
    if (shape == "fence") return BlockShape::Fence;
    if (shape == "door") return BlockShape::Door;
    if (shape == "trapdoor") return BlockShape::Trapdoor;
    valid = false;
    return BlockShape::None;
}

BlockTool ParseTool(std::string_view tool, bool& valid) {
    valid = true;
    if (tool == "none") return BlockTool::None;
    if (tool == "pickaxe") return BlockTool::Pickaxe;
    if (tool == "axe") return BlockTool::Axe;
    if (tool == "shovel") return BlockTool::Shovel;
    if (tool == "shears") return BlockTool::Shears;
    valid = false;
    return BlockTool::None;
}

std::uint16_t ResolveTile(BlockId id) {
    const auto* definition = GetBlockDefinition(id);
    return definition != nullptr ? definition->tileTop : 0u;
}

bool ReadEnabled(const std::filesystem::path& gameplayPath) {
    std::ifstream input(gameplayPath);
    if (!input) return false;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        const std::size_t split = line.find('=', first);
        if (split == std::string::npos) continue;
        std::string key = line.substr(first, split - first);
        std::string value = line.substr(split + 1u);
        const auto trim = [](std::string& text) {
            const std::size_t a = text.find_first_not_of(" \t\r");
            if (a == std::string::npos) { text.clear(); return; }
            const std::size_t b = text.find_last_not_of(" \t\r");
            text = text.substr(a, b - a + 1u);
        };
        trim(key); trim(value);
        if (key == "enabled") return value == "true";
    }
    return false;
}

std::uint32_t HashText(std::string_view text) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char value : text) {
        hash ^= static_cast<std::uint32_t>(value);
        hash *= 16777619u;
    }
    return hash;
}

} // namespace

ModContentRegistry& ModContentRegistry::Instance() {
    static ModContentRegistry registry;
    return registry;
}

void ModContentRegistry::Clear() noexcept {
    ClearModBlockDefinitions();
    recipes_.clear();
    biomes_.clear();
    structures_.clear();
    entities_.clear();
    pendingBlockDrops_.clear();
    blockDrops_.clear();
    textureTiles_.clear();
}

bool ModContentRegistry::Load(const std::filesystem::path& modsDirectory,
                              const std::uint8_t* baseAtlasPixels, std::size_t baseAtlasBytes,
                              int baseAtlasWidth, int baseAtlasHeight,
                              std::vector<std::uint8_t>& expandedAtlas,
                              int& expandedAtlasWidth, int& expandedAtlasHeight,
                              std::string& error) {
    Clear();
    error.clear();
    expandedAtlas.clear();
    if (baseAtlasPixels == nullptr || baseAtlasWidth <= 0 || baseAtlasHeight <= 0)
        { error = "atlas base inválido"; return false; }
    const std::size_t expected = static_cast<std::size_t>(baseAtlasWidth) *
        static_cast<std::size_t>(baseAtlasHeight) * 4u;
    if (baseAtlasBytes != expected) { error = "tamaño del atlas base incorrecto"; return false; }
    expandedAtlas.assign(baseAtlasPixels, baseAtlasPixels + baseAtlasBytes);
    expandedAtlasWidth = baseAtlasWidth;
    expandedAtlasHeight = baseAtlasHeight;
    SetBlockAtlasDimensions(static_cast<std::size_t>(baseAtlasWidth),
                            static_cast<std::size_t>(baseAtlasHeight));

    std::error_code fsError;
    if (!std::filesystem::is_directory(modsDirectory, fsError) || fsError) return true;
    std::vector<std::filesystem::path> packages;
    for (std::filesystem::directory_iterator it(modsDirectory,
             std::filesystem::directory_options::skip_permission_denied, fsError), end;
         !fsError && it != end; it.increment(fsError)) {
        const auto& entry = *it;
        std::error_code status;
        if (entry.is_symlink(status) || status || !entry.is_directory(status) || status) continue;
        const auto root = entry.path();
        if (std::filesystem::is_regular_file(root / "manifest.json", status) && !status &&
            std::filesystem::is_regular_file(root / "data" / "gameplay.mfmod", status) &&
            !status && ReadEnabled(root / "data" / "gameplay.mfmod")) {
            packages.push_back(root);
        }
    }
    if (fsError) { error = "no se pudo recorrer la carpeta de mods"; return false; }
    std::sort(packages.begin(), packages.end());

    std::uint16_t nextTile = static_cast<std::uint16_t>(kBlockAtlasTileCount);
    std::set<std::string> packageIds;
    for (const auto& package : packages) {
        std::vector<std::uint8_t> manifestBytes;
        JsonValue manifest;
        if (!ReadFile(package / "manifest.json", 65536u, manifestBytes) ||
            !ParseJson(std::string_view(reinterpret_cast<const char*>(manifestBytes.data()),
                                        manifestBytes.size()), manifest, error)) {
            error = "manifiesto nativo inválido en " + package.filename().string();
            return false;
        }
        double format = 0.0;
        if (const JsonValue* formatValue = manifest.Find("format")) {
            if (!formatValue->Number(format)) { error = "format de mod inválido"; return false; }
        }
        if (format != 3.0) continue; // MFMod v1/v2 gameplay settings are loaded by Engine.
        const std::string modId = GetString(manifest, "id");
        if (!NamespacedId(modId + ":root", modId) || !packageIds.insert(modId).second) {
            error = "id de mod repetido o inválido";
            return false;
        }
        if (!LoadPackage(package, expandedAtlas, baseAtlasWidth,
                         expandedAtlasHeight, nextTile, modId,
                         error)) { Clear(); return false; }
    }
    // Resolve drops only after all enabled packs have registered their item IDs.
    for (const PendingBlockDrops& pending : pendingBlockDrops_) {
        std::vector<ModBlockDropRule> rules;
        if (pending.entries.empty() || pending.entries.size() > 4u) {
            error = "tabla de drops vacía o demasiado grande";
            Clear();
            return false;
        }
        rules.reserve(pending.entries.size());
        for (const PendingBlockDrop& source : pending.entries) {
            const BlockId item = ResolveModBlockId(source.itemId);
            if (item == BlockId::Air) {
                error = "el drop " + source.itemId + " no existe o no está registrado";
                Clear();
                return false;
            }
            rules.push_back(ModBlockDropRule{
                static_cast<std::uint16_t>(item), source.minimumCount,
                source.maximumCount, source.chance
            });
        }
        const BlockId block = ResolveModBlockId(pending.blockId);
        if (block == BlockId::Air ||
            !blockDrops_.emplace(static_cast<std::uint16_t>(block), std::move(rules)).second) {
            error = "bloque de drops mod inexistente o duplicado: " + pending.blockId;
            Clear();
            return false;
        }
    }
    for (const ModRecipeDefinition& recipe : recipes_) {
        if (ResolveModBlockId(recipe.outputId) == BlockId::Air) {
            error = "la salida " + recipe.outputId + " de una receta no existe";
            Clear();
            return false;
        }
        for (const ModRecipeIngredient& ingredient : recipe.ingredients) {
            if (ResolveModBlockId(ingredient.itemId) == BlockId::Air) {
                error = "el ingrediente " + ingredient.itemId + " de una receta no existe";
                Clear();
                return false;
            }
        }
    }

    // The runtime atlas keeps the original 32-column cell layout and only grows
    // by complete rows; this preserves all built-in texture coordinates.
    expandedAtlasWidth = baseAtlasWidth;
    std::sort(entities_.begin(), entities_.end(),
        [](const ModEntityDefinition& a, const ModEntityDefinition& b) {
            return a.id < b.id;
        });
    if (entities_.size() > 128u) {
        error = "demasiadas entidades de mods instaladas";
        Clear();
        return false;
    }
    std::set<std::string> entityIds;
    for (std::size_t index = 0; index < entities_.size(); ++index) {
        if (!entityIds.insert(entities_[index].id).second) {
            error = "id de entidad mod duplicado: " + entities_[index].id;
            Clear();
            return false;
        }
        entities_[index].runtimeKind = 64 + static_cast<int>(index);
    }
    SetBlockAtlasDimensions(static_cast<std::size_t>(expandedAtlasWidth),
                            static_cast<std::size_t>(expandedAtlasHeight));
    return true;
}

bool ModContentRegistry::RegisterTexture(const std::filesystem::path& packageRoot,
                                         std::string_view relativePath,
                                         std::vector<std::uint8_t>& atlas, int atlasWidth,
                                         int& atlasHeight, std::uint16_t& nextTile,
                                         std::uint16_t& tile, std::string& error) {
    if (!SafeAssetPath(relativePath)) { error = "ruta de textura no permitida"; return false; }
    std::string textureName(relativePath);
    std::transform(textureName.begin(), textureName.end(), textureName.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (textureName.size() < 5u || textureName.compare(textureName.size() - 5u, 5u, ".rgba") != 0) {
        error = "formato de textura no compatible: usa RGBA 16x16";
        return false;
    }
    const std::string cacheKey = packageRoot.generic_string() + "|" + std::string(relativePath);
    const auto cached = textureTiles_.find(cacheKey);
    if (cached != textureTiles_.end()) {
        tile = cached->second;
        return true;
    }
    std::vector<std::uint8_t> pixels;
    if (!ReadFile(packageRoot / std::filesystem::path(relativePath), 1024u, pixels) ||
        pixels.size() != 1024u) {
        error = "textura RGBA no válida: " + std::string(relativePath);
        return false;
    }
    if (nextTile >= 65535u) { error = "el atlas superó el límite de celdas"; return false; }
    tile = nextTile++;
    const std::size_t rows = static_cast<std::size_t>(tile) / kBlockAtlasColumns + 1u;
    const std::size_t requiredHeight = rows * kBlockAtlasCellSize;
    if (requiredHeight > 16384u) { error = "el atlas mod supera la altura máxima"; return false; }
    if (requiredHeight > static_cast<std::size_t>(atlasHeight)) {
        const std::size_t oldSize = atlas.size();
        const std::size_t newSize = static_cast<std::size_t>(atlasWidth) * requiredHeight * 4u;
        if (newSize > 128u * 1024u * 1024u) { error = "el atlas mod supera 128 MiB"; return false; }
        atlas.resize(newSize, 0u);
        (void)oldSize;
        atlasHeight = static_cast<int>(requiredHeight);
    }
    const std::size_t column = static_cast<std::size_t>(tile) % kBlockAtlasColumns;
    const std::size_t row = static_cast<std::size_t>(tile) / kBlockAtlasColumns;
    const std::size_t originX = column * kBlockAtlasCellSize;
    const std::size_t originY = row * kBlockAtlasCellSize;
    for (std::size_t y = 0; y < kBlockAtlasCellSize; ++y) {
        const std::size_t sourceY = std::clamp<std::size_t>(y, 1u, 16u) - 1u;
        for (std::size_t x = 0; x < kBlockAtlasCellSize; ++x) {
            const std::size_t sourceX = std::clamp<std::size_t>(x, 1u, 16u) - 1u;
            const std::size_t sourceIndex = (sourceY * 16u + sourceX) * 4u;
            const std::size_t targetIndex =
                ((originY + y) * static_cast<std::size_t>(atlasWidth) + originX + x) * 4u;
            std::copy_n(pixels.data() + sourceIndex, 4u, atlas.data() + targetIndex);
        }
    }
    textureTiles_.emplace(cacheKey, tile);
    return true;
}

bool ModContentRegistry::LoadPackage(const std::filesystem::path& packageRoot,
                                     std::vector<std::uint8_t>& atlas, int atlasWidth,
                                     int& atlasHeight, std::uint16_t& nextTile,
                                     std::string_view expectedModId, std::string& error) {
    JsonValue blocksJson;
    if (!ReadOptionalJson(packageRoot / "data" / "blocks.json", "data/blocks.json", blocksJson, error)) return false;
    {

        const auto* blocks = blocksJson.Items();
        if (blocks == nullptr || blocks->size() > 256u) {
            error = "data/blocks.json inválido";
            return false;
        }
        std::vector<const JsonValue*> sorted;
        sorted.reserve(blocks->size());
        for (const auto& value : *blocks) sorted.push_back(&value);
        std::sort(sorted.begin(), sorted.end(), [](const JsonValue* a, const JsonValue* b) {
            return GetString(*a, "id") < GetString(*b, "id");
        });
        for (const JsonValue* value : sorted) {
            const std::string id = GetString(*value, "id");
            const std::size_t split = id.find(':');
            if (split == std::string::npos || !NamespacedId(id, expectedModId)) {
                error = "id de bloque mod inválido";
                return false;
            }
            const JsonValue* textures = value->Find("textures");
            if (textures == nullptr) { error = "bloque sin texturas"; return false; }
            const std::string top = GetString(*textures, "top");
            const std::string side = GetString(*textures, "side");
            const std::string bottom = GetString(*textures, "bottom");
            std::uint16_t tileTop = 0u, tileSide = 0u, tileBottom = 0u;
            if (!RegisterTexture(packageRoot, top, atlas, atlasWidth, atlasHeight, nextTile, tileTop, error) ||
                !RegisterTexture(packageRoot, side, atlas, atlasWidth, atlasHeight, nextTile, tileSide, error) ||
                !RegisterTexture(packageRoot, bottom, atlas, atlasWidth, atlasHeight, nextTile, tileBottom, error))
                return false;
            bool shapeValid = false, toolValid = false;
            const BlockShape shape = ParseShape(GetString(*value, "shape", "cube"), shapeValid);
            const BlockTool tool = ParseTool(GetString(*value, "tool", "none"), toolValid);
            if (!shapeValid || !toolValid) { error = "forma o herramienta de bloque inválida"; return false; }
            double hardness = 1.0;
            int stack = 64, light = 0;
            if (!GetNumber(*value, "hardness", 1.0, 0.0, 100.0, hardness) ||
                !GetInteger(*value, "stack_limit", 64, 1, 99, stack) ||
                !GetInteger(*value, "light_level", 0, 0, 15, light)) {
                error = "propiedades de bloque fuera de rango";
                return false;
            }
            bool transparent = false, collidable = true, emissive = false;
            if (const JsonValue* v = value->Find("transparent")) v->Boolean(transparent);
            if (const JsonValue* v = value->Find("collidable")) v->Boolean(collidable);
            if (const JsonValue* v = value->Find("emissive")) v->Boolean(emissive);
            std::uint16_t flags = BlockPlaceable;
            if (collidable) flags |= BlockCollidable;
            if (transparent) flags |= BlockTransparent;
            else if (shape != BlockShape::None) flags |= BlockOpaque;
            if (emissive || light > 0) flags |= BlockEmissive;
            if (shape == BlockShape::CrossPlant) flags |= BlockPlant;
            ModBlockRegistration registration;
            registration.key = id;
            registration.nameEs = GetString(*value, "name");
            registration.sound = GetString(*value, "sound", "stone");
            registration.textureTop = top;
            registration.textureSide = side;
            registration.textureBottom = bottom;
            registration.tileTop = tileTop;
            registration.tileSide = tileSide;
            registration.tileBottom = tileBottom;
            registration.hardness = static_cast<float>(hardness);
            registration.stackLimit = static_cast<std::uint8_t>(stack);
            registration.lightLevel = static_cast<std::uint8_t>(light);
            registration.flags = flags;
            registration.shape = shape;
            registration.preferredTool = tool;

            PendingBlockDrops pending;
            pending.blockId = id;
            const auto appendDrop = [&](const JsonValue& source) -> bool {
                PendingBlockDrop drop;
                if (const std::string* shorthand = source.String()) {
                    drop.itemId = *shorthand;
                } else {
                    drop.itemId = GetString(source, "id");
                    int minimum = 1, maximum = 1;
                    double chance = 1.0;
                    if (!GetInteger(source, "min", 1, 1, 99, minimum) ||
                        !GetInteger(source, "max", minimum, 1, 99, maximum) ||
                        maximum < minimum ||
                        !GetNumber(source, "chance", 1.0, 0.0, 1.0, chance)) return false;
                    drop.minimumCount = static_cast<std::uint16_t>(minimum);
                    drop.maximumCount = static_cast<std::uint16_t>(maximum);
                    drop.chance = static_cast<float>(chance);
                }
                if (!NamespacedReference(drop.itemId, expectedModId)) return false;
                pending.entries.push_back(std::move(drop));
                return pending.entries.size() <= 4u;
            };
            const JsonValue* dropValue = value->Find("drop");
            if (dropValue == nullptr) {
                pending.entries.push_back(PendingBlockDrop{id, 1u, 1u, 1.0f});
            } else if (const auto* dropArray = dropValue->Items()) {
                if (dropArray->empty() || dropArray->size() > 4u) {
                    error = "drop debe contener entre uno y cuatro resultados";
                    return false;
                }
                for (const JsonValue& entry : *dropArray) {
                    if (!appendDrop(entry)) {
                        error = "definición de drop inválida en " + id;
                        return false;
                    }
                }
            } else if (!appendDrop(*dropValue)) {
                error = "definición de drop inválida en " + id;
                return false;
            }
            if (!RegisterModBlock(std::move(registration), nullptr, &error)) return false;
            pendingBlockDrops_.push_back(std::move(pending));
        }
    }

    JsonValue itemsJson;
    if (!ReadOptionalJson(packageRoot / "data" / "items.json", "data/items.json", itemsJson, error)) return false;
    {

        const auto* items = itemsJson.Items();
        if (items == nullptr || items->size() > 256u) { error = "data/items.json inválido"; return false; }
        std::vector<const JsonValue*> sorted;
        for (const auto& value : *items) sorted.push_back(&value);
        std::sort(sorted.begin(), sorted.end(), [](const JsonValue* a, const JsonValue* b) {
            return GetString(*a, "id") < GetString(*b, "id");
        });
        for (const JsonValue* value : sorted) {
            const std::string id = GetString(*value, "id");
            const std::size_t split = id.find(':');
            if (split == std::string::npos || !NamespacedId(id, expectedModId)) {
                error = "id de objeto mod inválido"; return false;
            }
            const std::string icon = GetString(*value, "icon");
            std::uint16_t iconTile = 0u;
            if (!RegisterTexture(packageRoot, icon, atlas, atlasWidth, atlasHeight, nextTile, iconTile, error))
                return false;
            const std::string type = GetString(*value, "type", "item");
            double damage = 0.0;
            int durability = 0, protection = 0, stack = 64, armorSlot = 255;
            if (type == "weapon" || type == "tool") {
                if (!GetNumber(*value, "damage", 1.0, 0.0, 100.0, damage) ||
                    !GetInteger(*value, "durability", 100, 1, 100000, durability)) {
                    error = "estadísticas de herramienta/arma inválidas"; return false;
                }
            }
            if (type == "armor") {
                const std::string slot = GetString(*value, "armor_slot");
                armorSlot = slot == "helmet" ? 0 : slot == "chestplate" ? 1 :
                            slot == "leggings" ? 2 : slot == "boots" ? 3 : 255;
                if (armorSlot > 3 || !GetInteger(*value, "protection", 1, 0, 20, protection) ||
                    !GetInteger(*value, "durability", 100, 1, 100000, durability)) {
                    error = "definición de armadura inválida"; return false;
                }
            }
            if (!GetInteger(*value, "stack_limit", 64, 1, 99, stack)) {
                error = "stack_limit de objeto inválido"; return false;
            }
            bool toolValid = false;
            const BlockTool tool = ParseTool(GetString(*value, "tool", "none"), toolValid);
            if (!toolValid) { error = "herramienta de objeto inválida"; return false; }
            ModBlockRegistration registration;
            registration.key = id;
            registration.nameEs = GetString(*value, "name");
            registration.sound = "ui";
            registration.textureTop = icon;
            registration.textureSide = icon;
            registration.textureBottom = icon;
            registration.tileTop = registration.tileSide = registration.tileBottom = iconTile;
            registration.hardness = 0.0f;
            registration.stackLimit = static_cast<std::uint8_t>(stack);
            registration.flags = 0u;
            registration.shape = BlockShape::None;
            registration.preferredTool = tool;
            registration.attackDamage = static_cast<float>(damage);
            registration.durability = static_cast<std::uint16_t>(std::min(durability, 65535));
            registration.armorProtection = static_cast<std::uint8_t>(protection);
            registration.armorSlot = static_cast<std::uint8_t>(armorSlot);
            if (!RegisterModBlock(std::move(registration), nullptr, &error)) return false;
        }
    }

    JsonValue recipesJson;
    if (!ReadOptionalJson(packageRoot / "data" / "recipes.json", "data/recipes.json", recipesJson, error)) return false;
    {

        const auto* recipes = recipesJson.Items();
        if (recipes == nullptr || recipes->size() > 512u) { error = "data/recipes.json inválido"; return false; }
        for (const auto& value : *recipes) {
            ModRecipeDefinition recipe;
            recipe.id = GetString(value, "id");
            recipe.name = GetString(value, "name", recipe.id);
            recipe.station = GetString(value, "station", "inventory");
            static const std::set<std::string> validStations{
                "inventory", "crafting_table", "furnace", "anvil", "stonecutter"
            };
            if (!NamespacedId(recipe.id, expectedModId) ||
                validStations.find(recipe.station) == validStations.end() ||
                FindRecipe(recipe.id) != nullptr) {
                error = "id de receta duplicado o estación no compatible";
                return false;
            }
            const JsonValue* output = value.Find("output");
            if (output == nullptr) { error = "receta sin output"; return false; }
            recipe.outputId = GetString(*output, "id");
            if (!NamespacedReference(recipe.outputId, expectedModId)) {
                error = "salida de receta con ID namespaced inválido"; return false;
            }
            int outputCount = 1;
            if (!GetInteger(*output, "count", 1, 1, 999, outputCount)) {
                error = "cantidad de salida de receta inválida"; return false;
            }
            recipe.outputCount = static_cast<std::uint16_t>(outputCount);
            const JsonValue* ingredientValue = value.Find("ingredients");
            const auto* ingredients = ingredientValue != nullptr ? ingredientValue->Items() : nullptr;
            if (ingredients == nullptr || ingredients->empty() || ingredients->size() > 9u) {
                error = "ingredientes de receta inválidos"; return false;
            }
            for (const auto& ingredient : *ingredients) {
                ModRecipeIngredient item;
                item.itemId = GetString(ingredient, "id");
                if (!NamespacedReference(item.itemId, expectedModId)) {
                    error = "ingrediente de receta con ID namespaced inválido"; return false;
                }
                int count = 1;
                if (!GetInteger(ingredient, "count", 1, 1, 999, count)) {
                    error = "cantidad de ingrediente inválida"; return false;
                }
                item.count = static_cast<std::uint16_t>(count);
                recipe.ingredients.push_back(std::move(item));
            }
            recipes_.push_back(std::move(recipe));
        }
    }

    JsonValue biomesJson;
    if (!ReadOptionalJson(packageRoot / "data" / "biomes.json", "data/biomes.json", biomesJson, error)) return false;
    {

        const auto* biomes = biomesJson.Items();
        if (biomes == nullptr || biomes->size() > 64u) { error = "data/biomes.json inválido"; return false; }
        for (const auto& value : *biomes) {
            ModBiomeDefinition biome;
            biome.id = GetString(value, "id");
            biome.name = GetString(value, "name", biome.id);
            const JsonValue* temperature = value.Find("temperature");
            const JsonValue* humidity = value.Find("humidity");
            const auto parseRange = [](const JsonValue* range, float fallbackMin, float fallbackMax,
                                       float& minimum, float& maximum) {
                if (range == nullptr) { minimum = fallbackMin; maximum = fallbackMax; return true; }
                const auto* values = range->Items();
                double low = 0.0, high = 0.0;
                if (values == nullptr || values->size() != 2u ||
                    !(*values)[0].Number(low) || !(*values)[1].Number(high) ||
                    low < -1.0 || high > 1.0 || low > high) return false;
                minimum = static_cast<float>(low); maximum = static_cast<float>(high);
                return true;
            };
            if (!parseRange(temperature, -1.0f, 1.0f, biome.temperatureMin, biome.temperatureMax) ||
                !parseRange(humidity, -1.0f, 1.0f, biome.humidityMin, biome.humidityMax)) {
                error = "rango de clima de bioma inválido"; return false;
            }
            biome.surfaceBlock = GetString(value, "surface");
            biome.fillerBlock = GetString(value, "filler");
            if (biome.surfaceBlock.empty()) { error = "bioma sin bloque de superficie"; return false; }
            biomes_.push_back(std::move(biome));
        }
    }

    JsonValue structuresJson;
    if (!ReadOptionalJson(packageRoot / "data" / "structures.json", "data/structures.json", structuresJson, error)) return false;
    {

        const auto* structures = structuresJson.Items();
        if (structures == nullptr || structures->size() > 64u) {
            error = "data/structures.json inválido"; return false;
        }
        for (const auto& value : *structures) {
            ModStructureDefinition structure;
            structure.id = GetString(value, "id");
            double chance = 0.08;
            if (!GetNumber(value, "chance", 0.08, 0.0, 1.0, chance)) {
                error = "probabilidad de estructura inválida"; return false;
            }
            structure.chance = static_cast<float>(chance);
            if (const JsonValue* replace = value.Find("replace_existing")) replace->Boolean(structure.replaceExisting);
            const JsonValue* blockValues = value.Find("blocks");
            const auto* blocks = blockValues != nullptr ? blockValues->Items() : nullptr;
            if (blocks == nullptr || blocks->empty() || blocks->size() > 4096u) {
                error = "bloques de estructura inválidos"; return false;
            }
            for (const auto& block : *blocks) {
                const JsonValue* positionValue = block.Find("pos");
                const auto* position = positionValue != nullptr ? positionValue->Items() : nullptr;
                if (position == nullptr || position->size() != 3u) {
                    error = "posición de bloque de estructura inválida"; return false;
                }
                double x = 0.0, y = 0.0, z = 0.0;
                if (!(*position)[0].Number(x) || !(*position)[1].Number(y) ||
                    !(*position)[2].Number(z) || std::floor(x) != x ||
                    std::floor(y) != y || std::floor(z) != z ||
                    x < -64.0 || x > 64.0 || y < -64.0 || y > 64.0 ||
                    z < -64.0 || z > 64.0) {
                    error = "coordenada de estructura fuera de rango"; return false;
                }
                structure.blocks.push_back({static_cast<int>(x), static_cast<int>(y),
                                             static_cast<int>(z), GetString(block, "block")});
            }
            structures_.push_back(std::move(structure));
        }
    }

    JsonValue entitiesJson;
    if (!ReadOptionalJson(packageRoot / "data" / "entities.json", "data/entities.json", entitiesJson, error)) return false;
    {

        const auto* entities = entitiesJson.Items();
        if (entities == nullptr || entities->size() > 128u) { error = "data/entities.json inválido"; return false; }
        for (const auto& value : *entities) {
            ModEntityDefinition entity;
            entity.id = GetString(value, "id");
            entity.name = GetString(value, "name", entity.id);
            int spawnCount = 1;
            if (!GetInteger(value, "spawn_count", 1, 0, 8, spawnCount)) {
                error = "cantidad de aparición de entidad inválida"; return false;
            }
            entity.spawnCount = spawnCount;
            entity.modelPath = GetString(value, "model");
            entity.texturePath = GetString(value, "texture");
            entity.animationPath = GetString(value, "animations");
            entity.controllerPath = GetString(value, "controller");
            double width = 0.8, height = 1.0, eye = 0.7, health = 10.0, damage = 1.0;
            if (!GetNumber(value, "width", 0.8, 0.1, 16.0, width) ||
                !GetNumber(value, "height", 1.0, 0.1, 32.0, height) ||
                !GetNumber(value, "eye_height", 0.7, 0.1, 32.0, eye) ||
                !GetNumber(value, "health", 10.0, 1.0, 10000.0, health) ||
                !GetNumber(value, "damage", 1.0, 0.0, 100.0, damage)) {
                error = "estadísticas de entidad inválidas"; return false;
            }
            entity.width = static_cast<float>(width);
            entity.height = static_cast<float>(height);
            entity.eyeHeight = static_cast<float>(eye);
            entity.health = static_cast<float>(health);
            entity.damage = static_cast<float>(damage);
            if (const JsonValue* hostile = value.Find("hostile")) hostile->Boolean(entity.hostile);
            entities_.push_back(std::move(entity));
        }
    }
    return true;
}

const std::vector<ModBlockDropRule>* ModContentRegistry::DropsForBlock(BlockId block) const noexcept {
    const auto found = blockDrops_.find(static_cast<std::uint16_t>(block));
    return found == blockDrops_.end() ? nullptr : &found->second;
}

const ModRecipeDefinition* ModContentRegistry::FindRecipe(std::string_view id) const noexcept {
    for (const auto& recipe : recipes_) if (recipe.id == id) return &recipe;
    return nullptr;
}

BlockId ResolveModBlockId(std::string_view namespacedId) noexcept {
    const BlockDefinition* definition = FindBlockDefinition(namespacedId);
    if (definition == nullptr) {
        const std::size_t split = namespacedId.find(':');
        if (split != std::string_view::npos &&
            (namespacedId.substr(0u, split) == "minecraft" ||
             namespacedId.substr(0u, split) == "minefade")) {
            definition = FindBlockDefinition(namespacedId.substr(split + 1u));
        }
    }
    return definition != nullptr ? definition->id : BlockId::Air;
}

const ModBiomeDefinition* FindModBiomeForClimate(float temperature, float humidity) noexcept {
    const auto& biomes = ModContentRegistry::Instance().Biomes();
    for (const auto& biome : biomes) {
        if (temperature >= biome.temperatureMin && temperature <= biome.temperatureMax &&
            humidity >= biome.humidityMin && humidity <= biome.humidityMax) return &biome;
    }
    return nullptr;
}

} // namespace minefade
