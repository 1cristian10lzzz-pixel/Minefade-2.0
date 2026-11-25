#!/usr/bin/env python3
"""Regression checks for world creation, routing and save compatibility."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JAVA = ROOT / "app/src/main/java/com/minefade/nativeengine"
CPP = ROOT / "app/src/main/cpp"


def test_create_world_screen_uses_clear_minecraft_inspired_sections():
    home = (JAVA / "MinefadeHomeScreen.java").read_text(encoding="utf-8")
    assert 'String[] tabLabels = {"JUEGO", "MUNDO", "MÁS"};' in home
    assert 'final String[] modeNames = {"SUPERVIVENCIA", "CREATIVO", "AVENTURA"};' in home
    assert "final LinearLayout[] modeCards = new LinearLayout[3];" in home
    assert "updateModeCards(modeCards" in home
    assert 'actionButton("CREAR Y JUGAR"' in home
    assert '"VISTA PREVIA"' in home


def test_world_creation_seed_is_explicit_and_invalid_values_are_not_silently_changed():
    home = (JAVA / "MinefadeHomeScreen.java").read_text(encoding="utf-8")
    assert "ThreadLocalRandom.current().nextInt()" in home
    assert "Long.parseLong(rawSeed)" in home
    assert "Semilla no válida" in home
    assert "parsedSeed = rawSeed.isEmpty() ? System.currentTimeMillis() / 1000L : Long.parseLong(rawSeed);" in home
    assert "parsedSeed < Integer.MIN_VALUE || parsedSeed > 0xffffffffL" in home
    assert "worldManager.createWorld(" in home
    assert "selectedMode[0], cheats.isChecked(), chest.isChecked()," in home


def test_selected_world_path_reaches_native_engine_without_being_replaced():
    activity = (JAVA / "MainActivity.java").read_text(encoding="utf-8")
    surface = (JAVA / "MinefadeSurfaceView.java").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    assert "private void startWorld(WorldManager.WorldInfo world)" in activity
    assert "startGame(false, 0, world);" in activity
    assert "world == null ? getFilesDir().getAbsolutePath() : world.getPath()" in activity
    assert "NativeBridge.nativeSetStoragePath(storagePath);" in surface
    assert "boolean allowCheats, boolean starterChest, boolean flatWorld," in surface
    assert "this.flatWorld = flatWorld;" in surface
    assert "NativeBridge.nativeSetWorldOptions(gameMode, allowCheats, starterChest, worldSeed, flatWorld, difficulty, generateStructures);" in surface
    assert 'path.compare(path.size() - 8u, 8u, ".mfworld") == 0' in engine


def test_new_and_existing_worlds_have_a_safe_native_load_path():
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    world = (CPP / "world/VoxelWorld.cpp").read_text(encoding="utf-8")
    assert "loadedExistingWorld = world_.Load(storagePath_, player_.position, inventory_.counts);" in engine
    assert "world_ = VoxelWorld(requestedWorldSeed_, flatWorld_, generateStructures_);" in engine
    assert "world_.GenerateAround(8, 8, 0);" in engine
    assert "world_.Save(storagePath_, player_.position, inventory_.counts)" in engine
    assert 'std::memcmp(magic,"MFVOX001",8)==0' in world
    assert 'std::memcmp(magic,"MFVOX002",8)==0' in world
    assert 'std::memcmp(magic,"MFVOX003",8)==0' in world
    assert "constexpr char magic[8]={'M','F','V','O','X','0','0','3'};" in world


def test_legacy_world_migration_keeps_original_until_metadata_is_written():
    manager = (JAVA / "WorldManager.java").read_text(encoding="utf-8")
    branch = manager[manager.index("private WorldInfo read(File folder)"):
                     manager.index("private boolean write(WorldInfo info)")]
    write_check = branch.index("if (!write(migrated))")
    delete_source = branch.index("legacy.delete();")
    assert "world.mfworld.migration.tmp" in branch
    assert "output.getFD().sync();" in branch
    assert write_check < delete_source
    assert "if (createdDestination) migrated.worldFile.delete();" in branch


def test_switching_worlds_never_saves_previous_session_to_new_world_path():
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    header = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
    initialise = engine[engine.index("bool Engine::Initialise("):
                        engine.index("void Engine::Shutdown(bool saveWorld)")]
    shutdown = engine[engine.index("void Engine::Shutdown(bool saveWorld)"):]
    assert "void Shutdown(bool saveWorld = true);" in header
    assert "Shutdown(false);" in initialise
    assert "if (saveWorld && ready_) SaveWorld();" in shutdown
    assert "MainActivity has already paused and saved the previous world" in initialise


def test_world_options_are_staged_while_previous_world_is_still_ready():
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    header = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
    initialisation = engine[engine.index("bool Engine::Initialise("):
                            engine.index("void Engine::Shutdown(bool saveWorld)")]
    setter = engine[engine.index("void Engine::SetWorldOptions("):
                    engine.index("void Engine::SetModsDirectory(")]
    for field in ("pendingGameMode_", "pendingAllowCheats_",
                  "pendingStarterChestEnabled_", "pendingWorldSeed_"):
        assert field in header
        assert field in setter
        assert field in initialisation
    assert "if (ready_) return;" not in setter


def test_world_switch_is_blocked_until_native_save_finishes_successfully():
    bridge = (JAVA / "NativeBridge.java").read_text(encoding="utf-8")
    surface = (JAVA / "MinefadeSurfaceView.java").read_text(encoding="utf-8")
    activity = (JAVA / "MainActivity.java").read_text(encoding="utf-8")
    native = (CPP / "NativeEngine.cpp").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    header = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
    assert "public static native boolean nativePause();" in bridge
    assert "public boolean pauseNativeEngine()" in surface
    assert "saveSucceeded[0] = NativeBridge.nativePause();" in surface
    assert "if (!completed.await(5, TimeUnit.SECONDS))" in surface
    assert "return false;" in surface[surface.index("public boolean pauseNativeEngine()"):
                                       surface.index("/** Resume on the same GL thread")]
    assert "return minefade::GetEngine().Pause() ? JNI_TRUE : JNI_FALSE;" in native
    assert "bool SaveWorld();" in header and "bool Pause();" in header
    assert "const bool saved = SaveWorld();" in engine
    assert "return saved;" in engine[engine.index("bool Engine::Pause()"):
                                     engine.index("void Engine::Resume()")]
    assert activity.count("if (!surfaceView.pauseNativeEngine())") >= 2
    assert "No se cambiará de partida para proteger tus datos." in activity



def test_every_native_bridge_method_has_a_matching_jni_export():
    import re

    bridge = (JAVA / "NativeBridge.java").read_text(encoding="utf-8")
    native = (CPP / "NativeEngine.cpp").read_text(encoding="utf-8")
    java_methods = set(re.findall(
        r"public\s+static\s+native\s+[\w\[\]]+\s+(\w+)\s*\(",
        bridge,
    ))
    jni_methods = set(re.findall(
        r"Java_com_minefade_nativeengine_NativeBridge_(\w+)\s*\(",
        native,
    ))
    missing = sorted(java_methods - jni_methods)
    assert not missing, "NativeBridge methods missing JNI exports: " + ", ".join(missing)


def test_native_renderer_startup_and_failure_checks_are_exported():
    native = (CPP / "NativeEngine.cpp").read_text(encoding="utf-8")
    engine_header = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
    assert "Java_com_minefade_nativeengine_NativeBridge_nativeSetHudOptions" in native
    assert "minefade::GetEngine().SetHudOptions(showStats == JNI_TRUE);" in native
    assert "Java_com_minefade_nativeengine_NativeBridge_nativeConsumeEngineFailure" in native
    assert "gEngineFailure.exchange(false, std::memory_order_acq_rel)" in native
    assert "void SetHudOptions(bool showStats);" in engine_header



def test_world_options_are_persisted_and_passed_to_the_native_generator():
    manager = (JAVA / "WorldManager.java").read_text(encoding="utf-8")
    home = (JAVA / "MinefadeHomeScreen.java").read_text(encoding="utf-8")
    activity = (JAVA / "MainActivity.java").read_text(encoding="utf-8")
    surface = (JAVA / "MinefadeSurfaceView.java").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    world = (CPP / "world/VoxelWorld.cpp").read_text(encoding="utf-8")
    for field in ("difficulty", "generateStructures", "multiplayerEnabled"):
        assert f'json.put("{field}", info.' in manager
        assert f'json.opt' in manager and field in manager
        assert field in home
    assert "difficulty, generateStructures" in surface
    assert "int difficulty, boolean generateStructures," in surface
    assert "this.difficulty = Math.max(0, Math.min(3, difficulty));" in surface
    assert "this.generateStructures = generateStructures;" in surface
    assert "difficulty_ != 0" in engine
    assert "generateStructures_ && dryLand" in world
    assert "pendingDifficulty_ = std::clamp(difficulty, 0, 3);" in engine


def test_existing_world_lan_toggle_hosts_the_selected_save_and_clients_use_host_seed():
    home = (JAVA / "MinefadeHomeScreen.java").read_text(encoding="utf-8")
    activity = (JAVA / "MainActivity.java").read_text(encoding="utf-8")
    lan = (JAVA / "LanMultiplayerService.java").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    assert "onHostWorldMultiplayer(updated)" in home
    assert "if (world.multiplayerEnabled) startLanHost(world);" in activity
    assert 'service.setAttribute("seed", Long.toString(advertisedWorldSeed));' in lan
    assert "game.worldSeed" in activity
    assert "if (multiplayerSlot_ == 0 && hasWorldSavePath)" in engine
    assert "world_ = VoxelWorld(requestedWorldSeed_, flatWorld_, generateStructures_);" in engine


def test_world_thumbnails_capture_and_display_saved_camera_view():
    activity = (JAVA / "MainActivity.java").read_text(encoding="utf-8")
    surface = (JAVA / "MinefadeSurfaceView.java").read_text(encoding="utf-8")
    home = (JAVA / "MinefadeHomeScreen.java").read_text(encoding="utf-8")
    assert "saveCurrentWorldPreview();" in activity
    assert "surfaceView.captureWorldPreview(preview)" in activity
    assert "GLES20.glReadPixels" in surface
    assert 'new File(folder, "world-preview.jpg")' in home
    assert "ImageView.ScaleType.CENTER_CROP" in home


def test_world_creation_header_and_actions_use_compact_layout():
    home = (JAVA / "MinefadeHomeScreen.java").read_text(encoding="utf-8")
    assert 'TextView back = text("‹  SALIR", 10' in home
    assert 'pageRoot("", "")' in home
    assert 'String[] tabLabels = {"JUEGO", "MUNDO", "MÁS"};' in home
    assert 'new LinearLayout.LayoutParams(dp(82), -1)' in home
    assert 'new LinearLayout.LayoutParams(dp(164), -1)' in home



def test_existing_world_multiplayer_builds_chunks_near_the_saved_player():
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    assert "const bool isExistingHostWorld = loadedExistingWorld && multiplayerSlot_ == 0;" in engine
    assert "std::floor(player_.position.x)" in engine
    assert "std::floor(player_.position.z)" in engine
    assert "world_.GenerateAround(generationX, generationZ, 1);" in engine



def test_loaded_world_restores_terrain_generation_options_for_new_chunks():
    header = (CPP / "world/VoxelWorld.h").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    assert "void SetGenerationOptions(bool flatWorld, bool generateStructures) noexcept" in header
    assert "if (loadedExistingWorld) world_.SetGenerationOptions(flatWorld_, generateStructures_);" in engine
    assert engine.count("world_.SetGenerationOptions(flatWorld_, generateStructures_);") == 2



def test_quickbar_snapshot_is_a_jni_export_and_uses_real_inventory_slots():
    bridge = (JAVA / "NativeBridge.java").read_text(encoding="utf-8")
    native = (CPP / "NativeEngine.cpp").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    assert "nativeGetQuickbarSnapshot()" in bridge
    assert "Java_com_minefade_nativeengine_NativeBridge_nativeGetQuickbarSnapshot" in native
    assert "inventory_.counts[slot]" in engine
    assert "Inventory::kHotbarSlotCount" in engine



def test_fallback_ui_has_discrete_controls_real_quickbar_chat_and_crafting_commands():
    activity = (JAVA / "MainActivity.java").read_text(encoding="utf-8")
    lan = (JAVA / "LanMultiplayerService.java").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    assert "FrameLayout joystick" not in activity
    assert "attachFallbackHoldAction(up, 6);" in activity
    assert "attachFallbackHoldAction(down, 7);" in activity
    assert "attachFallbackHoldAction(left, 8);" in activity
    assert "attachFallbackHoldAction(right, 9);" in activity
    assert "showFallbackInventoryDialog()" in activity
    assert "showFallbackCraftingDialog()" in activity
    assert "private void updateFallbackQuickbar(String snapshot)" in activity
    assert "default void onChatMessage(String player, String message)" in lan
    assert 'line.startsWith("CHAT|")' in lan
    assert "public void sendChat(String rawMessage)" in lan
    assert 'verb == "inventario"' in engine
    assert 'verb == "fabricar"' in engine



def test_claim_protection_is_persisted_and_enforced_in_the_native_block_path():
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    voxel = (CPP / "world/VoxelWorld.cpp").read_text(encoding="utf-8")
    voxel_h = (CPP / "world/VoxelWorld.h").read_text(encoding="utf-8")
    claims = (CPP / "world/ProtectionManager.cpp").read_text(encoding="utf-8")
    cmake = (CPP / "CMakeLists.txt").read_text(encoding="utf-8")
    assert 'protection_.Load(storagePath_ + ".claims")' in engine
    assert 'protection_.Save()' in engine
    assert 'protection_.CanBuild(playerIdentity_' in engine
    assert 'protection_.CanUseContainer(playerIdentity_' in engine
    assert '"/claim id' in engine
    assert "GetPlacementTargetBlock" in voxel_h and "GetPlacementTargetBlock" in voxel
    assert "CanBuild(const std::string& player, int x, int z) const" in claims
    assert "MFCLAIMS1" in claims and "Overlaps(existing, claim)" in claims
    assert "ProtectionManager.cpp" in cmake


def test_lan_host_checks_persistent_player_identity_before_accepting_claim_block_edits():
    bridge = (JAVA / "NativeBridge.java").read_text(encoding="utf-8")
    surface = (JAVA / "MinefadeSurfaceView.java").read_text(encoding="utf-8")
    lan = (JAVA / "LanMultiplayerService.java").read_text(encoding="utf-8")
    native = (CPP / "NativeEngine.cpp").read_text(encoding="utf-8")
    assert "nativeSetPlayerIdentity" in bridge and "nativeCanModifyBlockForIdentity" in bridge
    assert "nativeSetPlayerIdentity(multiplayer == null" in surface
    assert "multiplayer.getLocalPlayerIdentity()" in surface
    assert "PROTOCOL_VERSION = 4" in lan
    assert "localPlayerIdentity" in lan and "validPlayerIdentity(helloFields[4])" in lan
    assert "NativeBridge.nativeCanModifyBlockForIdentity" in lan
    assert "BLOCK_DENIED" in lan
    assert "Java_com_minefade_nativeengine_NativeBridge_nativeCanModifyBlockForIdentity" in native



def test_crafting_uses_distinct_furnace_and_stonecutter_stations_and_expanded_recipes():
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    header = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
    activity = (JAVA / "MainActivity.java").read_text(encoding="utf-8")
    assert "std::array<Ingredient, 9> ingredients{};" in engine
    assert "bool needsFurnace = false;" in engine
    assert "bool needsStonecutter = false;" in engine
    assert "bool Engine::HasNearbyFurnace() const" in engine
    assert "bool Engine::HasNearbyStonecutter() const" in engine
    assert 'recipe == "piston"' in engine and 'recipe == "hopper"' in engine
    assert 'recipe == "glass"' in engine and 'recipe == "golden_rail"' in engine
    assert 'findId("minefade_raw_" + metal)' in engine
    assert "HasNearbyBlock(const std::string& blockKey) const;" in header
    assert "iron_bars" in activity and "redstone_lamp" in activity
