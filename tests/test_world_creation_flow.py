#!/usr/bin/env python3
"""Regression checks for world creation, routing and save compatibility."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JAVA = ROOT / "app/src/main/java/com/minefade/nativeengine"
CPP = ROOT / "app/src/main/cpp"


def test_create_world_screen_uses_clear_minecraft_inspired_sections():
    home = (JAVA / "MinefadeHomeScreen.java").read_text(encoding="utf-8")
    assert 'String[] tabLabels = {"JUEGO", "MUNDO", "TRUCOS"};' in home
    assert 'final String[] modeNames = {"SUPERVIVENCIA", "CREATIVO", "AVENTURA"};' in home
    assert "final LinearLayout[] modeCards = new LinearLayout[3];" in home
    assert "updateModeCards(modeCards" in home
    assert 'actionButton("CREAR Y JUGAR"' in home
    assert '"VISTA PREVIA"' in home
    assert '"VOLVER A MUNDOS"' in home


def test_world_creation_seed_is_explicit_and_invalid_values_are_not_silently_changed():
    home = (JAVA / "MinefadeHomeScreen.java").read_text(encoding="utf-8")
    assert "ThreadLocalRandom.current().nextLong()" in home
    assert "Long.parseLong(rawSeed)" in home
    assert "Semilla no válida" in home
    assert "parsedSeed = rawSeed.isEmpty() ? System.currentTimeMillis() : Long.parseLong(rawSeed);" in home
    assert "worldManager.createWorld(" in home
    assert "selectedMode[0], cheats.isChecked(), chest.isChecked()" in home


def test_selected_world_path_reaches_native_engine_without_being_replaced():
    activity = (JAVA / "MainActivity.java").read_text(encoding="utf-8")
    surface = (JAVA / "MinefadeSurfaceView.java").read_text(encoding="utf-8")
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    assert "private void startWorld(WorldManager.WorldInfo world)" in activity
    assert "startGame(false, 0, world);" in activity
    assert "world == null ? getFilesDir().getAbsolutePath() : world.getPath()" in activity
    assert "NativeBridge.nativeSetStoragePath(storagePath);" in surface
    assert 'path.compare(path.size() - 8u, 8u, ".mfworld") == 0' in engine


def test_new_and_existing_worlds_have_a_safe_native_load_path():
    engine = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
    world = (CPP / "world/VoxelWorld.cpp").read_text(encoding="utf-8")
    assert "loadedExistingWorld = world_.Load(storagePath_, player_.position, inventory_.counts);" in engine
    assert "world_ = VoxelWorld(requestedWorldSeed_);" in engine
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
