#include "ModelLoader.h"

#include "ModelValidator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>

namespace minefade {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic{{'M','F','M','O','D','E','L',0}};
constexpr std::uint32_t kNoParent = 0xFFFFFFFFu;
constexpr std::size_t kHeaderBytes = 8u + 2u + 2u + 12u * 4u + 6u * 4u;
constexpr std::size_t kMaxFileBytes = 8u * 1024u * 1024u;
constexpr std::uint32_t kMaxBones = 128u;
constexpr std::uint32_t kMaxCubes = 4096u;
constexpr std::uint32_t kMaxMaterials = 64u;
constexpr std::uint32_t kMaxLocators = 128u;
constexpr std::uint32_t kMaxClips = 64u;
constexpr std::uint32_t kMaxTracks = 512u;
constexpr std::uint32_t kMaxKeys = 16384u;
constexpr std::uint32_t kMaxStates = 32u;
constexpr std::uint32_t kMaxStrings = 65536u;

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool ReadU16(std::uint16_t& value) {
        if (!CanRead(2u)) return false;
        value = static_cast<std::uint16_t>(data_[position_]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[position_ + 1u]) << 8u);
        position_ += 2u;
        return true;
    }

    bool ReadU32(std::uint32_t& value) {
        if (!CanRead(4u)) return false;
        value = static_cast<std::uint32_t>(data_[position_]) |
                (static_cast<std::uint32_t>(data_[position_ + 1u]) << 8u) |
                (static_cast<std::uint32_t>(data_[position_ + 2u]) << 16u) |
                (static_cast<std::uint32_t>(data_[position_ + 3u]) << 24u);
        position_ += 4u;
        return true;
    }

    bool ReadF32(float& value) {
        std::uint32_t bits = 0;
        if (!ReadU32(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value);
    }

    bool ReadBytes(void* destination, std::size_t count) {
        if (!CanRead(count)) return false;
        if (count != 0u) std::memcpy(destination, data_ + position_, count);
        position_ += count;
        return true;
    }

    bool Skip(std::size_t count) {
        if (!CanRead(count)) return false;
        position_ += count;
        return true;
    }

    std::size_t Position() const { return position_; }
    std::size_t Remaining() const { return size_ - position_; }

private:
    bool CanRead(std::size_t count) const {
        return position_ <= size_ && count <= size_ - position_;
    }

    const std::uint8_t* data_{nullptr};
    std::size_t size_{0};
    std::size_t position_{0};
};

bool ReadVec3(Reader& reader, ModelVec3& value) {
    return reader.ReadF32(value.x) && reader.ReadF32(value.y) && reader.ReadF32(value.z);
}

bool GetString(const std::vector<std::uint8_t>& table, std::uint32_t offset,
               std::string& result) {
    if (offset >= table.size()) return false;
    const auto begin = table.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, table.end(), static_cast<std::uint8_t>(0));
    if (end == table.end() || end == begin) return false;
    const std::size_t length = static_cast<std::size_t>(end - begin);
    if (length > 1024u) return false;
    result.assign(reinterpret_cast<const char*>(&*begin), length);
    // Resource identifiers and paths are UTF-8 byte strings. Reject ASCII controls.
    for (unsigned char c : result) {
        if (c < 0x20u || c == 0x7Fu) return false;
    }
    return true;
}

bool ReadCount(std::uint32_t value, std::uint32_t maximum, const char* label,
               std::string& error) {
    if (value > maximum) {
        error = std::string("model ") + label + " count exceeds limit";
        return false;
    }
    return true;
}

} // namespace

