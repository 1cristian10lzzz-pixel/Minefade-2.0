#pragma once

#include "../game/Player.h"
#include "../ui/RmlUiRuntime.h"
#include "../world/VoxelWorld.h"
#include "../world/ProtectionManager.h"
#include "../render/WorldMeshRenderer.h"
#include "../render/FirstPersonRenderer.h"
#include "../entity/EntityVisualSystem.h"
#include "../mods/ModContentRegistry.h"

#include <GLES3/gl3.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace minefade {

class Engine {
public:
    bool Initialise(float displayDensity, const std::uint8_t* atlasPixels, std::size_t atlasBytes, int atlasWidth, int atlasHeight, const std::uint8_t* sheepModelBytes, std::size_t sheepModelSize, const std::uint8_t* sheepTextureRgba, std::size_t sheepTextureBytes);
    bool RestoreGlContext(float displayDensity, const std::uint8_t* atlasPixels, std::size_t atlasBytes, int atlasWidth, int atlasHeight, const std::uint8_t* sheepTextureRgba, std::size_t sheepTextureBytes);
    std::string LastInitError() const;
    bool IsUiReady() const;
    void FallbackAction(int action, bool pressed);
    void SetLastInitError(std::string message);
    void Shutdown(bool saveWorld = true);
    void Resize(int width, int height);
    void Render();
    void Touch(int action, int pointerId, float x, float y);
    bool Pause();
    void Resume();
    void SetStoragePath(std::string path);
    void SetPlayerIdentity(std::string identity);
    bool CanRemoteModifyBlockForIdentity(const std::string& identity, int x, int z) const;
    void SetWorldOptions(int gameMode, bool allowCheats, bool starterChest, std::uint32_t seed, bool flatWorld, int difficulty, bool generateStructures);
    void SetModsDirectory(std::string path);
    std::string ExecuteCommand(const std::string& command);
    std::string QuickbarSnapshot() const;
    std::string ProgressionSnapshot() const;
    std::string InventorySnapshot() const;
    std::string HudSnapshot() const;
    void SelectInventorySlot(int slot);
    bool ConsumeExitRequest();
    bool ConsumeChatRequest() noexcept {
        return chatRequested_.exchange(false, std::memory_order_acq_rel);
    }
    void SetMinimapShape(int shape);
    std::uint32_t ConsumeSoundEvents();
    void SetMultiplayerMode(bool enabled, int playerSlot);
    void SetGraphicsOptions(int quality, int renderDistanceChunks, bool waterWaves, float fieldOfView, bool contactShadows);
    void SetShaderOptions(bool lavaAnimations, bool leafWind, float leafWindStrength);
    void SetInputOptions(float sensitivityX, float sensitivityY, float joystickDeadzone, bool invertLookY, bool leftHanded, int controlMode, bool autoJump);
    void SetHudOptions(bool showStats);
    void SetAnimationOptions(int quality, bool enabled);
    void SetAvatarAppearance(const int* values, std::size_t count);
    void GetPlayerState(float output[6]) const;
    void SetRemotePlayers(const float* values, std::size_t floatCount);
    void ApplyRemoteBlockEdits(const int* values, std::size_t valueCount);
    std::vector<int> ConsumeLocalBlockEdits();

private:
    struct Mat4 { std::array<float, 16> v{}; };
    struct TouchState {
        float startX{0.0f};
        float startY{0.0f};
        float lastX{0.0f};
        float lastY{0.0f};
        double startedAt{0.0};
        bool uiHandled{false};
        bool joystick{false};
        bool movementPad{false};
        float movementX{0.0f};
        float movementY{0.0f};
        bool moved{false};
        bool mining{false};
    };

    static Mat4 Perspective(float fovRadians, float aspect, float nearPlane, float farPlane);
    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
    static Mat4 Multiply(const Mat4& a, const Mat4& b);
    static CameraFrustum ExtractFrustum(const Mat4& mvp);
    static GLuint CompileShader(GLenum type, const char* source);
    bool CreateWorldProgram();
    bool UploadBlockAtlas(const std::uint8_t* pixels, std::size_t bytes, int width, int height);
    bool UploadSheepTexture(const std::uint8_t* pixels, std::size_t bytes);
    void RebuildWorldMesh();
    void UploadWorldMesh();
    void UploadTransparentWorldMesh();
    void RebuildEntityMesh();
    void UploadEntityMesh();
    void UploadMaesEntityMesh();
    void UploadItemMesh();
    void HandleUiAction(const std::string& action);
    void CyclePerspective();
    void UpdateTouchMovement();
    void AddProgressionXp(std::uint32_t amount);
    bool SaveWorld();
    void LoadEnabledMods();
    void OpenStarterChest();
    void TakeStarterChestContents();
    void RefreshInventoryUi();
    void RefreshCraftingUi();
    bool HasNearbyBlock(const std::string& blockKey) const;
    bool HasNearbyCraftingTable() const;
    bool HasNearbyFurnace() const;
    bool HasNearbyStonecutter() const;
    bool CraftRecipe(const std::string& recipe);
    void UpdateMining(float deltaSeconds);
    void SetTargetBlock(const std::string& name, float progress);
    Vec3 ViewPosition() const;
    Vec3 ViewTarget() const;
    Vec3 ViewDirection() const;
    double ElapsedSeconds() const;

