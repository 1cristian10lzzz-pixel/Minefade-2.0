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


def test_fallback_world_camera_touch_and_discrete_dpad_cancel_release():
    assert "overlay.setOnTouchListener((view, event) -> dispatchFallbackWorldTouch(event));" in ACTIVITY
    assert "FrameLayout joystick" not in ACTIVITY
    for direction, action in (("up", 6), ("down", 7), ("left", 8), ("right", 9)):
        assert f"attachFallbackHoldAction({direction}, {action});" in ACTIVITY
    assert "attachFallbackHoldAction(crouch, 1);" in ACTIVITY
    assert "state == MotionEvent.ACTION_POINTER_UP || state == MotionEvent.ACTION_UP" in ACTIVITY
    assert "ownerPointer[0] == event.getPointerId(index)" in ACTIVITY
    assert "state == MotionEvent.ACTION_CANCEL" in ACTIVITY
    assert "surfaceView.dispatchFallbackAction(action, false);" in ACTIVITY
    assert "surfaceView.dispatchFallbackTouch(mapped, event.getPointerId(index)" in ACTIVITY


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
    assert "slotWidth = std::clamp((availableDp - 28.0f) / 9.0f" in RML
    assert 'SetProperty("min-width", value)' in RML
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


if __name__ == "__main__":
    checks = [value for name, value in globals().items()
              if name.startswith("test_") and callable(value)]
    for check in checks:
        check()
    print(f"Minefade HUD/touch/GLES regressions passed ({len(checks)} checks).")