bool ModelLoader::Load(const std::uint8_t* bytes, std::size_t size,
                       ModelData& output, std::string& error) {
    error.clear();
    output = {};
    if (bytes == nullptr || size < kHeaderBytes || size > kMaxFileBytes) {
        error = "model file is null, truncated or exceeds 8 MiB";
        return false;
    }

    Reader reader(bytes, size);
    std::array<std::uint8_t, 8> magic{};
    if (!reader.ReadBytes(magic.data(), magic.size()) || magic != kMagic) {
        error = "model magic is not MFMODEL";
        return false;
    }

    std::uint16_t major = 0, minor = 0;
    if (!reader.ReadU16(major) || !reader.ReadU16(minor)) {
        error = "model version header is truncated";
        return false;
    }
    if (major != 1u || minor > 0u) {
        error = "unsupported .mfmodel version";
        return false;
    }

    std::array<std::uint32_t, 12> fields{};
    for (auto& field : fields) {
        if (!reader.ReadU32(field)) {
            error = "model section table is truncated";
            return false;
        }
    }
    const std::uint32_t headerBytes = fields[0];
    const std::uint32_t totalBytes = fields[1];
    const std::uint32_t expectedCrc = fields[2];
    const std::uint32_t boneCount = fields[3];
    const std::uint32_t cubeCount = fields[4];
    const std::uint32_t materialCount = fields[5];
    const std::uint32_t locatorCount = fields[6];
    const std::uint32_t clipCount = fields[7];
    const std::uint32_t trackCount = fields[8];
    const std::uint32_t keyCount = fields[9];
    const std::uint32_t stateCount = fields[10];
    const std::uint32_t stringBytes = fields[11];

    if (headerBytes != kHeaderBytes || totalBytes != size || reader.Position() != kHeaderBytes - 6u * 4u) {
        error = "model header size or total file size is inconsistent";
        return false;
    }
    if (!ReadCount(boneCount, kMaxBones, "bone", error) ||
        !ReadCount(cubeCount, kMaxCubes, "cube", error) ||
        !ReadCount(materialCount, kMaxMaterials, "material", error) ||
        !ReadCount(locatorCount, kMaxLocators, "locator", error) ||
        !ReadCount(clipCount, kMaxClips, "clip", error) ||
        !ReadCount(trackCount, kMaxTracks, "track", error) ||
        !ReadCount(keyCount, kMaxKeys, "keyframe", error) ||
        !ReadCount(stateCount, kMaxStates, "state", error) ||
        !ReadCount(stringBytes, kMaxStrings, "string table", error)) {
        return false;
    }
    if (boneCount == 0u || cubeCount == 0u || materialCount == 0u ||
        clipCount == 0u || stateCount == 0u) {
        error = "model is missing required bones, cubes, materials, clips or states";
        return false;
    }

    ModelVec3 boundsMin{}, boundsMax{};
    if (!ReadVec3(reader, boundsMin) || !ReadVec3(reader, boundsMax)) {
        error = "model bounds contain non-finite or truncated values";
        return false;
    }
    if (reader.Remaining() < stringBytes) {
        error = "model string table exceeds file size";
        return false;
    }
    std::vector<std::uint8_t> strings(stringBytes);
    if (!reader.ReadBytes(strings.data(), strings.size())) {
        error = "model string table is truncated";
        return false;
    }
    const std::uint32_t crc = Crc32(bytes + headerBytes, size - headerBytes);
    if (crc != expectedCrc) {
        error = "model CRC32 verification failed";
        return false;
    }

    ModelData parsed;
    parsed.versionMajor = major;
    parsed.versionMinor = minor;
    parsed.boundsMin = boundsMin;
    parsed.boundsMax = boundsMax;
    parsed.bones.reserve(boneCount);
    parsed.cubes.reserve(cubeCount);
    parsed.materials.reserve(materialCount);
    parsed.locators.reserve(locatorCount);
    parsed.clips.reserve(clipCount);
    parsed.tracks.reserve(trackCount);
    parsed.states.reserve(stateCount);

    for (std::uint32_t i = 0; i < boneCount; ++i) {
        std::uint32_t nameOffset = 0, parentIndex = 0;
        ModelBone bone;
        if (!reader.ReadU32(nameOffset) || !reader.ReadU32(parentIndex) ||
            !GetString(strings, nameOffset, bone.name) ||
            !reader.ReadF32(bone.pivot.x) || !reader.ReadF32(bone.pivot.y) ||
            !reader.ReadF32(bone.pivot.z) ||
            !reader.ReadF32(bone.bindRotationDegrees.x) ||
            !reader.ReadF32(bone.bindRotationDegrees.y) ||
            !reader.ReadF32(bone.bindRotationDegrees.z)) {
            error = "invalid or truncated bone section";
            return false;
        }
        bone.parentIndex = parentIndex;
        parsed.bones.push_back(std::move(bone));
    }

    for (std::uint32_t i = 0; i < cubeCount; ++i) {
        ModelCube cube;
        if (!reader.ReadU32(cube.boneIndex) || !reader.ReadU32(cube.materialIndex) ||
            !ReadVec3(reader, cube.origin) || !ReadVec3(reader, cube.size) ||
            !ReadVec3(reader, cube.pivot) || !ReadVec3(reader, cube.rotationDegrees)) {
            error = "invalid or truncated cube transform";
            return false;
        }
        for (auto& uv : cube.faceUv) {
            for (float& component : uv) {
                if (!reader.ReadF32(component)) {
                    error = "invalid or truncated cube UV section";
                    return false;
                }
            }
        }
        if (!reader.ReadBytes(cube.faceColors.data(), cube.faceColors.size())) {
            error = "truncated cube face-colour section";
            return false;
        }
        parsed.cubes.push_back(cube);
    }

    for (std::uint32_t i = 0; i < materialCount; ++i) {
        std::uint32_t nameOffset = 0, textureOffset = 0;
        ModelMaterial material;
        if (!reader.ReadU32(nameOffset) || !reader.ReadU32(textureOffset) ||
            !reader.ReadU32(material.flags) ||
            !GetString(strings, nameOffset, material.name) ||
            !GetString(strings, textureOffset, material.texturePath)) {
            error = "invalid or truncated material section";
            return false;
        }
        for (float& component : material.baseColor) {
            if (!reader.ReadF32(component)) {
                error = "invalid material base colour";
                return false;
            }
        }
        parsed.materials.push_back(std::move(material));
    }

    for (std::uint32_t i = 0; i < locatorCount; ++i) {
        std::uint32_t nameOffset = 0;
        ModelLocator locator;
        if (!reader.ReadU32(nameOffset) || !reader.ReadU32(locator.boneIndex) ||
            !GetString(strings, nameOffset, locator.name) ||
            !ReadVec3(reader, locator.position)) {
            error = "invalid or truncated locator section";
            return false;
        }
        parsed.locators.push_back(std::move(locator));
    }

    for (std::uint32_t i = 0; i < clipCount; ++i) {
        std::uint32_t nameOffset = 0, flags = 0;
        ModelAnimationClip clip;
        if (!reader.ReadU32(nameOffset) || !reader.ReadU32(flags) ||
            !reader.ReadF32(clip.duration) ||
            !reader.ReadU32(clip.firstTrack) || !reader.ReadU32(clip.trackCount) ||
            !GetString(strings, nameOffset, clip.name)) {
            error = "invalid or truncated animation clip section";
            return false;
        }
        if ((flags & ~1u) != 0u) {
            error = "animation clip uses unsupported flags";
            return false;
        }
        clip.loop = (flags & 1u) != 0u;
        parsed.clips.push_back(std::move(clip));
    }

    std::vector<std::array<std::uint32_t, 4>> rawTracks;
    rawTracks.reserve(trackCount);
    for (std::uint32_t i = 0; i < trackCount; ++i) {
        std::array<std::uint32_t, 4> track{};
        for (auto& field : track) {
            if (!reader.ReadU32(field)) {
                error = "truncated animation track section";
                return false;
            }
        }
        rawTracks.push_back(track);
    }

    std::vector<ModelKeyframe> allKeys;
    allKeys.reserve(keyCount);
    for (std::uint32_t i = 0; i < keyCount; ++i) {
        ModelKeyframe key;
        if (!reader.ReadF32(key.time) || !ReadVec3(reader, key.translation) ||
            !reader.ReadF32(key.rotation.x) || !reader.ReadF32(key.rotation.y) ||
            !reader.ReadF32(key.rotation.z) || !reader.ReadF32(key.rotation.w) ||
            !ReadVec3(reader, key.scale)) {
            error = "invalid or truncated animation keyframe";
            return false;
        }
        allKeys.push_back(key);
    }

    for (const auto& raw : rawTracks) {
        const std::uint32_t clipIndex = raw[0];
        const std::uint32_t boneIndex = raw[1];
        const std::uint32_t firstKey = raw[2];
        const std::uint32_t count = raw[3];
        if (clipIndex >= clipCount || boneIndex >= boneCount ||
            count == 0u || count > 256u ||
            static_cast<std::size_t>(firstKey) + count > allKeys.size()) {
            error = "animation track has out-of-range clip, bone or key range";
            return false;
        }
        ModelTrack track;
        track.clipIndex = clipIndex;
        track.boneIndex = boneIndex;
        track.keyframes.assign(allKeys.begin() + static_cast<std::ptrdiff_t>(firstKey),
                               allKeys.begin() + static_cast<std::ptrdiff_t>(firstKey + count));
        parsed.tracks.push_back(std::move(track));
    }

    for (std::uint32_t i = 0; i < stateCount; ++i) {
        std::uint32_t nameOffset = 0, clipIndex = 0, condition = 0;
        ModelAnimationState state;
        if (!reader.ReadU32(nameOffset) || !reader.ReadU32(clipIndex) ||
            !reader.ReadU32(condition) || !reader.ReadF32(state.blendDuration) ||
            !GetString(strings, nameOffset, state.name)) {
            error = "invalid or truncated animation controller state";
            return false;
        }
        if (condition > static_cast<std::uint32_t>(AnimationCondition::Eating)) {
            error = "animation controller condition is unsupported";
            return false;
        }
        state.clipIndex = clipIndex;
        state.condition = static_cast<AnimationCondition>(condition);
        parsed.states.push_back(std::move(state));
    }

    if (reader.Remaining() != 0u) {
        error = "model contains trailing bytes after the final section";
        return false;
    }
    if (!ModelValidator::Validate(parsed, error)) {
        return false;
    }
    output = std::move(parsed);
    return true;
}

} // namespace minefade
