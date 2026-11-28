#!/usr/bin/env python3
"""Regression checks for HUD identity, Android touch cancellation and GLES uploads."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
JAVA = ROOT / "app/src/main/java/com/minefade/nativeengine"
CPP = ROOT / "app/src/main/cpp"
SURFACE = (JAVA / "MinefadeSurfaceView.java").read_text(encoding="utf-8")
ACTIVITY = (JAVA / "MainActivity.java").read_text(encoding="utf-8")
RML = (CPP / "ui/RmlUiRuntime.cpp").read_text(encoding="utf-8")
RML_GL = (CPP / "ui/RmlUiOpenGLES.cpp").read_text(encoding="utf-8")
ENGINE = (CPP / "engine/Engine.cpp").read_text(encoding="utf-8")
RENDERER = (CPP / "render/WorldMeshRenderer.cpp").read_text(encoding="utf-8")
FIRST_PERSON = (CPP / "render/FirstPersonRenderer.cpp").read_text(encoding="utf-8")
FIRST_PERSON_H = (CPP / "render/FirstPersonRenderer.h").read_text(encoding="utf-8")
PLAYER_CPP = (CPP / "game/Player.cpp").read_text(encoding="utf-8")


def hud_document() -> str:
    start = RML.index('constexpr char kHudDocument[] = R"RML(')
    end = RML.index(')RML";', start)
    return RML[start:end]


def test_static_hud_element_ids_are_unique():
    markup = hud_document()
    ids = re.findall(r'\bid="([^"]+)"', markup)
    duplicates = sorted({value for value in ids if ids.count(value) > 1})
    assert not duplicates, f"Duplicate RmlUi element IDs: {duplicates}"
    assert ids.count("chat") == 1


def test_every_static_hud_button_is_registered_once_or_by_slot_loop():
    markup = hud_document()
    button_ids = set(re.findall(r'<button\b[^>]*\bid="([^"]+)"', markup, re.S))
    registrations = re.findall(r'AttachAction\("([^"]+)",\s*"([^"]+)"\)', RML)
    registered_ids = [element_id for element_id, _ in registrations]
    assert len(registered_ids) == len(set(registered_ids)), "Duplicate native action registration"
    missing = button_ids - set(registered_ids) - {f"slot-{index}" for index in range(9)}
    assert not missing, f"Static HUD buttons lack native actions: {sorted(missing)}"
    assert "for (int slot = 0; slot < 9; ++slot)" in RML
    assert "AttachAction(id.c_str(), id.c_str());" in RML
    assert registered_ids.count("chat") == 1


def test_surface_action_cancel_releases_all_motion_event_pointers():
    start = SURFACE.index("public boolean onTouchEvent(MotionEvent event)")
    end = SURFACE.index("/** Run a typed command", start)
    method = SURFACE[start:end]
    cancel = method.index("if (action == MotionEvent.ACTION_CANCEL)")
    cancel_body = method[cancel:method.index("final int pointerId =", cancel)]
    assert "event.getPointerCount()" in cancel_body
    assert "event.getPointerId(i)" in cancel_body
    assert "for (int i = 0; i < pointerCount; ++i)" in cancel_body
    assert "NativeBridge.nativeTouch(3, pointerIds[i], pointerXs[i], pointerYs[i]);" in cancel_body


def test_fallback_controls_route_multiple_pointers_through_one_parent_dispatcher():
    assert "overlay.setOnTouchListener((view, event) -> dispatchFallbackOverlayTouch(event, overlay));" in ACTIVITY
    assert "private boolean dispatchFallbackOverlayTouch(MotionEvent event, View overlay)" in ACTIVITY
    assert "private View findFallbackTarget(View overlay, float x, float y)" in ACTIVITY
    assert "child.getLocationOnScreen(location)" in ACTIVITY
    assert "fallbackPointers.put(pointerId, state)" in ACTIVITY
    assert "fallbackPointers.get(pointerId)" in ACTIVITY
    assert "state.camera" in ACTIVITY
    assert "surfaceView.dispatchFallbackTouch(0, pointerId, x, y)" in ACTIVITY
    assert "surfaceView.dispatchFallbackTouch(2, pointerId, state.lastX, state.lastY)" in ACTIVITY
    assert "surfaceView.dispatchFallbackTouch(1, pointerId, x, y)" in ACTIVITY
    assert "surfaceView.dispatchFallbackTouch(3, entry.getKey()" in ACTIVITY
    assert "surfaceView.dispatchFallbackAction(tag, true)" in ACTIVITY
    assert "surfaceView.dispatchFallbackAction(state.holdAction, false)" in ACTIVITY
    for direction, action in (("up", 6), ("down", 7), ("left", 8), ("right", 9)):
        assert f"{direction}.setTag({action})" in ACTIVITY
    assert "crouch.setTag(1)" in ACTIVITY
    assert "jump.setTag(0)" in ACTIVITY and "attack.setTag(10)" in ACTIVITY
    assert "setFallbackChildrenPassive(overlay)" in ACTIVITY
    assert "if (view.getParent() != null)" in ACTIVITY


def test_rmlui_texture_upload_saves_resets_and_restores_all_unpack_state():
    start = RML_GL.index("Rml::TextureHandle RmlUiOpenGLES::CreateTexture(")
    end = RML_GL.index("void RmlUiOpenGLES::ReleaseTexture", start)
    method = RML_GL[start:end]
    for name in (
        "GL_UNPACK_ALIGNMENT",
        "GL_UNPACK_ROW_LENGTH",
        "GL_UNPACK_SKIP_PIXELS",
        "GL_UNPACK_SKIP_ROWS",
        "GL_PIXEL_UNPACK_BUFFER_BINDING",
    ):
        assert f"glGetIntegerv({name}," in method
    for name in (
        "GL_UNPACK_ALIGNMENT",
        "GL_UNPACK_ROW_LENGTH",
        "GL_UNPACK_SKIP_PIXELS",
        "GL_UNPACK_SKIP_ROWS",
    ):
        if name == "GL_UNPACK_ALIGNMENT":
            assert f"glPixelStorei({name}, 1);" in method
        else:
            assert f"glPixelStorei({name}, 0);" in method
        assert name + ", previous" in method
    assert "glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);" in method


def test_native_touch_cancel_clears_pointer_state_without_place_or_mine():
    start = ENGINE.index("void Engine::Touch(int action")
    end = ENGINE.index("void Engine::HandleUiAction", start)
    method = ENGINE[start:end]
    assert "constexpr int kCancel = 3;" in method
    assert "action == kUp || action == kCancel" in method
    assert "touches_.erase(found);" in method
    assert "action == kUp && !state.moved" in method



def test_rmlui_virtual_mouse_is_owned_by_the_pointer_that_pressed_the_hud():
    header = (CPP / "ui/RmlUiRuntime.h").read_text(encoding="utf-8")
    runtime = RML
    assert "bool Touch(int action, int pointerId, float x, float y);" in header
    assert "int mousePointerId_{-1};" in header
    assert "mousePointerId_ >= 0 && mousePointerId_ != pointerId" in runtime
    assert "mousePointerId_ != pointerId || !mouseCaptured_" in runtime
    assert "mousePointerId_ = -1;" in runtime
    assert "ui_.Touch(action, pointerId, x, y)" in ENGINE


def test_non_owner_pointer_cannot_release_or_move_a_hud_button():
    owner_guard = RML.index("mousePointerId_ != pointerId || !mouseCaptured_")
    up_handler = RML.index("context_->ProcessMouseButtonUp", owner_guard)
    assert owner_guard < up_handler
    guard_body = RML[owner_guard:up_handler]
    assert "return true;" in guard_body



def test_secondary_pointer_over_hud_button_is_consumed_not_sent_to_world():
    owner = RML.index("mousePointerId_ >= 0 && mousePointerId_ != pointerId")
    normal_press = RML.index("context_->ProcessMouseButtonDown", owner)
    branch = RML[owner:normal_press]
    assert "context_->GetElementAtPoint" in branch
    assert 'hit->Closest("button")' in branch
    assert "return hit == nullptr || hit->Closest(\"button\") == nullptr;" in branch
    # The handler returns false for HUD buttons (consumed) and true only\n    # for non-UI touches; it must not synthesize a normal mouse-down here.\n    assert "context_->ProcessMouseButtonDown" not in branch



def test_direction_pad_does_not_capture_invisible_gaps_and_crouch_is_centre_control():
    runtime = RML
    start = runtime.index("bool RmlUiRuntime::MovementVectorAt(")
    end = runtime.index("bool RmlUiRuntime::Touch(", start)
    movement = runtime[start:end]
    assert "if (!IsInMovementPad(x, y))" not in movement
    assert "return false;" in movement
    assert "bool RmlUiRuntime::CrouchButtonAt(" in movement
    assert '#movement-pad #crouch' in hud_document()
    assert '<button id="crouch">' in hud_document()
    touch = ENGINE[ENGINE.index("void Engine::Touch(int action"):ENGINE.index("void Engine::HandleUiAction")]
    assert "if (ui_.CrouchButtonAt(x, y))" in touch
    assert 'HandleUiAction("crouch");' in touch


def test_hotbar_resizes_and_left_handed_positions_update_live():
    assert "void RmlUiRuntime::ApplyResponsiveLayout()" in RML
    assert "slotWidth = std::clamp((availableWidthDp - fixedWidthDp) / kSlotCount" in RML
    assert "const float leftDp = std::max(0.0f, (surfaceWidthDp - barWidth) * 0.5f)" in RML
    assert "constexpr float kSafeMarginDp = 10.0f" in RML
    assert "constexpr float kSlotCount = 9.0f" in RML
    assert "margin-left: -150dp" not in RML
    assert "width: 34dp; min-width: 34dp; height: 36dp" in RML
    assert "#selected-item" in RML and "background: #171c22d9" in RML
    assert 'SetProperty("min-width", slotValue)' in RML
    assert "void RmlUiRuntime::SetLeftHanded(bool enabled)" in RML
    assert 'SetProperty("right", leftHanded_ ? "14dp" : "auto")' in RML
    assert 'SetProperty("left", leftHanded_ ? "22dp" : "auto")' in RML
    assert "final int slotWidthDp" in ACTIVITY


def test_progression_xp_and_achievement_counters_are_world_persisted():
    header = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
    bridge = (JAVA / "NativeBridge.java").read_text(encoding="utf-8")
    native = (CPP / "NativeEngine.cpp").read_text(encoding="utf-8")
    assert "progressionXp_" in header
    assert 'storagePath_ + ".progression"' in ENGINE
    assert "0x4D465052u" in ENGINE
    assert "Engine::ProgressionSnapshot() const" in ENGINE
    assert "AddProgressionXp" in ENGINE
    assert "nativeGetProgressionSnapshot" in bridge
    assert "nativeGetProgressionSnapshot" in native
    assert "requestProgressionSnapshot" in SURFACE
    assert "updateProgressionPreferences" in ACTIVITY
    assert 'achievement_blocks_mined' in ACTIVITY
    assert 'achievement_blocks_placed' in ACTIVITY
    assert "NIVEL " in (JAVA / "MinefadeHomeScreen.java").read_text(encoding="utf-8")


def test_transparent_triangles_are_sorted_across_chunk_boundaries():
    header = (CPP / "render/WorldMeshRenderer.h").read_text(encoding="utf-8")
    assert "const std::vector<ChunkMeshView>& chunkViews" in header
    assert "std::vector<WorldVertex> sortedTransparentVertices_" in header
    assert "kMaxSortedTriangles = 75000u" in RENDERER
    assert "triangles.push_back({dx * dx + dy * dy + dz * dz, triangle})" in RENDERER
    assert "a.distanceSquared > b.distanceSquared" in RENDERER
    assert "sortedTransparentVao_" in RENDERER
    assert "DrawTransparent(frustum, eye, worldChunkViews_)" in ENGINE


def test_android_hud_chat_is_not_duplicated_when_rmlui_is_live():
    assert "private TextView commandButton;" in ACTIVITY
    assert "commandButton.setVisibility(View.GONE)" in ACTIVITY
    assert "commandButton.setVisibility(View.VISIBLE)" in ACTIVITY
    assert "commandButton.setVisibility(View.GONE)" in ACTIVITY


def test_fallback_jump_and_crouch_match_main_hud_progression_behavior():
    start = ENGINE.index("void Engine::FallbackAction")
    end = ENGINE.index("void Engine::SetLastInitError", start)
    method = ENGINE[start:end]
    assert "crouching_ = !crouching_;" in method
    assert "ui_.SetCrouchEnabled(crouching_);" in method
    jump = method[method.index("case 0:"):method.index("case 2:")]
    assert "progressionJumps_" in jump
    assert "AddProgressionXp(1u)" in jump
    assert "if (!player_.grounded)" in jump


def test_first_person_viewmodel_is_independent_from_entity_and_drop_meshes():
    start = ENGINE.index("void Engine::RebuildEntityMesh()")
    end = ENGINE.index("void Engine::UploadItemMesh()", start)
    rebuild = ENGINE[start:end]
    assert "firstPersonRenderer_.Build(player_, selected, definition, leftHandedControls_)" in rebuild
    assert "firstPersonRenderer_.Clear();" in rebuild
    assert "AppendCube(entityMesh_, grip" not in rebuild
    assert "AppendAnimatedTexturedBlockCube(itemMesh_" not in rebuild
    assert "player_.AppendMesh(entityMesh_)" in rebuild
    assert "droppedItems_.AppendMesh(itemMesh_, &frustum)" in rebuild
    assert "render/FirstPersonRenderer.cpp" in (CPP / "CMakeLists.txt").read_text(encoding="utf-8")


def test_first_person_arm_reuses_player_appearance_and_has_distinct_item_models():
    assert "Player::FirstPersonSkinColor()" in PLAYER_CPP
    assert "Player::FirstPersonSleeveColor()" in PLAYER_CPP
    assert "player.FirstPersonSkinColor()" in FIRST_PERSON
    assert "player.FirstPersonSleeveColor()" in FIRST_PERSON
    for kind in ("_pickaxe", "_axe", "_shovel", "_sword", "_hoe", "stick"):
        assert kind in FIRST_PERSON
    assert "heldAtlasVertices_" in FIRST_PERSON
    assert "AppendTexturedBlockCube(heldAtlasVertices_" in FIRST_PERSON
    assert "AppendSolidBox(armVertices_" in FIRST_PERSON
    assert "std::vector<WorldVertex> armVertices_" in FIRST_PERSON_H


def test_first_person_pass_draws_after_world_transparency_and_disappears_in_third_person():
    render = ENGINE[ENGINE.index("void Engine::Render()"):ENGINE.index("void Engine::Touch")]
    transparent = render.index("worldRenderer_.DrawTransparent(frustum, eye, worldChunkViews_)")
    viewmodel = render.index("firstPersonRenderer_.DrawHeldAtlas()")
    arm = render.index("firstPersonRenderer_.DrawArm()")
    assert transparent < viewmodel < arm
    assert "const bool hasFirstPerson = !thirdPerson_ && firstPersonRenderer_.HasContent()" in render
    assert "firstPersonRenderer_.Upload()" in render
    assert "firstPersonRenderer_.Shutdown()" in ENGINE
    assert "glUniformMatrix4fv(mvpLocation_, 1, GL_FALSE, viewModelProjection.v.data())" in render
    assert "glDepthMask(GL_FALSE)" not in render[viewmodel:arm + 80]


def test_selected_item_label_is_temporary_and_selection_source_stays_inventory_slot():
    assert "inventory_.selectedSlot != lastQuickbarSelection_" in ENGINE
    assert "selectedItemToastUntil_ = ElapsedSeconds() + 1.25" in ENGINE
    assert "ElapsedSeconds() < selectedItemToastUntil_" in ENGINE
    assert "ui_.SetSelectedItem({});" in ENGINE
    assert "quickbarSelectionChanged" in ENGINE


def test_first_person_viewmodel_uses_only_confirmed_gameplay_action_events():
    assert "firstPersonActionKind_ = 1;" in ENGINE
    assert "firstPersonActionKind_ = 2;" in ENGINE
    assert "firstPersonActionKind_ = 3;" in ENGINE
    assert "world_.BreakRay(" in ENGINE
    assert "world_.PlaceRay(" in ENGINE
    assert "inventory_.ConsumeSelected(1u)" in ENGINE
    render = ENGINE[ENGINE.index("void Engine::Render()"):ENGINE.index("void Engine::Touch")]
    assert "actionElapsed < 0.24f" in render
    assert "std::sin(phase * kPi)" in render
    assert "actionAngle = -0.48f * envelope" in render
    assert "actionAngle = 0.34f * envelope" in render
    assert "actionLift = 0.07f * envelope" in render


if __name__ == "__main__":
    checks = [value for name, value in globals().items()
              if name.startswith("test_") and callable(value)]
    for check in checks:
        check()
    print(f"Minefade HUD/touch/GLES regressions passed ({len(checks)} checks).")


def test_android_fallback_hud_and_inventory_are_bound_to_real_engine_state():
    engine_header = (CPP / "engine/Engine.h").read_text(encoding="utf-8")
    native_bridge = (JAVA / "NativeBridge.java").read_text(encoding="utf-8")
    native = (CPP / "NativeEngine.cpp").read_text(encoding="utf-8")
    surface = (JAVA / "MinefadeSurfaceView.java").read_text(encoding="utf-8")
    assert "std::string InventorySnapshot() const;" in engine_header
    assert "std::string HudSnapshot() const;" in engine_header
    assert "void SelectInventorySlot(int slot);" in engine_header
    assert "Inventory::kSlotCount" in ENGINE and "nameEs" in ENGINE
    assert "targetBlockProgress_" in engine_header and "SetTargetBlock(" in ENGINE
    assert "nativeGetInventorySnapshot" in native_bridge and "nativeGetHudSnapshot" in native_bridge
    assert "nativeSelectInventorySlot" in native_bridge
    assert "NativeBridge_nativeGetInventorySnapshot" in native
    assert "NativeBridge_nativeGetHudSnapshot" in native
    assert "NativeBridge_nativeSelectInventorySlot" in native
    assert "requestInventorySnapshot" in surface and "requestHudSnapshot" in surface
    assert "createBlockIcon(int tileIndex)" in surface
    assert "((y0 + y) * ATLAS_WIDTH + x0 + x) * 4" in surface
    assert "FALLBACK_TAG_QUICKBAR_BASE" in ACTIVITY
    assert "slot.setCompoundDrawables(null, drawable, null, null)" in ACTIVITY
    assert "renderFallbackInventoryGrid" in ACTIVITY
    assert "surfaceView.selectInventorySlot(slotIndex)" in ACTIVITY
    assert "fallbackHealthBar.setProgress(health)" in ACTIVITY
    assert "fallbackHungerBar.setProgress(hunger)" in ACTIVITY
    assert "fallbackXpBar.setProgress(xp)" in ACTIVITY
    assert 'crosshair = statusText("+", 25' in ACTIVITY
    assert "fallbackTargetLabel" in ACTIVITY


def test_safe_rml_document_routes_to_full_android_recovery_overlay():
    header = (CPP / "ui/RmlUiRuntime.h").read_text(encoding="utf-8")
    runtime = (CPP / "ui/RmlUiRuntime.cpp").read_text(encoding="utf-8")
    assert "bool usingSafeDocument_{false};" in header
    assert "return ready_ && !usingSafeDocument_;" in header
    assert "usingSafeDocument_ = true;" in runtime
    assert "return !usingSafeDocument_;" in runtime
    assert "if (!ready_ || context_ == nullptr || usingSafeDocument_) return;" in runtime
    assert "if (!ready_ || context_ == nullptr || usingSafeDocument_) return true;" in runtime
    assert "if (usingSafeDocument_) return false;" in runtime
    assert "Se activaron los controles" not in ACTIVITY
    assert "HUD Android completo activado." in ACTIVITY
