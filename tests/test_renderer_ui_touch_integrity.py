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


def test_fallback_world_and_joystick_cancel_release_all_pointers():
    assert ACTIVITY.count("action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_CANCEL") >= 2
    assert ACTIVITY.count("action == MotionEvent.ACTION_CANCEL ? 3 : 2") >= 2
    assert ACTIVITY.count("event.getPointerCount()") >= 2
    assert "dispatchFallbackTouch(pointerAction, event.getPointerId(i)" in ACTIVITY


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


if __name__ == "__main__":
    checks = [value for name, value in globals().items()
              if name.startswith("test_") and callable(value)]
    for check in checks:
        check()
    print(f"Minefade HUD/touch/GLES regressions passed ({len(checks)} checks).")