    GLuint worldProgram_{0};
    GLint mvpLocation_{-1};
    GLint lightLocation_{-1};
    GLint daylightLocation_{-1};
    GLint atlasLocation_{-1};
    GLint useAtlasLocation_{-1};
    GLint entityTextureLocation_{-1};
    GLint useEntityTextureLocation_{-1};
    GLint timeLocation_{-1};
    GLint qualityLocation_{-1};
    GLint waterWavesLocation_{-1};
    GLint lavaAnimationsLocation_{-1};
    GLint leafWindLocation_{-1};
    GLint leafWindStrengthLocation_{-1};
    GLint cameraLocation_{-1};
    GLint fogColorLocation_{-1};
    GLint fogNearLocation_{-1};
    GLint fogFarLocation_{-1};
    GLuint blockAtlasTexture_{0};
    GLuint sheepTexture_{0};
    GLuint entityVao_{0};
    GLuint entityVbo_{0};
    GLuint maesEntityVao_{0};
    GLuint maesEntityVbo_{0};
    GLuint itemVao_{0};
    GLuint itemVbo_{0};
    GLsizei entityVertexCount_{0};
    GLsizei maesEntityVertexCount_{0};
    GLsizei itemVertexCount_{0};
    int lastChunkX_{999999};
    int lastChunkZ_{999999};
    int width_{1};
    int height_{1};
    bool ready_{false};
    bool paused_{false};
    bool dead_{false};
    bool multiplayerMode_{false};
    int multiplayerSlot_{0};
    bool pendingMultiplayerMode_{false};
    int pendingMultiplayerSlot_{0};
    int pendingGameMode_{0};
    bool pendingAllowCheats_{false};
    bool pendingStarterChestEnabled_{false};
    std::uint32_t pendingWorldSeed_{0x4D464445u};
    bool pendingFlatWorld_{false};
    bool flatWorld_{false};
    int pendingDifficulty_{2};
    int difficulty_{2};
    bool pendingGenerateStructures_{true};
    bool generateStructures_{true};
    int graphicsQuality_{1};
    int pendingGraphicsQuality_{1};
    int renderDistanceChunks_{2};
    int pendingRenderDistanceChunks_{2};
    bool waterWaves_{true};
    bool lavaAnimations_{true};
    bool leafWind_{true};
    float leafWindStrength_{0.04f};
    bool pendingWaterWaves_{true};
    float fieldOfView_{68.0f};
    float pendingFieldOfView_{68.0f};
    bool contactShadows_{true};
    bool pendingContactShadows_{true};
    float lookSensitivityX_{1.0f};
    float pendingLookSensitivityX_{1.0f};
    float lookSensitivityY_{1.0f};
    float pendingLookSensitivityY_{1.0f};
    bool invertLookY_{false};
    bool pendingInvertLookY_{false};
    bool leftHandedControls_{false};
    bool pendingLeftHandedControls_{false};
    int controlMode_{0};
    bool autoJump_{true};
    bool pendingAutoJump_{true};
    float autoJumpCooldown_{0.0f};
    float joystickDeadzone_{0.12f};
    float pendingJoystickDeadzone_{0.12f};
    bool showHudStats_{true};
    bool pendingShowHudStats_{true};
    int minimapShape_{0};
    int animationQuality_{2};
    int pendingAnimationQuality_{2};
    bool animationsEnabled_{true};
    bool pendingAnimationsEnabled_{true};
    std::array<int, 6> avatarAppearance_{{0,0,0,0,0,0}};
    bool inventoryVisible_{false};
    int inventoryCursorSlot_{-1};
    bool craftingVisible_{false};
    bool pauseVisible_{false};
    bool thirdPerson_{false};
    bool thirdPersonFront_{false};
    std::array<bool, 4> fallbackMovementButtons_{{false, false, false, false}};
    bool crouching_{false};
    int gameMode_{0};
    bool allowCheats_{false};
    float playerHealth_{20.0f};
    float playerHunger_{20.0f};
    // Per-world progression is persisted separately to preserve legacy MFVOX save formats.
    std::uint32_t progressionXp_{0u};
    std::uint32_t progressionBlocksMined_{0u};
    std::uint32_t progressionBlocksPlaced_{0u};
    std::uint32_t progressionItemsCollected_{0u};
    std::uint32_t progressionJumps_{0u};
    bool progressionWorldEntered_{false};
    float hungerExhaustion_{0.0f};
    float hungerDrainTimer_{0.0f};
    float starvationTimer_{0.0f};
    float regenerationTimer_{0.0f};
    float hostileDamageCooldown_{0.0f};
    float hostileSpawnTimer_{0.0f};
    std::uint32_t hostileSpawnSequence_{0u};
    bool starterChestEnabled_{false};
    bool starterChestOpen_{false};
    Vec3 starterChestPosition_{};
    std::uint32_t requestedWorldSeed_{0x4D464445u};
    std::string modsDirectory_;
    int modSheepSpawns_{0};
    int modCowSpawns_{0};
    int modBirdSpawns_{0};
    int modPigSpawns_{0};
    int modRabbitSpawns_{0};
    int modZombieSpawns_{0};
    int modSkeletonSpawns_{0};
    float modPlayerSpeed_{1.0f};
    float modDaySpeed_{1.0f};
    float modEntitySpeed_{1.0f};
    float modPlayerJump_{1.0f};
    float modCrouchSpeed_{0.46f};
    float modPassiveEntitySpeed_{1.0f};
    float modHostileEntitySpeed_{1.0f};
    float modFleeRadius_{6.0f};
    float modAlertRadius_{15.0f};
    float modSpawnRadius_{1.8f};
    float modPickupRadius_{1.65f};
    float modDropLifetime_{300.0f};
    float modMiningSpeed_{1.0f};
    int modDropQuantity_{1};
    int modEntityCap_{128};
    float modSpawnInterval_{0.0f};
    float modSpawnDistance_{18.0f};
    int modPassiveEntityCap_{24};
    std::array<std::uint32_t, 9> starterChestContents_{{
        1064u, 5064u, 64016u, 6004u, 3064u, 0u, 0u, 0u, 0u
    }};
    std::string storagePath_;
    std::string pendingPlayerIdentity_{"local-device"};
    std::string playerIdentity_{"local-device"};
    ProtectionManager protection_;
    VoxelWorld world_;
    Player player_;
    Inventory inventory_;
    DroppedItemSystem droppedItems_;
    EntitySystem entities_;
    EntityVisualSystem entityVisuals_;
    EnvironmentSystem environment_;
    RmlUiRuntime ui_;
    // Scratch output is kept empty in normal play: BuildMesh updates only the
    // cache, and the renderer reads the per-chunk vectors directly.
    std::vector<WorldVertex> meshBuildScratch_;
    std::vector<ChunkMeshView> worldChunkViews_;
    WorldMeshRenderer worldRenderer_;
    FirstPersonRenderer firstPersonRenderer_;
    std::uint16_t firstPersonItemId_{0xffffu};
    double firstPersonSwitchStarted_{0.0};
    double firstPersonActionStarted_{0.0};
    int firstPersonActionKind_{0};
    std::uint16_t lastQuickbarSelection_{0xffffu};
    double selectedItemToastUntil_{0.0};
    std::vector<WorldVertex> entityMesh_;
    std::vector<WorldVertex> maesEntityMesh_;
    std::vector<WorldVertex> itemMesh_;
    std::vector<float> remotePlayers_;
    std::vector<int> pendingBlockEdits_;
    std::unordered_map<int, TouchState> touches_;
    int miningPointerId_{-1};
    Vec3 miningBlock_{};
    BlockId miningBlockType_{BlockId::Air};
    float miningElapsed_{0.0f};
    float miningRequired_{0.0f};
    std::string targetBlockName_;
    float targetBlockProgress_{0.0f};
    float footstepElapsed_{0.0f};
    float chunkMeshAccumulator_{0.0f};
    float entityMeshAccumulator_{0.0f};
    float itemMeshAccumulator_{0.0f};
    float minimapAccumulator_{0.0f};
    std::string pickupToastText_;
    std::chrono::steady_clock::time_point pickupToastUntil_{};
    std::chrono::steady_clock::time_point lastFrame_{};
    std::chrono::steady_clock::time_point lastStats_{};
    std::chrono::steady_clock::time_point lastPerfLog_{};
    std::array<float, 120> frameTimeSamplesMs_{};
    std::size_t frameTimeSampleCursor_{0u};
    std::size_t frameTimeSampleCount_{0u};
    float lastChunkGenerationPumpMs_{0.0f};
    float lastMeshBuildMs_{0.0f};
    float lastGpuUploadMs_{0.0f};
    int framesSinceStats_{0};
    float fps_{0.0f};
    std::atomic_bool exitRequested_{false};
    std::atomic_bool chatRequested_{false};
    std::atomic<std::uint32_t> soundEvents_{0};
    // Kept after GL cleanup so Java can show the exact native startup failure.
    std::string lastInitError_;
};

Engine& GetEngine();

} // namespace minefade
