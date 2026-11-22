#!/usr/bin/env python3
"""Phase 2/3 integration checks for survival, data mods and LAN compatibility."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CPP = ROOT / "app/src/main/cpp"
JAVA = ROOT / "app/src/main/java/com/minefade/nativeengine"
ENGINE = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
ENGINE_H = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
PLAYER = (CPP / "game/Player.cpp").read_text(encoding="utf-8")
PLAYER_H = (CPP / "game/Player.h").read_text(encoding="utf-8")
RML = (CPP / "ui/RmlUiRuntime.cpp").read_text(encoding="utf-8")
MODS = (JAVA / "ModManager.java").read_text(encoding="utf-8")
LAN = (JAVA / "LanMultiplayerService.java").read_text(encoding="utf-8")
MAIN = (JAVA / "MainActivity.java").read_text(encoding="utf-8")


def test_survival_stats_are_sidecar_data_not_mfvox_format_changes():
    assert "float playerHealth_{20.0f};" in ENGINE_H
    assert "float playerHunger_{20.0f};" in ENGINE_H
    assert 'storagePath_ + ".survival"' in ENGINE
    assert "0x4D465356u" in ENGINE
    assert "savedHealth >= 0.0f && savedHealth <= 20.0f" in ENGINE
    assert "savedHunger >= 0.0f && savedHunger <= 20.0f" in ENGINE
    assert "world_.Save(storagePath_, player_.position, inventory_.counts)" in ENGINE
    assert "MFVOX001/002/003" in ENGINE


def test_survival_loop_covers_fall_damage_hunger_regeneration_and_starvation():
    assert "incomingVerticalVelocity < -10.0f" in ENGINE
    assert "hungerExhaustion_ +=" in ENGINE
    assert "playerHunger_ >= 18.0f" in ENGINE
    assert "starvationTimer_ >= 8.0f" in ENGINE
    assert 'entity.kind != 4 && entity.kind != 5' in ENGINE
    assert "hostileDamageCooldown_ = 1.25f;" in ENGINE


def test_food_consumption_and_survival_hud_are_wired_to_real_inventory():
    assert 'action == "eat"' in ENGINE
    assert 'key == "carrots"' in ENGINE and 'key == "potatoes"' in ENGINE
    assert "inventory_.ConsumeSelected(1u)" in ENGINE
    assert 'AttachAction("eat", "eat")' in RML
    assert 'id="eat"' in RML
    assert "VIDA %.0f/20 · COMIDA %.0f/20" in ENGINE


def test_melee_combat_is_forward_limited_and_targets_hostiles_only():
    assert "bool EntitySystem::DamageNearest(" in PLAYER
    assert "if (entity.kind != 4 && entity.kind != 5) continue;" in PLAYER
    assert "if (facing < 0.72f) continue;" in PLAYER
    assert "target.health = std::max(0.0f, target.health - damage);" in PLAYER
    assert "bool DamageNearest(" in PLAYER_H
    assert 'action == "attack"' in ENGINE
    assert 'AttachAction("attack", "attack")' in RML
    assert 'id="attack"' in RML


def test_mfmod2_is_a_bounded_data_only_container():
    assert 'endsWith(".mfmod2")' in MODS
    assert 'name.equals("manifest.json") || name.equals("data/gameplay.mfmod")' in MODS
    assert r"""name.indexOf('\\')""" in MODS
    assert "entryCount > 3" in MODS
    assert "totalExpanded + count > 40960" in MODS
    assert "expectedHash.equals(sha256(gameplayBytes))" in MODS
    assert '!"false".equals(validated.settings.get("enabled"))' in MODS
    assert "org.json.JSONObject" in MODS
    assert "ZipInputStream" in MODS


def test_legacy_mfmod_and_installed_v2_payloads_are_both_loaded():
    assert 'endsWith(".mfmod")' in MODS
    assert 'new File(new File(file, "data"), "gameplay.mfmod")' in MODS
    assert "recursive_directory_iterator" in ENGINE
    assert 'entry.path().parent_path().filename() == "data"' in ENGINE
    assert 'packageRoot / "manifest.json"' in ENGINE


def test_worlds_pin_enabled_mod_fingerprint_without_silently_overwriting_it():
    assert "enabledModsFingerprint()" in MODS
    assert "pinOrCompareWorldMods(String worldPath)" in MODS
    assert 'new File(worldPath + ".mods")' in MODS
    assert "if (!pinned.equals(fingerprint))" in MODS
    assert "pinOrCompareWorldMods(world.getPath())" in MAIN


def test_lan_protocol_v3_requires_matching_content_on_both_sides():
    assert "PROTOCOL_VERSION = 3" in LAN
    assert 'service.setAttribute("content"' in LAN
    assert 'readNsdAttribute(info, "content")' in LAN
    assert "contentFingerprint.equals(game.contentFingerprint)" in LAN
    assert '"HELLO|" + PROTOCOL_VERSION + "|" + contentFingerprint + "|"' in LAN
    assert "helloFields.length != 4" in LAN
    assert "contentFingerprint.equals(helloFields[2])" in LAN
    assert '"WELCOME|" + PROTOCOL_VERSION + "|" + id + "|" + playerName + "|"' in LAN
    assert "contentFingerprint.equals(fields[4])" in LAN
    assert "setContentFingerprint(new ModManager(this).enabledModsFingerprint())" in MAIN



def test_tool_durability_is_material_based_and_persisted_outside_mfvox():
    assert "std::array<std::uint16_t, kSlotCount> toolDurability{};" in PLAYER_H
    assert "bool DamageSelectedItem(std::uint16_t amount = 1);" in PLAYER_H
    assert "MaxToolDurability" in PLAYER
    for material in ("_wooden_", "_stone_", "_copper_", "_iron_", "_gold_", "_diamond_", "_emerald_"):
        assert material in PLAYER
    assert 'storagePath_ + ".durability"' in ENGINE
    assert "0x4D464459u" in ENGINE
    assert "Inventory::kSlotCount" in ENGINE
    assert "inventory_.toolDurability = savedDurability;" in ENGINE
    assert "inventory_.DamageSelectedItem(1u)" in ENGINE


def test_tool_durability_moves_with_inventory_slots_and_has_visual_bars():
    assert "std::swap(inventory_.toolDurability[slot]" in ENGINE
    assert "SlotItemMaxDurability(std::size_t slot) const" in PLAYER_H
    assert "SlotItemDurability(std::size_t slot) const" in PLAYER_H
    assert "inventory-durability" in ENGINE
    assert "slot-durability" in ENGINE
    assert "Durabilidad " in ENGINE
    assert ".slot-durability" in RML and ".inventory-durability" in RML


def test_lan_fingerprint_errors_fail_closed_and_incompatible_games_are_filtered():
    assert "contentFingerprint = validFingerprint(fingerprint)" in LAN
    assert '? fingerprint.toLowerCase(Locale.ROOT) : ""' in LAN
    assert "Ignoring LAN game with different active mods" in LAN
    assert "!contentFingerprint.equals(game.contentFingerprint)" in LAN



def test_hostile_entities_spawn_at_night_with_a_population_cap():
    assert "hostileSpawnTimer_ >= 48.0f" in ENGINE
    assert "environment_.Daylight() < 0.22f" in ENGINE
    assert "hostileCount < 4u" in ENGINE
    assert "hostileSpawnSequence_" in ENGINE
    assert "entities_.SpawnEntity(anchor, kind, world_)" in ENGINE
    assert "hostileSpawnTimer_ = 0.0f;" in ENGINE


def test_loaded_tool_durability_is_clamped_to_its_material_maximum():
    assert "remaining = std::min(remaining, maximum);" in PLAYER
    assert "std::uint16_t SelectedItemMaxDurability() const;" in PLAYER_H
    assert "std::uint16_t SlotItemDurability(std::size_t slot) const;" in PLAYER_H


def test_installed_mfmod2_packages_are_revalidated_during_library_scan():
    assert "isValidInstalledV2Package(file, values)" in MODS
    assert "compareVersions(minimum, currentEngineVersion()) > 0" in MODS
    assert "expected.equals(sha256(payload))" in MODS

def test_mod_toggle_writes_valid_java_line_endings():
    method_start = MODS.index("public synchronized boolean setEnabled(")
    method_end = MODS.index("Installs a bounded", method_start)
    method = MODS[method_start:method_end]
    assert "new StringBuilder(\"# Minefade data mod v1\")" in method
    assert "data.append((char) 10);" in method
    assert "value.indexOf((char) 10) < 0" in method
    assert "value.indexOf((char) 13) < 0" in method
    assert "append((char) 10)" in method
    assert "indexOf('\\\\n')" not in method
    assert "indexOf('\\\\r')" not in method


if __name__ == "__main__":
    checks = [value for name, value in globals().items()
              if name.startswith("test_") and callable(value)]
    for check in checks:
        check()
    print(f"Minefade phase 2/3 integration checks passed ({len(checks)} checks).")


def test_crafting_is_atomic_and_checks_output_after_consumption():
    recipe = ENGINE[ENGINE.index("bool Engine::CraftRecipe("):ENGINE.index("std::string Engine::ExecuteCommand(")]
    assert "Inventory stagedInventory = inventory_;" in recipe
    assert "stagedInventory.ConsumeItem(ingredients[i].id, ingredients[i].amount)" in recipe
    assert "stagedInventory.Add(outputId, outputAmount)" in recipe
    assert "inventory_ = stagedInventory;" in recipe
    assert "inventory_.CanAdd(outputId, outputAmount)" not in recipe


def test_survival_building_and_redstone_recipes_are_exposed_and_implemented():
    recipe_ui = ENGINE[ENGINE.index("void Engine::RefreshCraftingUi()"):ENGINE.index("void Engine::SaveWorld()")]
    recipe_impl = ENGINE[ENGINE.index("bool Engine::CraftRecipe("):ENGINE.index("std::string Engine::ExecuteCommand(")]
    for name in (
        "torch", "chest", "furnace", "wooden_door", "ladder", "fence",
        "wooden_slab", "oak_stairs", "stone_stairs", "lever",
        "wooden_pressure_plate", "stone_pressure_plate", "wooden_button",
        "stone_button", "trapdoor", "standing_sign", "rail",
    ):
        assert 'addRecipe("' + name + '"' in recipe_ui
        assert 'recipe == "' + name + '"' in recipe_impl


def test_inventory_and_mobile_hud_labels_match_real_slot_counts():
    assert "INVENTARIO · 36 ESPACIOS" in RML
    assert "margin-left: -150dp" in RML
    assert "width: 30dp; min-width: 30dp" in RML
    assert "left: auto; bottom: 126dp" in RML
