#!/usr/bin/env python3
"""Regression checks for voxel face visibility and mobile chunk rendering."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "app/src/main/cpp"
WORLD = (CPP / "world/VoxelWorld.cpp").read_text(encoding="utf-8")
HEADER = (CPP / "world/VoxelWorld.h").read_text(encoding="utf-8")
RENDERER = (CPP / "render/WorldMeshRenderer.cpp").read_text(encoding="utf-8")
ENGINE = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
NATIVE_TESTS = (ROOT / "tests/WorldSmokeTests.cpp").read_text(encoding="utf-8")


def test_partial_opaque_voxels_do_not_cull_whole_cube_faces():
    assert "neighborDef->shape == BlockShape::Cube" in WORLD
    assert "(BlockTransparent | BlockFluid)" in WORLD
    assert "BlockId::StoneSlab" in NATIVE_TESTS
    assert "hasUpperEastFace()" in NATIVE_TESTS


def test_mobile_single_chunk_meshing_uses_immutable_background_snapshot():
    assert "pendingMeshBuild_ = std::async(std::launch::async" in WORLD
    assert "snapshot.BuildMesh(" in WORLD
    assert "current->revision == completed.sourceRevision" in WORLD
    assert "mutable std::future<AsyncMeshBuildResult> pendingMeshBuild_" in HEADER
    assert "asyncMeshWorld.BuildMesh(" in NATIVE_TESTS


def test_gpu_mesh_is_swapped_only_after_both_layers_upload():
    assert "GpuChunk staged{};" in RENDERER
    assert "if (!opaqueOk || !transparentOk)" in RENDERER
    assert "existing->second = staged;" in RENDERER
    failure_branch = RENDERER.split("if (!opaqueOk || !transparentOk)", 1)[1].split("staged.revision", 1)[0]
    assert "gpu.opaqueCount = 0" not in failure_branch
    assert "GL_DYNAMIC_DRAW" in RENDERER
    assert "staged.minY = view.minY;" in RENDERER
    assert "staged.maxY = view.maxY;" in RENDERER


def test_lod2_keeps_fluid_heightfield_and_transparent_water():
    assert "fluidHeights" in WORLD
    assert "def->shape == BlockShape::Fluid" in WORLD
    assert "AddFace(result.transparent, fluidTop, fluidShade" in WORLD
    assert "farWaterSurfaceVisible" in NATIVE_TESTS


def test_unified_dpad_and_minimap_shape_are_connected():
    header = (CPP / "ui/RmlUiRuntime.h").read_text(encoding="utf-8")
    rml = (CPP / "ui/RmlUiRuntime.cpp").read_text(encoding="utf-8")
    home = (ROOT / "app/src/main/java/com/minefade/nativeengine/MinefadeHomeScreen.java").read_text(encoding="utf-8")
    assert "MovementVectorAt(float x, float y" in header
    assert "state.movementPad = true;" in ENGINE
    assert "UpdateTouchMovement();" in ENGINE
    assert 'AttachAction("chat", "chat")' in rml
    assert '"minimap_shape"' in home
    assert 'settingChoice("Tipo de controles"' not in home


def test_world_creation_and_multiplayer_search_use_existing_world_and_lan_data():
    home = (ROOT / "app/src/main/java/com/minefade/nativeengine/MinefadeHomeScreen.java").read_text(encoding="utf-8")
    create = home[home.index("private void showCreateWorld()"):home.index("private void showModsPage()")]
    multi = home[home.index("private void showMultiplayerPage()"):home.index("private void showAchievementsPage()")]
    assert 'String[] tabLabels = {"JUEGO", "MUNDO", "MÁS"};' in create
    assert 'final String[] modeNames = {"SUPERVIVENCIA", "CREATIVO", "AVENTURA"};' in create
    assert "updateModeCards(modeCards" in create
    assert "worldManager.createWorld(" in create
    assert "seed.getText()" in create and "Long.parseLong(rawSeed)" in create
    assert "cheats.isChecked()" in create and "chest.isChecked()" in create
    assert 'actionButton("CREAR Y JUGAR"' in create
    assert "private void renderNearbyGames()" in multi
    assert "name.toLowerCase(java.util.Locale.ROOT).contains(query)" in multi
    assert "listener.onHostMultiplayer()" in multi
    assert "listener.onSearchMultiplayer()" in multi
    assert "listener.onJoinMultiplayer(game)" in multi


def test_avatar_editor_uses_one_model_and_persistent_style_actions():
    home = (ROOT / "app/src/main/java/com/minefade/nativeengine/MinefadeHomeScreen.java").read_text(encoding="utf-8")
    avatar = home[home.index("private void showAvatarPage()"):home.index("private void showSettingsPage()")]
    assert "new AvatarPreview(getContext())" in avatar
    assert 'actionButton("ALEATORIO"' in avatar
    assert 'actionButton("RESTABLECER"' in avatar
    assert "new AvatarOptionView(getContext(), avatarModuleIndex, choice)" in avatar


def test_renderer_draws_transparent_only_chunks_and_keeps_near_vegetation():
    assert "worldRenderer_.TransparentVertexCount() > 0" in ENGINE
    assert "result.lod>=2 &&" in WORLD
    assert "def->shape==BlockShape::CrossPlant" in WORLD


if __name__ == "__main__":
    test_partial_opaque_voxels_do_not_cull_whole_cube_faces()
    test_mobile_single_chunk_meshing_uses_immutable_background_snapshot()
    test_gpu_mesh_is_swapped_only_after_both_layers_upload()
    test_lod2_keeps_fluid_heightfield_and_transparent_water()
    test_unified_dpad_and_minimap_shape_are_connected()
    test_world_creation_and_multiplayer_search_use_existing_world_and_lan_data()
    test_avatar_editor_uses_one_model_and_persistent_style_actions()
    test_renderer_draws_transparent_only_chunks_and_keeps_near_vegetation()
    print("Minefade renderer/mobile chunk regression checks passed.")



def test_player_avatar_front_axis_matches_camera_forward_and_three_camera_modes_cycle():
    player = (CPP / "game/Player.cpp").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    header = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
    assert "const Vec3 forward{std::cos(yaw), 0.0f, std::sin(yaw)};" in player
    assert "const Vec3 right{std::sin(yaw), 0.0f, -std::cos(yaw)};" in player
    assert "const float cameraSide = thirdPersonFront_ ? 1.0f : -1.0f;" in engine
    assert "void Engine::CyclePerspective()" in engine
    assert "else if (!thirdPersonFront_)" in engine
    assert "thirdPersonFront_{false};" in header



def test_surface_caves_keep_a_deep_roof_and_do_not_create_huge_exposed_voids():
    world = (CPP / "world/VoxelWorld.cpp").read_text(encoding="utf-8")
    assert "const float caveGate=0.70f+static_cast<float>(y)*0.0007f;" in world
    assert "y>14&&y<height-10&&cavern>0.73f&&caveB>0.05f" in world
    assert "y>8&&y<height-6&&caveA>caveGate&&caveB>0.16f" in world



def test_world_spawn_mesh_is_ready_before_first_visible_frame():
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    assert "Build the spawn chunk synchronously" in engine
    assert "0, nullptr, nullptr, 0u, false" in engine


def test_egl_context_recreation_restores_resources_without_reloading_world():
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    header = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
    bridge = (ROOT / "app/src/main/java/com/minefade/nativeengine/NativeBridge.java").read_text(encoding="utf-8")
    native = (CPP / "NativeEngine.cpp").read_text(encoding="utf-8")
    surface = (ROOT / "app/src/main/java/com/minefade/nativeengine/MinefadeSurfaceView.java").read_text(encoding="utf-8")
    assert "RestoreGlContext" in header and "Engine::RestoreGlContext" in engine
    assert "nativeRestoreGlContext" in bridge and "nativeRestoreGlContext" in native
    assert "final boolean restoringSession = nativeSessionInitialised;" in surface
    assert "nativeReady = restoringSession" in surface
    assert "reopening the last saved world" in surface
    restore = engine.split("bool Engine::RestoreGlContext", 1)[1].split("void Engine::Shutdown", 1)[0]
    assert "worldRenderer_.Shutdown();" in restore
    assert "world_.Load(" not in restore
    assert "world_.GenerateAround(" not in restore


def test_reference_hud_inventory_and_crafting_visuals_keep_real_actions():
    rml = (CPP / "ui/RmlUiRuntime.cpp").read_text(encoding="utf-8")
    header = (CPP / "ui/RmlUiRuntime.h").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    assert 'id="vitals-armor"' in rml and 'id="vitals-health"' in rml
    assert 'id="vitals-hunger"' in rml and 'id="vitals-xp-fill"' in rml
    assert "void RmlUiRuntime::SetVitals" in rml and "SetVitals(" in header
    assert "crafting-input-grid" in rml and "craft-output" in rml
    assert 'AttachAction("crafting-open", "crafting-open")' in rml
    assert "CraftRecipe(" in engine
    assert 'SetProperty("height", "42dp")' in rml
    assert 'wideLandscape ? "row" : "column"' in rml


def test_main_menu_style_refresh_preserves_navigation_handlers():
    home = (ROOT / "app/src/main/java/com/minefade/nativeengine/MinefadeHomeScreen.java").read_text(encoding="utf-8")
    assert "EXPLORA  ·  CREA  ·  JUEGA" in home
    assert '() -> showWorlds()' in home
    assert '() -> showSection("multiplayer")' in home
    assert '() -> showSection("avatar")' in home
    assert '() -> showSection("settings")' in home
