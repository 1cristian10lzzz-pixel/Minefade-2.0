#!/usr/bin/env python3
"""Source-level regression checks for Android startup recovery and phase-2 navigation."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "app/src/main/java/com/minefade/nativeengine/MainActivity.java").read_text(encoding="utf-8")
SURFACE = (ROOT / "app/src/main/java/com/minefade/nativeengine/MinefadeSurfaceView.java").read_text(encoding="utf-8")
BRIDGE = (ROOT / "app/src/main/java/com/minefade/nativeengine/NativeBridge.java").read_text(encoding="utf-8")
ENGINE = (ROOT / "app/src/main/cpp/engine/Engine.cpp").read_text(encoding="utf-8")
RML = (ROOT / "app/src/main/cpp/ui/RmlUiRuntime.cpp").read_text(encoding="utf-8")
HOME = (ROOT / "app/src/main/java/com/minefade/nativeengine/MinefadeHomeScreen.java").read_text(encoding="utf-8")


def test_rmlui_failure_does_not_abort_a_valid_world():
    start = ENGINE.index("if (!ui_.Initialise(")
    ready = ENGINE.index("ready_ = true;", start)
    recovery = ENGINE[start:ready]
    assert "El HUD RmlUi no está disponible" in recovery
    assert "return failInitialisation" not in recovery
    assert "ready_ = true;" in ENGINE[ready:ready + 40]


def test_android_emergency_controls_and_selected_world_retry_are_connected():
    assert "private WorldManager.WorldInfo requestedWorld;" in MAIN
    assert "requestedWorld = world;" in MAIN
    assert "startGame(requestedMultiplayer, requestedPlayerSlot, requestedWorld)" in MAIN
    assert "showFallbackTouchControls();" in MAIN
    assert "dispatchFallbackWorldTouch" in MAIN
    assert "overlay.setOnTouchListener" in MAIN
    assert "nativeFallbackAction" in SURFACE
    assert "nativeIsUiReady" in SURFACE
    assert "nativeFallbackAction" in BRIDGE


def test_joystick_hit_box_is_shared_when_rmlui_is_unavailable():
    helper = RML.index("bool RmlUiRuntime::IsInMovementPad")
    touch = ENGINE.index("void Engine::Touch(int action")
    update = ENGINE.index("void Engine::HandleUiAction", touch)
    method = ENGINE[touch:update]
    movement = method.index("if (ui_.MovementVectorAt(x, y, moveX, moveY))")
    ui_touch = method.index("ui_.Touch(action, pointerId, x, y)")
    assert helper >= 0
    assert movement < ui_touch
    assert "state.movementPad = true;" in method
    assert "UpdateTouchMovement();" in method
    # RmlUi owns only the virtual mouse pointer; the engine keeps the shared
    # movement hit-test even when the RmlUi context cannot be initialized.
    rml_touch = RML.index("bool RmlUiRuntime::Touch(int action, int pointerId")
    rml_update = RML.index("void RmlUiRuntime::UpdateAndRender()", rml_touch)
    assert "mousePointerId_ >= 0 && mousePointerId_ != pointerId" in RML[rml_touch:rml_update]


def test_phase_two_navigation_and_layout_regressions():
    assert "LinearLayout moduleRail = column();" in HOME
    assert "moduleScroll.addView(moduleRail" in HOME
    assert "pageHeader.addView(search" in HOME
    assert "pageHeader.addView(importButton" in HOME
    assert "for (int col = 0; col < 2; col++)" in HOME
    assert "SERVIDORES PÚBLICOS" not in HOME


if __name__ == "__main__":
    test_rmlui_failure_does_not_abort_a_valid_world()
    test_android_emergency_controls_and_selected_world_retry_are_connected()
    test_joystick_hit_box_is_shared_when_rmlui_is_unavailable()
    test_phase_two_navigation_and_layout_regressions()
    print("Minefade Android startup recovery and phase-2 UI checks passed.")
