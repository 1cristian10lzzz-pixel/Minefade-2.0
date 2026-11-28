#include "RmlUiRuntime.h"
#include "../blocks/BlockAtlasManifest.h"

#include <android/log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace minefade {
namespace {
constexpr const char* kTag = "MinefadeRmlUi";
const auto kStartTime = std::chrono::steady_clock::now();
std::string gLastRmlLogMessage;

// RmlUi uses GLES state directly. Isolate that state once per UI frame so
// blend/scissor/viewport/texture/VAO changes cannot leak into the world pass.
class ScopedRmlUiGlState {
public:
    ScopedRmlUiGlState() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray_);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer_);
        glGetIntegerv(GL_VIEWPORT, viewport_.data());
        glGetIntegerv(GL_SCISSOR_BOX, scissorBox_.data());
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureUnit0_);
        glActiveTexture(static_cast<GLenum>(activeTexture_));

        glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb_);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb_);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha_);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha_);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb_);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha_);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc_);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteMask_);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorWriteMask_.data());

        blendEnabled_ = glIsEnabled(GL_BLEND);
        depthEnabled_ = glIsEnabled(GL_DEPTH_TEST);
        cullEnabled_ = glIsEnabled(GL_CULL_FACE);
        scissorEnabled_ = glIsEnabled(GL_SCISSOR_TEST);
    }

    ~ScopedRmlUiGlState() {

        glUseProgram(static_cast<GLuint>(program_));
        glBindVertexArray(static_cast<GLuint>(vertexArray_));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(arrayBuffer_));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureUnit0_));
        glActiveTexture(static_cast<GLenum>(activeTexture_));

        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glScissor(scissorBox_[0], scissorBox_[1], scissorBox_[2], scissorBox_[3]);
        glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb_),
                            static_cast<GLenum>(blendDstRgb_),
                            static_cast<GLenum>(blendSrcAlpha_),
                            static_cast<GLenum>(blendDstAlpha_));
        glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb_),
                                static_cast<GLenum>(blendEquationAlpha_));
        glDepthFunc(static_cast<GLenum>(depthFunc_));
        glDepthMask(depthWriteMask_);
        glColorMask(colorWriteMask_[0], colorWriteMask_[1],
                    colorWriteMask_[2], colorWriteMask_[3]);
        SetEnabled(GL_BLEND, blendEnabled_);
        SetEnabled(GL_DEPTH_TEST, depthEnabled_);
        SetEnabled(GL_CULL_FACE, cullEnabled_);
        SetEnabled(GL_SCISSOR_TEST, scissorEnabled_);
    }

    ScopedRmlUiGlState(const ScopedRmlUiGlState&) = delete;
    ScopedRmlUiGlState& operator=(const ScopedRmlUiGlState&) = delete;

private:
    static void SetEnabled(GLenum capability, GLboolean enabled) {
        if (enabled == GL_TRUE) glEnable(capability);
        else glDisable(capability);
    }

    GLint program_{0};
    GLint vertexArray_{0};
    GLint arrayBuffer_{0};
    GLint activeTexture_{GL_TEXTURE0};
    GLint textureUnit0_{0};
    GLint blendSrcRgb_{GL_ONE};
    GLint blendDstRgb_{GL_ZERO};
    GLint blendSrcAlpha_{GL_ONE};
    GLint blendDstAlpha_{GL_ZERO};
    GLint blendEquationRgb_{GL_FUNC_ADD};
    GLint blendEquationAlpha_{GL_FUNC_ADD};
    GLint depthFunc_{GL_LESS};
    std::array<GLint, 4> viewport_{0, 0, 1, 1};
    std::array<GLint, 4> scissorBox_{0, 0, 1, 1};
    std::array<GLboolean, 4> colorWriteMask_{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean depthWriteMask_{GL_TRUE};
    GLboolean blendEnabled_{GL_FALSE};
    GLboolean depthEnabled_{GL_FALSE};
    GLboolean cullEnabled_{GL_FALSE};
    GLboolean scissorEnabled_{GL_FALSE};
};

constexpr char kHudDocument[] = R"RML(
<rml>
<head>
<title>Minefade HUD</title>
<style>
body { margin: 0; padding: 0; width: 100%; height: 100%; overflow: hidden; font-family: Roboto; color: #f3f6f8; }
#topbar { position: absolute; z-index: 8; top: 8dp; left: 8dp; right: 8dp; height: 40dp; display: flex; align-items: center; justify-content: flex-end; pointer-events: none; }
#topbar button { pointer-events: auto; }
#brand { display: none; }
#stats { color: #f3f8fb; font-size: 9dp; background: #1c344acb; padding: 7dp 10dp; border: 1dp #d7e8ef66; border-radius: 10dp; text-shadow: none; }
#minimap { position: absolute; z-index: 9; top: 54dp; left: 10dp; width: 98dp; padding: 5dp; display: flex; flex-direction: column; opacity: 1; visibility: visible; background: #10243aee; border: 1dp #d7e8efcc; border-radius: 8dp; box-shadow: none; }
#minimap-grid { width: 88dp; height: 88dp; display: flex; flex-direction: row; flex-wrap: wrap; gap: 1dp; border: 1dp #e2f0f4aa; border-radius: 5dp; overflow: hidden; background: #173047; }
#minimap-north { position: absolute; right: 4dp; top: 3dp; width: 15dp; height: 15dp; text-align: center; color: #ffffff; font-size: 9dp; background: #142638cc; border-radius: 8dp; }
.map-tile { width: 11dp; height: 11dp; border-radius: 1dp; }
.map-low { background: #6c8064; }
.map-mid { background: #52805b; }
.map-high { background: #8b9185; }
.map-water { background: #326f9a; }
.map-player { background: #d9ed94; border: 1dp #f8ffe8; border-radius: 5dp; }
#minimap-title { color: #dcebf5; font-size: 8dp; font-weight: bold; text-align: center; margin-bottom: 3dp; }
#minimap-coords { margin-top: 3dp; text-align: center; color: #d4e4e9; font-size: 8dp; }
.actions { display: flex; flex-direction: row; align-items: center; gap: 6dp; }
button { color: #f7fbff; background: #142638bb; border: 1dp #d7e8ef66; border-radius: 9dp; padding: 6dp 9dp; font-size: 11dp; }
button, .slot, #minimap, .panel, #chat-message { transition: background-color 0.14s linear, border-color 0.14s linear, opacity 0.18s linear; }
.slot { transition: background-color 0.14s linear, border-color 0.14s linear; }
#target-progress-fill { transition: width 0.10s linear; }
#pickup { transition: opacity 0.16s linear; }
button:hover { background: #365a77; }
button:active { background: #47789b; }
#crosshair { display: block; position: absolute; z-index: 5; left: 50%; top: 50%; width: 16dp; height: 16dp; margin-left: -8dp; margin-top: -8dp; pointer-events: none; }
#crosshair-v { position: absolute; left: 7dp; top: 0dp; width: 2dp; height: 16dp; background: #ffffffee; border: 1dp #152331aa; }
#crosshair-h { position: absolute; left: 0dp; top: 7dp; width: 16dp; height: 2dp; background: #ffffffee; border: 1dp #152331aa; }
#crosshair-center { position: absolute; left: 6dp; top: 6dp; width: 4dp; height: 4dp; border-radius: 2dp; background: #e7f6ad; }
#hint { display: none; }
.panel { position: absolute; left: 50%; top: 50%; width: 310dp; margin-left: -155dp; margin-top: -112dp; padding: 16dp; background: #101d2bf5; border: 1dp #8ca9bd99; border-radius: 16dp; display: none; }
.panel h1 { font-size: 20dp; margin: 0dp 0dp 10dp 0dp; color: #ffffff; }
.panel p { font-size: 12dp; color: #c8d3db; margin: 3dp 0dp 12dp 0dp; }
.panel .actions { flex-wrap: wrap; }
#quickbar { position: absolute; z-index: 7; bottom: 10dp; left: 50%; margin-left: 0dp; display: flex; flex-direction: row; gap: 2.5dp; padding: 0dp; background: transparent; border: 0dp transparent; border-radius: 0dp; box-shadow: none; }
.slot { position: relative; width: 34dp; min-width: 34dp; height: 36dp; text-align: center; padding: 0dp; display: flex; align-items: center; justify-content: center; background: #30363dcc; border: 1dp #7e878fbb; border-radius: 2dp; color: #ffffff; font-size: 9dp; }
.slot:active { background: #59636b; }
.slot img { width: 26dp; height: 26dp; display: block; margin: auto; }
.slot .slot-count { position: absolute; right: 1dp; bottom: 0dp; color: #ffffff; font-size: 8dp; text-shadow: 0dp 1dp 2dp #000000; }
.slot-durability { position: absolute; left: 3dp; right: 3dp; bottom: 2dp; height: 3dp; background: #000000aa; border-radius: 2dp; overflow: hidden; }
.slot-durability span { display: block; height: 3dp; background: #9ad27d; }
.inventory-row { position: relative; flex-wrap: wrap; }
.inventory-row.inventory-cursor { border: 2dp #d9e9a4; background: #627d4f; }
.inventory-hint { color: #d3e1eb; font-size: 8dp; padding: 4dp 2dp; }
.inventory-durability { display: block; flex-basis: 100%; height: 3dp; margin-left: 26dp; margin-top: -2dp; background: #000000aa; border-radius: 2dp; overflow: hidden; }
.inventory-durability span { display: block; height: 3dp; background: #9ad27d; }
.selected-durability { font-size: 8dp; color: #c9e3ad; margin-left: 5dp; }
.inventory-row { display: flex; align-items: center; gap: 7dp; color: #e0e8ed; font-size: 12dp; padding: 5dp 2dp; border-bottom: 1dp #43576a; }
.inventory-icon { width: 19dp; height: 19dp; }
#inventory, #pause, #perspective, #chat { width: 38dp; min-width: 38dp; height: 38dp; padding: 0dp; text-align: center; background: #142638cc; border: 1dp #d8e6ee77; border-radius: 11dp; }
#movement-pad { position: absolute; z-index: 6; left: 14dp; bottom: 54dp; width: 132dp; height: 132dp; border-radius: 18dp; background: transparent; border: none; pointer-events: none; }
.pad-arrow { position: absolute; width: 42dp; height: 42dp; display: flex; align-items: center; justify-content: center; text-align: center; color: #f5fbff; font-size: 21dp; font-weight: bold; text-shadow: 0dp 1dp 2dp #071321; border: 1dp #d7e8ef88; border-radius: 11dp; background: #1b3850dd; pointer-events: none; }
#move-up { left: 45dp; top: 2dp; }
#move-down { left: 45dp; bottom: 2dp; }
#move-left { left: 2dp; top: 45dp; }
#move-right { right: 2dp; top: 45dp; }
#jump { position: absolute; z-index: 7; right: 22dp; bottom: 60dp; width: 56dp; height: 56dp; padding: 2dp; text-align: center; border-radius: 18dp; background: #2f6479f2; border: 2dp #d9e9a4ee; color: #ffffff; font-size: 20dp; }
#attack { position: absolute; z-index: 7; right: 24dp; bottom: 126dp; width: 50dp; height: 50dp; padding: 1dp; text-align: center; border-radius: 16dp; background: #7d3d36ee; border: 2dp #f0c1a1cc; color: #fff7f0; font-size: 17dp; }
#movement-pad #crouch { position: absolute; z-index: 8; left: 45dp; top: 45dp; right: auto; bottom: auto; width: 42dp; height: 42dp; padding: 0dp; text-align: center; border-radius: 11dp; background: #20394cf0; border: 1dp #c3d8e8aa; color: #f5fbff; font-size: 15dp; pointer-events: auto; }
#inventory-panel { z-index: 9; left: 50%; top: 50%; width: 94%; height: 88%; margin-left: -47%; margin-top: -44%; padding: 7dp; background: #20344aef; border: 2dp #a9c1d4aa; border-radius: 13dp; overflow: hidden; }
.inventory-layout { width: 100%; height: 100%; display: flex; flex-direction: row; gap: 8dp; }
.inventory-left, .inventory-right { width: 50%; height: 100%; overflow-y: auto; background: #263449ee; border: 1dp #a8bdce66; border-radius: 9dp; padding: 7dp; }
.inventory-left { display: flex; flex-direction: column; }
.inventory-right { display: flex; flex-direction: column; }
.inventory-heading { font-size: 12dp; color: #f2f7fb; padding: 3dp 4dp 7dp 4dp; border-bottom: 1dp #d9e9f044; }
.inventory-equipment { display: flex; flex-direction: row; flex-wrap: wrap; gap: 4dp; padding: 5dp 0dp; }
.equipment-slot { width: 43dp; height: 36dp; background: #33465e; border: 1dp #c4d6e255; border-radius: 5dp; text-align: center; padding: 5dp 1dp; font-size: 8dp; color: #dbe8f0; }
.inventory-grid { display: flex; flex-direction: row; flex-wrap: wrap; gap: 3dp; padding-top: 5dp; }
.inventory-row { width: 48dp; height: 48dp; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 2dp; background: #3a5067; border: 1dp #d4e2ed88; border-radius: 6dp; color: #f1f6fa; font-size: 8dp; padding: 3dp; }
.inventory-icon { width: 23dp; height: 23dp; }
.material-row { display: flex; flex-direction: row; align-items: center; gap: 7dp; color: #e3edf4; font-size: 10dp; padding: 5dp 3dp; border-bottom: 1dp #aabed044; }
.material-row .inventory-icon { width: 20dp; height: 20dp; }
.inventory-row span { max-width: 42dp; overflow: hidden; }
.inventory-actions { display: flex; flex-direction: row; justify-content: flex-end; gap: 5dp; padding-top: 5dp; }
#crafting-panel { z-index: 10; left: 50%; top: 50%; width: 94%; height: 88%; margin-left: -47%; margin-top: -44%; padding: 9dp; background: #20344af5; border: 2dp #a9c1d4aa; border-radius: 13dp; overflow: hidden; }
#crafting-panel .recipe-title { font-size: 13dp; color: #f2f7fb; margin-bottom: 6dp; }
#crafting-recipes { height: 90%; overflow-y: auto; display: flex; flex-direction: row; flex-wrap: wrap; align-content: flex-start; gap: 5dp; }
#crafting-panel button { width: 48%; min-height: 42dp; background: #36516b; border: 1dp #a8c1d4aa; border-radius: 7dp; font-size: 10dp; padding: 5dp; }
.recipe-category { width: 100%; color: #d8f0a4; font-size: 11dp; padding: 5dp 2dp 2dp 2dp; border-bottom: 1dp #d8f0a455; }
#pause-panel { z-index: 10; width: 270dp; margin-left: -135dp; }
#pause-panel { z-index: 10; width: 270dp; margin-left: -135dp; }
#pause-panel .actions { justify-content: center; gap: 8dp; }
#pause-panel button { min-width: 104dp; }
.panel h1, .panel p { display: none; }
#target { position: absolute; z-index: 4; left: 50%; top: 57%; margin-left: -75dp; min-width: 150dp; text-align: center; color: #ffffff; font-size: 11dp; text-shadow: 0dp 1dp 3dp #000000; display: none; pointer-events: none; }
#target-progress { width: 100dp; height: 4dp; margin: 5dp auto 0dp auto; background: #ffffff33; border-radius: 4dp; overflow: hidden; }
#target-progress-fill { height: 4dp; width: 0%; background: #b9f27c; border-radius: 4dp; }
#selected-item { position: absolute; z-index: 6; left: 50%; bottom: 59dp; margin-left: -95dp; width: 190dp; max-width: 82%; padding: 4dp 8dp; display: none; flex-direction: row; align-items: center; justify-content: center; gap: 6dp; color: #ffffff; font-size: 10dp; text-shadow: 0dp 1dp 3dp #000000; background: #171c22d9; border: 1dp #aeb8c255; border-radius: 4dp; pointer-events: none; }
#selected-item img { width: 22dp; height: 22dp; }
#pickup { position: absolute; z-index: 5; right: 12dp; bottom: 54dp; color: #ffffff; font-size: 11dp; text-shadow: 0dp 1dp 3dp #000000; background: #102131bb; border: 1dp #d9e9a455; border-radius: 9dp; padding: 6dp 9dp; pointer-events: none; }

#inventory-panel { z-index: 9; }
#starter-chest-panel { z-index: 11; width: 92%; max-width: 520dp; max-height: 82%; margin-left: -46%; padding: 10dp; background: #17293cf5; border: 2dp #8ca8c5; border-radius: 10dp; overflow-y: auto; }
#starter-chest-panel .container-title { color: #e7f1fa; font-size: 13dp; padding: 2dp 4dp 8dp 4dp; border-bottom: 1dp #7694b455; }
#starter-chest-panel #starter-chest-items { display: flex; flex-direction: row; flex-wrap: wrap; align-content: flex-start; gap: 4dp; padding: 8dp 0dp; }
#starter-chest-panel .container-slot { width: 10.6%; min-width: 30dp; height: 44dp; padding: 2dp; background: #20384f; border: 1dp #7290a9; border-radius: 3dp; }
#starter-chest-panel .container-slot img { width: 24dp; height: 24dp; }
#starter-chest-panel .container-slot span { font-size: 7dp; color: #e9f1f6; }
#starter-chest-panel .container-empty { color: #7890a2; }
#starter-chest-panel .actions { justify-content: center; flex-wrap: wrap; gap: 6dp; }
#starter-chest-panel button { min-width: 84dp; min-height: 34dp; }
#death-panel { z-index: 20; width: 286dp; margin-left: -143dp; margin-top: -104dp; padding: 18dp; background: #251a25f5; border: 2dp #d46c70dd; border-radius: 14dp; }
.death-title { color: #ff7e83; text-align: center; font-size: 23dp; font-weight: bold; margin-bottom: 8dp; }
.death-subtitle { color: #e4d8dc; text-align: center; font-size: 11dp; margin-bottom: 16dp; }
#death-panel .actions { justify-content: center; gap: 8dp; }
#death-panel button { min-width: 110dp; }
#chat-message { position: absolute; z-index: 12; left: 50%; bottom: 62dp; width: 78%; margin-left: -39%; padding: 7dp 10dp; border-radius: 10dp; border: 1dp #d9e9a466; background: #101d2be8; color: #f3f7f8; font-size: 10dp; text-align: center; display: none; pointer-events: none; }

#vitals { position: absolute; z-index: 6; left: 50%; bottom: 72dp; margin-left: -42%; width: 84%; min-height: 24dp; padding: 0dp; display: flex; flex-direction: row; align-items: center; justify-content: center; gap: 7dp; background: transparent; border: 0dp transparent; border-radius: 0dp; box-shadow: none; }
#vitals { position: absolute; z-index: 6; left: 50%; bottom: 72dp; margin-left: -42%; width: 84%; min-height: 24dp; padding: 0dp; display: flex; flex-direction: row; align-items: center; justify-content: center; gap: 7dp; background: transparent; border: 0dp transparent; border-radius: 0dp; box-shadow: none; }
.vital-bar { display: flex; flex-direction: row; align-items: center; gap: 3dp; width: 76dp; }
.vital-symbol { width: 10dp; text-align: center; font-size: 10dp; }
.vital-track { width: 62dp; height: 8dp; background: #0b1420; border: 1dp #8ba0b2aa; border-radius: 3dp; overflow: hidden; }
.vital-fill { width: 100%; height: 8dp; border-radius: 2dp; transition: width 0.12s linear; }
#vitals { position: absolute; z-index: 6; left: 50%; bottom: 72dp; width: 84%; margin-left: -42%; display: flex; flex-direction: row; align-items: center; justify-content: center; gap: 7dp; padding: 0dp; background: transparent; border: 0dp transparent; box-shadow: none; }
#vitals-health-fill { background: #dd5964; }
#vitals-hunger-fill { background: #e5a34e; }
.vital-group { display: flex; flex-direction: row; align-items: center; gap: 1dp; white-space: nowrap; font-size: 10dp; }
.heart-on { color: #ff5e63; } .heart-off { color: #713e4a; }
.food-on { color: #f5a45b; } .food-off { color: #75533c; }
.armor-on { color: #e3f2ff; } .armor-off { color: #566a7b; }
#vitals-level { color: #d5f38c; font-size: 10dp; min-width: 25dp; text-align: right; font-weight: bold; }
#vitals-xp { width: 62dp; height: 5dp; background: #243240; border: 1dp #8296a5aa; border-radius: 4dp; overflow: hidden; }
#vitals-xp-fill { width: 0%; height: 5dp; background: #9fdf59; border-radius: 4dp; }
#quickbar { bottom: 12dp; gap: 3dp; padding: 0dp; background: transparent; border: 0dp transparent; border-radius: 0dp; box-shadow: none; }
.slot { height: 42dp; background: #101923e8; border: 1dp #98a8b7bb; border-radius: 3dp; font-size: 11dp; }
.slot img { width: 31dp; height: 31dp; }
.slot .slot-count { right: 2dp; bottom: 0dp; font-size: 10dp; text-shadow: 0dp 1dp 2dp #000000; }
#selected-item { bottom: 108dp; min-height: 30dp; padding: 5dp 10dp; background: #101a27ed; border: 1dp #d4e3eeaa; border-radius: 6dp; font-size: 11dp; }
#inventory-panel { z-index: 9; width: 96%; height: 91%; margin-left: -48%; margin-top: -45.5%; padding: 7dp; background: #111f31f8; border: 2dp #d5e1ee; border-radius: 9dp; overflow: hidden; }
.inventory-layout { width: 100%; height: 100%; display: flex; flex-direction: row; gap: 6dp; }
.inventory-left { width: 30%; min-width: 88dp; height: 100%; display: flex; flex-direction: column; gap: 5dp; background: #192b40; border: 1dp #69839a; border-radius: 6dp; padding: 6dp; overflow-y: auto; }
.inventory-right { width: 68%; height: 100%; display: flex; flex-direction: column; gap: 5dp; background: #192b40; border: 1dp #69839a; border-radius: 6dp; padding: 6dp; overflow-y: auto; }
.inventory-heading { color: #e9f1f7; font-size: 10dp; font-weight: bold; padding: 4dp 3dp; border-bottom: 1dp #d2dfeb66; }
.inventory-hint { color: #aebfd0; font-size: 7dp; padding: 2dp; }
.inventory-equipment { display: flex; flex-direction: row; flex-wrap: wrap; gap: 4dp; align-content: flex-start; }
.equipment-slot { width: 46%; height: 43dp; background: #233c55; border: 1dp #b0c4d7; border-radius: 4dp; font-size: 7dp; color: #ebf7ff; padding: 2dp; }
.equipment-slot img { width: 28dp; height: 28dp; }
.inventory-grid { display: flex; flex-direction: row; flex-wrap: wrap; align-content: flex-start; gap: 3dp; padding-top: 4dp; overflow-y: auto; }
.inventory-row { width: 29dp; height: 39dp; display: flex; flex-direction: column; align-items: center; justify-content: center; background: #213a54; border: 1dp #829db6; border-radius: 3dp; font-size: 6dp; padding: 1dp; }
.inventory-row.inventory-cursor { border: 2dp #f1d17c; background: #3b536d; }
.inventory-icon { width: 23dp; height: 23dp; }
.inventory-row span { max-width: 100%; font-size: 6dp; color: #eaf1f7; overflow: hidden; }
.inventory-durability { width: 23dp; height: 3dp; background: #071321; border-radius: 2dp; overflow: hidden; }
.inventory-durability span { display: block; height: 3dp; background: #9ad27d; }
.material-row { display: flex; flex-direction: row; align-items: center; gap: 5dp; min-height: 25dp; font-size: 8dp; color: #e2ebf2; }
.material-row .inventory-icon { width: 20dp; height: 20dp; }
#crafting-panel { z-index: 10; width: 96%; height: 91%; margin-left: -48%; margin-top: -45.5%; padding: 8dp; background: #111f31f8; border: 2dp #d5e1ee; border-radius: 9dp; overflow: hidden; }
#crafting-panel .recipe-title { color: #e9f1f7; font-size: 12dp; padding: 4dp 3dp 7dp 3dp; border-bottom: 1dp #d2dfeb66; }
.crafting-workbench { width: 100%; min-height: 58dp; display: flex; flex-direction: column; justify-content: center; background: #1b3047; border: 1dp #8099b2; border-radius: 5dp; padding: 6dp; margin-bottom: 6dp; }
.workbench-title { color: #dceaf5; font-size: 10dp; font-weight: bold; }
.crafting-note { color: #aebfd0; font-size: 8dp; margin-top: 4dp; }
#crafting-recipes { height: 76%; overflow-y: auto; display: flex; flex-direction: row; flex-wrap: wrap; align-content: flex-start; gap: 4dp; }
#crafting-panel button { width: 48%; min-height: 40dp; background: #203b56; border: 1dp #91a9c1; border-radius: 4dp; font-size: 8dp; padding: 4dp; }
#crafting-panel .inventory-actions button { width: auto; min-width: 104dp; }
.craft-recipe-icon { width: 24dp; height: 24dp; margin-right: 5dp; }
.recipe-category { width: 100%; color: #d8f0a4; font-size: 9dp; padding: 4dp 2dp; border-bottom: 1dp #d8f0a455; }

</style>
</head>
<body>
<div id="topbar">
  <div class="actions">
    <div id="stats">FPS -- · Chunks --</div>
    <button id="starter-chest-open">COFRE</button>
    <button id="chat">✉</button>
    <button id="inventory">▦</button>
    <button id="perspective">◉</button>
    <button id="pause">Ⅱ</button>
  </div>
</div>
<div id="minimap"><div id="minimap-title">MAPA</div><div id="minimap-grid"></div><div id="minimap-north">N</div><div id="minimap-coords">X 0 · Z 0</div></div>
<div id="crosshair"><div id="crosshair-v"></div><div id="crosshair-h"></div><div id="crosshair-center"></div></div>
<div id="movement-pad">
  <div id="move-up" class="pad-arrow">↑</div><div id="move-down" class="pad-arrow">↓</div>
  <div id="move-left" class="pad-arrow">←</div><div id="move-right" class="pad-arrow">→</div>
  <button id="crouch">▣</button>
</div>
<div id="hint"></div>
<div id="vitals"><div id="vitals-health" class="vital-bar"><span class="vital-symbol heart-on">♥</span><div class="vital-track"><div id="vitals-health-fill" class="vital-fill"></div></div></div><div id="vitals-hunger" class="vital-bar"><span class="vital-symbol food-on">◆</span><div class="vital-track"><div id="vitals-hunger-fill" class="vital-fill"></div></div></div><div class="vital-group" id="vitals-armor"></div><div id="vitals-level">NV 1</div><div id="vitals-xp"><div id="vitals-xp-fill"></div></div></div>
<div id="quickbar">
  <button class="slot" id="slot-0">1</button><button class="slot" id="slot-1">2</button><button class="slot" id="slot-2">3</button>
  <button class="slot" id="slot-3">4</button><button class="slot" id="slot-4">5</button><button class="slot" id="slot-5">6</button>
  <button class="slot" id="slot-6">7</button><button class="slot" id="slot-7">8</button><button class="slot" id="slot-8">9</button>
</div>
<button id="jump">↑</button><button id="attack">⚔</button>
<div id="target"><div id="target-name"></div><div id="target-progress"><div id="target-progress-fill"></div></div></div>
<div id="selected-item"></div>
<div id="pickup"></div>
<div id="inventory-panel" class="panel">
  <div class="inventory-layout" id="inventory-layout">
    <div class="inventory-left" id="inventory-left">
      <div class="inventory-heading">PERSONAJE</div>
      <div class="inventory-hint">Equipo y protección</div>
      <div class="inventory-equipment" id="equipment-items"></div>
      <div class="inventory-heading">ACCIONES</div>
      <div class="inventory-actions"><button id="eat">Comer</button></div>
      <div class="inventory-actions"><button id="crafting-open">Fabricar</button><button id="inventory-close">Cerrar</button></div>
    </div>
    <div class="inventory-right" id="inventory-right">
      <div class="inventory-heading">MOCHILA · 36 ESPACIOS</div>
      <div class="inventory-hint">Toca un espacio y después otro para mover o combinar. Selecciona una pila para dividirla o soltarla.</div>
      <div id="inventory-items" class="inventory-grid"></div>
      <div class="inventory-actions"><button id="inventory-split">Dividir pila</button><button id="inventory-drop">Soltar 1</button></div>
      <div class="inventory-heading">MATERIALES</div>
      <div id="inventory-catalog" class="inventory-materials"></div>
    </div>
  </div>
</div>
<div id="crafting-panel" class="panel">
  <div class="crafting-workbench">
    <div class="workbench-title">FABRICACIÓN · RECETAS REALES</div>
    <div class="crafting-note">Selecciona una receta para comprobar ingredientes, estación y espacio libre. La fabricación se confirma en tu inventario.</div>
  </div>
  <div class="recipe-title">RECETAS DISPONIBLES</div>
  <div id="crafting-recipes"></div>
  <div class="inventory-actions"><button id="crafting-close">Volver al inventario</button></div>
</div>
<div id="pause-panel" class="panel">
  <div class="actions">
    <button id="resume">Continuar</button>
    <button id="save-exit">Guardar y salir</button>
  </div>
</div>
<div id="starter-chest-panel" class="panel">
  <div class="container-title">COFRE · 27 ESPACIOS</div>
  <div id="starter-chest-items" class="container-grid"></div>
  <div class="actions">
    <button id="starter-chest-take">Recoger todo</button>
    <button id="container-put">Guardar selección</button>
    <button id="starter-chest-close">Cerrar</button>
  </div>
</div>
<div id="death-panel" class="panel">
  <div class="death-title">HAS MUERTO</div>
  <div class="death-subtitle">Tu aventura continúa. Puedes volver al mundo.</div>
  <div class="actions"><button id="respawn">Reaparecer</button><button id="death-exit">Guardar y salir</button></div>
</div>
<div id="chat-message"></div>
</body>
</rml>
)RML";
}

double RmlUiRuntime::AndroidSystemInterface::GetElapsedTime() {
    const auto elapsed = std::chrono::steady_clock::now() - kStartTime;
    return std::chrono::duration<double>(elapsed).count();
}

bool RmlUiRuntime::AndroidSystemInterface::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    gLastRmlLogMessage = message;
    const int priority = type == Rml::Log::LT_ERROR ? ANDROID_LOG_ERROR :
                        type == Rml::Log::LT_WARNING ? ANDROID_LOG_WARN : ANDROID_LOG_INFO;
    __android_log_print(priority, kTag, "%s", message.c_str());
    return true;
}

Rml::FileHandle RmlUiRuntime::MemoryOnlyFileInterface::Open(const Rml::String& path) {
    __android_log_print(ANDROID_LOG_WARN, kTag,
                        "RmlUi requested a disk resource outside the in-memory HUD: %s",
                        path.c_str());
    return static_cast<Rml::FileHandle>(0);
}

void RmlUiRuntime::MemoryOnlyFileInterface::Close(Rml::FileHandle) {}
std::size_t RmlUiRuntime::MemoryOnlyFileInterface::Read(void*, std::size_t, Rml::FileHandle) { return 0; }
bool RmlUiRuntime::MemoryOnlyFileInterface::Seek(Rml::FileHandle, long, int) { return false; }
std::size_t RmlUiRuntime::MemoryOnlyFileInterface::Tell(Rml::FileHandle) { return 0; }

void RmlUiRuntime::ActionListener::ProcessEvent(Rml::Event& event) {
    (void)event;
    if (callback_) callback_(action_);
}

bool RmlUiRuntime::Initialise(int width, int height, float displayDensity, ActionCallback callback) {
    Shutdown();
    lastError_.clear();
    gLastRmlLogMessage.clear();
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    density_ = displayDensity > 0.25f ? displayDensity : 1.0f;
    callback_ = std::move(callback);

    if (!renderer_.Initialise()) {
        lastError_ = renderer_.LastError().empty()
            ? "No se pudo inicializar el backend OpenGL ES de RmlUi."
            : renderer_.LastError();
        __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", lastError_.c_str());
        return false;
    }
    if(!blockAtlasPixels_.empty())
        renderer_.SetBlockAtlas(blockAtlasPixels_.data(),blockAtlasPixels_.size(),
                                blockAtlasWidth_,blockAtlasHeight_);

    Rml::SetSystemInterface(&system_);
    Rml::SetRenderInterface(&renderer_);
    Rml::SetFileInterface(&fileInterface_);
    if (!Rml::Initialise()) {
        lastError_ = "Rml::Initialise() rechazó las interfaces Android.";
        if (!gLastRmlLogMessage.empty()) lastError_ += " Detalle: " + gLastRmlLogMessage;
        __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", lastError_.c_str());
        renderer_.Shutdown();
        return false;
    }
    rmlInitialised_ = true;

    fontData_.clear();
    const char* fontPaths[] = {"/system/fonts/Roboto-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf"};
    for (const char* path : fontPaths) {
        std::ifstream font(path, std::ios::binary);
        if (!font) continue;
        fontData_.assign(std::istreambuf_iterator<char>(font), std::istreambuf_iterator<char>());
        if (!fontData_.empty()) break;
    }
    if (!fontData_.empty()) {
        const Rml::Span<const Rml::byte> fontSpan(fontData_.data(), fontData_.size());
        if (!Rml::LoadFontFace(fontSpan, "Roboto", Rml::Style::FontStyle::Normal,
                               Rml::Style::FontWeight::Normal, true)) {
            __android_log_print(ANDROID_LOG_WARN, kTag, "The Android system font could not be registered with RmlUi");
        }
    } else {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "No readable Android system font was found; RmlUi text may be unavailable");
    }

    context_ = Rml::CreateContext("minefade_hud", {width_, height_}, &renderer_);
    if (context_ == nullptr) {
        lastError_ = "RmlUi no pudo crear el contexto táctil.";
        if (!gLastRmlLogMessage.empty()) lastError_ += " Detalle: " + gLastRmlLogMessage;
        __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", lastError_.c_str());
        Rml::Shutdown();
        rmlInitialised_ = false;
        renderer_.Shutdown();
        return false;
    }
    context_->SetDensityIndependentPixelRatio(displayDensity > 0.25f ? displayDensity : 1.0f);
    std::string hudMarkup = kHudDocument;
    if (!showStats_) {
        const std::string statsRule = "#stats { color:";
        const std::size_t statsPosition = hudMarkup.find(statsRule);
        if (statsPosition != std::string::npos)
            hudMarkup.replace(statsPosition, statsRule.size(), "#stats { display: none; color:");
    }
    if (leftHanded_) {
        const auto replaceStyle = [&hudMarkup](const std::string& from, const std::string& to) {
            const std::size_t position = hudMarkup.find(from);
            if (position != std::string::npos) hudMarkup.replace(position, from.size(), to);
        };
        replaceStyle("left: 14dp; bottom: 54dp;", "right: 14dp; left: auto; bottom: 54dp;");
        replaceStyle("right: 22dp; bottom: 60dp;", "left: 22dp; right: auto; bottom: 60dp;");
        replaceStyle("right: 82dp; bottom: 65dp;", "left: 82dp; right: auto; bottom: 65dp;");
        replaceStyle("right: 24dp; bottom: 126dp;", "left: 24dp; right: auto; bottom: 126dp;");
    }
    document_ = context_->LoadDocumentFromMemory(hudMarkup, "minefade://hud.rml");
    if (document_ == nullptr) {
        const std::string primaryError = gLastRmlLogMessage;
        __android_log_print(ANDROID_LOG_WARN, kTag,
                            "The full HUD did not parse; retrying a safe touch HUD: %s",
                            primaryError.c_str());
        gLastRmlLogMessage.clear();
        constexpr char kFallbackHud[] = R"RML(
<rml>
<head>
<title>Minefade Safe HUD</title>
<style>
body { margin: 0dp; padding: 0dp; width: 100%; height: 100%; color: #ffffff; font-family: Roboto; }
button { background-color: rgba(52, 83, 105, 0.88); border: 1dp solid rgba(220, 238, 249, 0.65); border-radius: 9dp; color: #ffffff; padding: 5dp; font-size: 12dp; }
#topbar { position: absolute; top: 8dp; right: 8dp; display: flex; }
#topbar button { width: 38dp; height: 38dp; margin-left: 4dp; }
#stats { position: absolute; right: 112dp; top: 12dp; display: block; padding: 6dp 8dp; color: #f3f8fb; font-size: 10dp; border: 1dp solid #d7e8ef77; border-radius: 5dp; background: #1c344acb; }
#movement-pad { position: absolute; left: 14dp; bottom: 54dp; width: 132dp; height: 132dp; border-radius: 18dp; pointer-events: none; }
.pad-arrow { position: absolute; width: 42dp; height: 42dp; text-align: center; background: rgba(25, 50, 73, 0.88); border: 1dp solid #e1efb8; border-radius: 11dp; font-size: 20dp; }
#move-up { left: 45dp; top: 2dp; } #move-down { left: 45dp; bottom: 2dp; }
#move-left { left: 2dp; top: 45dp; } #move-right { right: 2dp; top: 45dp; }
#quickbar { position: absolute; bottom: 10dp; left: 50%; margin-left: 0dp; display: flex; gap: 2.5dp; padding: 0dp; background: transparent; border: 0dp transparent; border-radius: 0dp; box-shadow: none; }
.slot { width: 34dp; min-width: 34dp; height: 36dp; margin-right: 0dp; padding: 0dp; background: #30363dcc; border: 1dp #7e878fbb; border-radius: 2dp; }
.slot img { width: 26dp; height: 26dp; }
#jump { position: absolute; right: 22dp; bottom: 60dp; width: 56dp; height: 56dp; font-size: 20dp; }
#attack { position: absolute; right: 24dp; bottom: 126dp; width: 50dp; height: 50dp; font-size: 17dp; }
#movement-pad #crouch { position: absolute; left: 45dp; top: 45dp; right: auto; bottom: auto; width: 42dp; height: 42dp; font-size: 15dp; pointer-events: auto; }
#minimap { position: absolute; z-index: 9; top: 54dp; left: 10dp; width: 98dp; padding: 5dp; display: flex; flex-direction: column; opacity: 1; visibility: visible; border: 1dp solid #d7e8efcc; border-radius: 8dp; background-color: #10243aee; }
#minimap-title { color: #dcebf5; font-size: 8dp; font-weight: bold; text-align: center; margin-bottom: 3dp; }
#minimap-grid { display: flex; flex-direction: row; flex-wrap: wrap; align-content: flex-start; gap: 1dp; width: 88dp; height: 88dp; border: 1dp solid #e2f0f4aa; border-radius: 5dp; overflow: hidden; background: #173047; }
.map-tile { width: 11dp; height: 11dp; border-radius: 1dp; }
.map-low { background: #6c8064; } .map-mid { background: #52805b; } .map-high { background: #8b9185; } .map-water { background: #326f9a; }
.map-player { background: #d9ed94; border: 1dp solid #f8ffe8; border-radius: 5dp; }
#minimap-north { position: absolute; right: 6dp; top: 18dp; width: 14dp; height: 14dp; background: #142638cc; color: #ffffff; border-radius: 7dp; text-align: center; font-size: 9dp; }
#minimap-coords { margin-top: 3dp; text-align: center; color: #d4e4e9; font-size: 8dp; }
#crosshair { position: absolute; z-index: 5; display: block; left: 50%; top: 50%; width: 16dp; height: 16dp; margin-left: -8dp; margin-top: -8dp; pointer-events: none; }
#crosshair-v { position: absolute; left: 7dp; top: 0dp; width: 2dp; height: 16dp; background: #ffffffee; }
#crosshair-h { position: absolute; left: 0dp; top: 7dp; width: 16dp; height: 2dp; background: #ffffffee; }
#crosshair-center { position: absolute; left: 6dp; top: 6dp; width: 4dp; height: 4dp; background: #e7f6ad; border-radius: 2dp; }
#target { position: absolute; top: 58%; left: 50%; margin-left: -70dp; width: 140dp; text-align: center; display: none; }
#target-progress { width: 100dp; height: 4dp; margin: 4dp auto 0dp auto; background: #ffffff44; border-radius: 3dp; overflow: hidden; }
#target-progress-fill { height: 4dp; width: 0%; background: #b9f27c; border-radius: 3dp; transition: width 0.10s linear; }
#selected-item { position: absolute; left: 50%; bottom: 59dp; margin-left: -95dp; width: 190dp; max-width: 82%; padding: 4dp 8dp; text-align: center; background: #171c22d9; border: 1dp #aeb8c255; border-radius: 4dp; display: none; }
#pickup { position: absolute; right: 12dp; bottom: 54dp; display: none; }
.panel { position: absolute; left: 50%; top: 50%; width: 280dp; margin-left: -140dp; margin-top: -100dp; padding: 12dp; border: 1dp solid #ffffffaa; border-radius: 12dp; background-color: rgba(21, 41, 61, 0.96); display: none; }
#pause-panel .actions { display: flex; }
#pause-panel button { width: 128dp; margin-right: 6dp; }
#death-panel { z-index: 20; width: 286dp; margin-left: -143dp; margin-top: -104dp; background: #251a25f5; border: 2dp solid #d46c70; }
.death-title { color: #ff7e83; text-align: center; font-size: 23dp; font-weight: bold; margin-bottom: 8dp; }
.death-subtitle { color: #e4d8dc; text-align: center; font-size: 11dp; margin-bottom: 16dp; }
#death-panel .actions { display: flex; justify-content: center; }
#death-panel button { width: 110dp; margin-right: 6dp; }
.vital-bar { display: flex; flex-direction: row; align-items: center; gap: 3dp; width: 76dp; }
.vital-track { width: 62dp; height: 8dp; background: #0b1420; border: 1dp solid #8ba0b2; border-radius: 3dp; overflow: hidden; }
.vital-fill { width: 100%; height: 8dp; }
#vitals-health-fill { background: #dd5964; }
#vitals-hunger-fill { background: #e5a34e; }
#vitals-level { color: #d5f38c; font-size: 10dp; }
#vitals-xp { width: 62dp; height: 5dp; background: #243240; border: 1dp solid #8296a5; }
#vitals-xp-fill { height: 5dp; background: #9fdf59; }
</style>
</head>
<body>
<div id="topbar">
<div id="stats"></div>
<button id="chat">✉</button>
<button id="inventory">▦</button>
<button id="perspective">◉</button>
<button id="pause">Ⅱ</button>
</div>
<div id="minimap"><div id="minimap-title">MAPA</div><div id="minimap-grid"></div><div id="minimap-north">N</div><div id="minimap-coords">X 0 · Z 0</div></div>
<div id="crosshair"><div id="crosshair-v"></div><div id="crosshair-h"></div><div id="crosshair-center"></div></div>
<div id="vitals"><div id="vitals-health" class="vital-bar"><span class="heart-on">♥</span><div class="vital-track"><div id="vitals-health-fill" class="vital-fill"></div></div></div><div id="vitals-hunger" class="vital-bar"><span class="food-on">◆</span><div class="vital-track"><div id="vitals-hunger-fill" class="vital-fill"></div></div></div><div id="vitals-armor" class="vital-group"></div><div id="vitals-level">NV 1</div><div id="vitals-xp"><div id="vitals-xp-fill"></div></div></div>
<div id="movement-pad"><div id="move-up" class="pad-arrow">↑</div><div id="move-down" class="pad-arrow">↓</div><div id="move-left" class="pad-arrow">←</div><div id="move-right" class="pad-arrow">→</div><button id="crouch">▣</button></div>
<div id="quickbar">
<button class="slot" id="slot-0">1</button><button class="slot" id="slot-1">2</button><button class="slot" id="slot-2">3</button>
<button class="slot" id="slot-3">4</button><button class="slot" id="slot-4">5</button><button class="slot" id="slot-5">6</button>
<button class="slot" id="slot-6">7</button><button class="slot" id="slot-7">8</button><button class="slot" id="slot-8">9</button>
</div>
<button id="jump">↑</button><button id="attack">⚔</button>
<div id="target"><div id="target-name"></div><div id="target-progress"><div id="target-progress-fill"></div></div></div>
<div id="selected-item"></div><div id="pickup"></div>
<div id="inventory-panel" class="panel">
<button id="inventory-close">Cerrar</button><div id="equipment-items"></div><div id="inventory-items"></div>
<div id="inventory-catalog"></div><button id="crafting-open">Fabricar</button>
</div>
<div id="crafting-panel" class="panel"><div id="crafting-recipes"></div><button id="crafting-close">Volver</button></div>
<div id="pause-panel" class="panel"><div class="actions"><button id="resume">Continuar</button><button id="save-exit">Guardar y salir</button></div></div>
<div id="starter-chest-panel" class="panel"><div class="container-title">COFRE · 27 ESPACIOS</div>
<div id="starter-chest-items" class="container-grid"></div>
<button id="starter-chest-take">Recoger todo</button><button id="container-put">Guardar selección</button><button id="starter-chest-close">Cerrar</button></div>
<div id="death-panel" class="panel"><div class="death-title">HAS MUERTO</div><div class="death-subtitle">Tu aventura continúa.</div><div class="actions"><button id="respawn">Reaparecer</button><button id="death-exit">Guardar y salir</button></div></div>
</body>
</rml>
)RML";
        document_ = context_->LoadDocumentFromMemory(kFallbackHud, "minefade://hud-safe.rml");
        if (document_ != nullptr) {
            usingSafeDocument_ = true;
            lastError_ = "El HUD principal no se pudo cargar; se solicita el HUD Android completo.";
            if (!primaryError.empty()) lastError_ += " Diagnóstico del HUD principal: " + primaryError;
            __android_log_print(ANDROID_LOG_WARN, kTag, "%s", lastError_.c_str());
        }
        if (document_ == nullptr) {
            lastError_ = "RmlUi no pudo interpretar ni el HUD principal ni el HUD táctil seguro.";
            if (!gLastRmlLogMessage.empty()) lastError_ += " Detalle: " + gLastRmlLogMessage;
            __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", lastError_.c_str());
            Rml::RemoveContext("minefade_hud");
            context_ = nullptr;
            Rml::Shutdown();
            rmlInitialised_ = false;
            renderer_.Shutdown();
            return false;
        }
    }

    AttachAction("chat", "chat");
    AttachAction("respawn", "respawn");
    AttachAction("death-exit", "save-exit");
    AttachAction("inventory", "inventory");
    AttachAction("inventory-close", "inventory-close");
    AttachAction("inventory-split", "inventory-split");
    AttachAction("inventory-drop", "inventory-drop");
    AttachAction("crafting-open", "crafting-open");
    AttachAction("crafting-close", "crafting-close");
    AttachAction("craft-planks", "craft-planks");
    AttachAction("craft-sticks", "craft-sticks");
    AttachAction("craft-table", "craft-table");
    AttachAction("craft-wooden-pickaxe", "craft-wooden-pickaxe");
    AttachAction("craft-wooden-axe", "craft-wooden-axe");
    AttachAction("craft-wooden-shovel", "craft-wooden-shovel");
    AttachAction("craft-wooden-sword", "craft-wooden-sword");
    AttachAction("jump", "jump");
    AttachAction("crouch", "crouch");
    AttachAction("attack", "attack");
    AttachAction("eat", "eat");
    AttachAction("starter-chest-open", "starter-chest-open");
    AttachAction("starter-chest-close", "starter-chest-close");
    AttachAction("starter-chest-take", "starter-chest-take");
    AttachAction("container-put", "container-put");
    for (int slot = 0; slot < 9; ++slot) {
        const std::string id = "slot-" + std::to_string(slot);
        AttachAction(id.c_str(), id.c_str());
    }
    AttachAction("pause", "pause");
    AttachAction("perspective", "perspective");
    AttachAction("resume", "resume");
    AttachAction("save", "save");
    AttachAction("save-exit", "save-exit");
    document_->Show();
    context_->Update();
    ready_ = true;
    ApplyResponsiveLayout();
    SetMinimapShape(minimapShape_);
    __android_log_print(ANDROID_LOG_INFO, kTag, "RmlUi %s initialised with OpenGL ES backend%s",
                        Rml::GetVersion().c_str(), usingSafeDocument_ ? " (safe document; Android HUD required)" : "");
    return !usingSafeDocument_;
}

void RmlUiRuntime::AttachAction(const char* elementId, const char* action) {
    if (document_ == nullptr) return;
    Rml::Element* element = document_->GetElementById(elementId);
    if (element == nullptr) return;
    listeners_.push_back(std::make_unique<ActionListener>(callback_, action));
    element->AddEventListener("click", listeners_.back().get());
}

void RmlUiRuntime::Shutdown() {
    ready_ = false;
    usingSafeDocument_ = false;
    // Context/document destruction invokes listener detach callbacks, so listeners must outlive it.
    document_ = nullptr;
    if (context_ != nullptr) {
        Rml::RemoveContext("minefade_hud");
        context_ = nullptr;
    }
    listeners_.clear();
    inventoryListeners_.clear();
    craftingListeners_.clear();
    containerListeners_.clear();
    // RmlUi requires its contexts to be destroyed before Shutdown and the render interface to outlive it.
    if (rmlInitialised_) Rml::Shutdown();
    rmlInitialised_ = false;
    fontData_.clear();
    mouseCaptured_ = false;
    mousePointerId_ = -1;
    renderer_.Shutdown();
}

void RmlUiRuntime::SetBlockAtlas(const std::uint8_t* pixels,std::size_t bytes,
                                     int width,int height) {
    if (pixels == nullptr ||
        width != static_cast<int>(kBlockAtlasWidth) ||
        height != static_cast<int>(kBlockAtlasHeight) ||
        bytes < kBlockAtlasByteCount) {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "Rejected block atlas: %dx%d bytes=%zu; expected %zux%zu bytes=%zu",
            width, height, bytes, kBlockAtlasWidth, kBlockAtlasHeight,
            kBlockAtlasByteCount);
        blockAtlasPixels_.clear();
        blockAtlasWidth_ = blockAtlasHeight_ = 0;
        return;
    }
    blockAtlasPixels_.assign(pixels, pixels + kBlockAtlasByteCount);
    blockAtlasWidth_ = width;
    blockAtlasHeight_ = height;
}

void RmlUiRuntime::Resize(int width, int height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    renderer_.SetViewport(width_, height_);
    if (context_ != nullptr) {
        context_->SetDimensions({width_, height_});
        ApplyResponsiveLayout();
    }
}

bool RmlUiRuntime::IsInMovementPad(float x, float y) const {
    if (usingSafeDocument_) return false;
    const float padSize = 132.0f * density_;
    const float padMargin = 14.0f * density_;
    const float padBottom = 54.0f * density_;
    const float padLeft = leftHanded_
        ? static_cast<float>(width_) - padMargin - padSize : padMargin;
    const float padTop = static_cast<float>(height_) - padBottom - padSize;
    return x >= padLeft && x <= padLeft + padSize &&
           y >= padTop && y <= static_cast<float>(height_) - padBottom;
}

bool RmlUiRuntime::MovementVectorAt(float x, float y, float& moveX, float& moveY) const {
    moveX = 0.0f;
    moveY = 0.0f;
    if (usingSafeDocument_) return false;
    const float size = 132.0f * density_;
    const float margin = 14.0f * density_;
    const float bottom = 54.0f * density_;
    const float left = leftHanded_ ? static_cast<float>(width_) - margin - size : margin;
    const float top = static_cast<float>(height_) - bottom - size;
    const float button = 42.0f * density_;
    const float offset = 45.0f * density_;
    const auto hit = [x,y,button](float l, float t) {
        return x >= l && x <= l + button && y >= t && y <= t + button;
    };
    // Only the four visible directional buttons capture movement. The gaps and
    // centre are not a giant invisible joystick hitbox: they remain available
    // for camera look or the dedicated crouch control.
    if (hit(left + offset, top + 2.0f * density_)) { moveY = 1.0f; return true; }
    if (hit(left + offset, top + size - button - 2.0f * density_)) { moveY = -1.0f; return true; }
    if (hit(left + 2.0f * density_, top + offset)) { moveX = -1.0f; return true; }
    if (hit(left + size - button - 2.0f * density_, top + offset)) { moveX = 1.0f; return true; }
    return false;
}

bool RmlUiRuntime::CrouchButtonAt(float x, float y) const {
    if (usingSafeDocument_) return false;
    const float size = 132.0f * density_;
    const float margin = 14.0f * density_;
    const float bottom = 54.0f * density_;
    const float left = leftHanded_ ? static_cast<float>(width_) - margin - size : margin;
    const float top = static_cast<float>(height_) - bottom - size;
    const float startX = left + 45.0f * density_;
    const float startY = top + 45.0f * density_;
    const float button = 42.0f * density_;
    return x >= startX && x <= startX + button && y >= startY && y <= startY + button;
}

bool RmlUiRuntime::Touch(int action, int pointerId, float x, float y) {
    if (!ready_ || context_ == nullptr || usingSafeDocument_) return true;

    // RmlUi's mouse API represents one pointer. Reserve it for the finger that
    // actually pressed a HUD element; other fingers remain world/camera gestures.
    // Otherwise a second finger can send MouseUp and release the first finger's
    // button while the player is trying to move and operate the HUD simultaneously.
    if (action == 0) {
        if (mousePointerId_ >= 0 && mousePointerId_ != pointerId) {
            // A second finger landing on another HUD button is UI input, not
            // a quick world tap (which could place a block on release).
            Rml::Element* hit = context_->GetElementAtPoint(Rml::Vector2f(
                static_cast<float>(x), static_cast<float>(y)));
            return hit == nullptr || hit->Closest("button") == nullptr;
        }
        const Rml::Vector2f point(static_cast<float>(x), static_cast<float>(y));
        Rml::Element* hit = context_->GetElementAtPoint(point);
        // Only real HUD buttons capture a finger. Decorative panels, bars and
        // labels never swallow the camera-look pointer while another finger moves.
        if (hit == nullptr || hit->Closest("button") == nullptr) {
            mouseCaptured_ = false;
            mousePointerId_ = -1;
            return true;
        }
        const int px = static_cast<int>(x);
        const int py = static_cast<int>(y);
        const bool outside = context_->ProcessMouseMove(px, py, 0);
        const bool buttonOutside = context_->ProcessMouseButtonDown(0, 0);
        mouseCaptured_ = !(outside && buttonOutside);
        mousePointerId_ = mouseCaptured_ ? pointerId : -1;
        return !mouseCaptured_;
    }

    // An unrelated pointer must never move, press or release the virtual mouse.
    if (mousePointerId_ != pointerId || !mouseCaptured_) return true;

    const int px = static_cast<int>(x);
    const int py = static_cast<int>(y);
    const bool outside = context_->ProcessMouseMove(px, py, 0);
    if (action == 1 || action == 3) {
        const bool buttonOutside = context_->ProcessMouseButtonUp(0, 0);
        const bool consumed = mouseCaptured_ || !outside || !buttonOutside;
        mouseCaptured_ = false;
        mousePointerId_ = -1;
        return !consumed;
    }
    return false;
}

void RmlUiRuntime::SetVitals(float health, float hunger, int armorPieces,
                             std::uint32_t level, float xpProgress) {
    if (document_ == nullptr) return;
    const auto makePips = [](int filled, const char* onClass, const char* offClass,
                             const char* symbol) {
        std::string markup;
        for (int i = 0; i < 10; ++i) {
            markup += "<span class='";
            markup += i < filled ? onClass : offClass;
            markup += "'>";
            markup += symbol;
            markup += "</span>";
        }
        return markup;
    };
    const int armorPips = std::clamp(armorPieces * 2, 0, 10);
    const float healthPercent = std::clamp(health, 0.0f, 20.0f) * 5.0f;
    const float hungerPercent = std::clamp(hunger, 0.0f, 20.0f) * 5.0f;
    if (Rml::Element* element = document_->GetElementById("vitals-armor"))
        element->SetInnerRML(makePips(armorPips, "armor-on", "armor-off", "⬟"));
    if (Rml::Element* element = document_->GetElementById("vitals-health-fill"))
        element->SetProperty("width", std::to_string(healthPercent) + "%");
    if (Rml::Element* element = document_->GetElementById("vitals-hunger-fill"))
        element->SetProperty("width", std::to_string(hungerPercent) + "%");
    if (Rml::Element* element = document_->GetElementById("vitals-level"))
        element->SetInnerRML("NV " + std::to_string(level));
    if (Rml::Element* element = document_->GetElementById("vitals-xp")) {
        if (Rml::Element* fill = element->GetFirstChild()) {
            const float percent = std::clamp(xpProgress, 0.0f, 1.0f) * 100.0f;
            fill->SetProperty("width", std::to_string(percent) + "%");
        }
    }
}

void RmlUiRuntime::SetLeftHanded(bool enabled) {
    leftHanded_ = enabled;
    ApplyResponsiveLayout();
}

void RmlUiRuntime::ApplyResponsiveLayout() {
    if (document_ == nullptr || density_ <= 0.0f) return;
    if (Rml::Element* pad = document_->GetElementById("movement-pad")) {
        pad->SetProperty("left", leftHanded_ ? "auto" : "14dp");
        pad->SetProperty("right", leftHanded_ ? "14dp" : "auto");
    }
    if (Rml::Element* jump = document_->GetElementById("jump")) {
        jump->SetProperty("left", leftHanded_ ? "22dp" : "auto");
        jump->SetProperty("right", leftHanded_ ? "auto" : "22dp");
    }
    if (Rml::Element* attack = document_->GetElementById("attack")) {
        attack->SetProperty("left", leftHanded_ ? "24dp" : "auto");
        attack->SetProperty("right", leftHanded_ ? "auto" : "24dp");
    }

    // Nine slots stay present at every width. The bar is positioned from the
    // live surface width (not a fixed negative margin) with safe side margins.
    constexpr float kSafeMarginDp = 10.0f;
    constexpr float kGapDp = 3.0f;
    constexpr float kOuterPaddingDp = 5.0f;
    constexpr float kBorderDp = 2.0f;
    constexpr float kSlotCount = 9.0f;
    const float surfaceWidthDp = static_cast<float>(width_) / density_;
    const float availableWidthDp = std::max(0.0f, surfaceWidthDp - 2.0f * kSafeMarginDp);
    const float fixedWidthDp = 8.0f * kGapDp + 2.0f * kOuterPaddingDp + 2.0f * kBorderDp;
    const float slotWidth = std::clamp((availableWidthDp - fixedWidthDp) / kSlotCount,
                                       28.0f, 42.0f);
    const float barWidth = kSlotCount * slotWidth + fixedWidthDp;
    const float leftDp = std::max(0.0f, (surfaceWidthDp - barWidth) * 0.5f);
    if (Rml::Element* quickbar = document_->GetElementById("quickbar")) {
        quickbar->SetProperty("left", std::to_string(leftDp) + "dp");
        quickbar->SetProperty("margin-left", "0dp");
        quickbar->SetProperty("gap", std::to_string(kGapDp) + "dp");
        quickbar->SetProperty("padding", std::to_string(kOuterPaddingDp) + "dp");
    }
    const std::string slotValue = std::to_string(slotWidth) + "dp";
    const std::string iconValue = std::to_string(std::clamp(slotWidth * 0.78f, 23.0f, 32.0f)) + "dp";
    for (int slot = 0; slot < 9; ++slot) {
        const std::string id = "slot-" + std::to_string(slot);
        if (Rml::Element* element = document_->GetElementById(id)) {
            element->SetProperty("width", slotValue);
            element->SetProperty("min-width", slotValue);
            element->SetProperty("height", "42dp");
            if (Rml::Element* icon = element->GetFirstChild()) {
                icon->SetProperty("width", iconValue);
                icon->SetProperty("height", iconValue);
            }
        }
    }

    const bool wideLandscape = width_ > height_ && surfaceWidthDp >= 520.0f;
    if (Rml::Element* layout = document_->GetElementById("inventory-layout"))
        layout->SetProperty("flex-direction", wideLandscape ? "row" : "column");
    if (Rml::Element* left = document_->GetElementById("inventory-left")) {
        left->SetProperty("width", wideLandscape ? "34%" : "100%");
        left->SetProperty("height", wideLandscape ? "100%" : "47%");
    }
    if (Rml::Element* right = document_->GetElementById("inventory-right")) {
        right->SetProperty("width", wideLandscape ? "63%" : "100%");
        right->SetProperty("height", wideLandscape ? "100%" : "47%");
    }
}

void RmlUiRuntime::UpdateAndRender() {
    if (!ready_ || context_ == nullptr || usingSafeDocument_) return;
    ScopedRmlUiGlState stateGuard;
    context_->Update();
    context_->Render();
}

void RmlUiRuntime::SetControlMode(int mode) {
    (void)mode;
    controlMode_ = 0;
}

void RmlUiRuntime::SetMinimapShape(int shape) {
    minimapShape_ = std::clamp(shape, 0, 1);
    if (document_ == nullptr) return;
    if (Rml::Element* grid = document_->GetElementById("minimap-grid")) {
        grid->SetProperty("border-radius", minimapShape_ == 1 ? "50%" : "5dp");
        grid->SetProperty("overflow", "hidden");
    }
}

void RmlUiRuntime::SetStatus(const std::string& status) {
    if (document_ == nullptr) return;
    if (Rml::Element* element = document_->GetElementById("stats")) element->SetInnerRML(status);
}

void RmlUiRuntime::SetTargetBlock(const std::string& name, float progress) {
    if (document_ == nullptr) return;
    Rml::Element* target = document_->GetElementById("target");
    Rml::Element* label = document_->GetElementById("target-name");
    Rml::Element* fill = document_->GetElementById("target-progress-fill");
    if (target == nullptr || label == nullptr || fill == nullptr) return;
    if (name.empty()) {
        target->SetProperty("display", "none");
        return;
    }
    label->SetInnerRML(name);
    target->SetProperty("display", "block");
    const float percent = std::clamp(progress, 0.0f, 1.0f) * 100.0f;
    fill->SetProperty("width", std::to_string(percent) + "%");
}

void RmlUiRuntime::SetSelectedItem(const std::string& markup) {
    if (document_ == nullptr) return;
    if (Rml::Element* element = document_->GetElementById("selected-item")) {
        element->SetInnerRML(markup);
        element->SetProperty("display", markup.empty() ? "none" : "flex");
    }
}

void RmlUiRuntime::SetPickupToast(const std::string& text) {
    if (document_ == nullptr) return;
    if (Rml::Element* element = document_->GetElementById("pickup")) {
        element->SetInnerRML(text);
        element->SetProperty("display", text.empty() ? "none" : "block");
    }
}

void RmlUiRuntime::SetPerspectiveEnabled(bool enabled) {
    if (document_ == nullptr) return;
    if (Rml::Element* element = document_->GetElementById("perspective")) {
        element->SetProperty("background-color", enabled ? "#d9e9a455" : "#1b2a3733");
        element->SetProperty("border", enabled ? "1dp #d9e9a4" : "1dp #b9c9d855");
    }
}

void RmlUiRuntime::SetCrouchEnabled(bool enabled) {
    if (document_ == nullptr) return;
    if (Rml::Element* element = document_->GetElementById("crouch")) {
        element->SetProperty("background-color", enabled ? "#8bbf75ee" : "#20394ce8");
        element->SetProperty("border", enabled ? "2dp #e2ffb0" : "1dp #c3d8e888");
        element->SetProperty("color", enabled ? "#102119" : "#f5fbff");
    }
}

void RmlUiRuntime::SetChestContents(const std::string& markup) {
    if (document_ == nullptr) return;
    Rml::Element* container = document_->GetElementById("starter-chest-items");
    if (container == nullptr) return;
    container->SetInnerRML(markup);
    containerListeners_.clear();
    for (int slot = 0; slot < 27; ++slot) {
        const std::string elementId = "container-slot-" + std::to_string(slot);
        Rml::Element* element = document_->GetElementById(elementId);
        if (element == nullptr) continue;
        containerListeners_.push_back(
            std::make_unique<ActionListener>(callback_, elementId));
        element->AddEventListener("click", containerListeners_.back().get());
    }
}

void RmlUiRuntime::SetMinimap(const std::string& gridMarkup, const std::string& coordinates) {
    if (document_ == nullptr) return;
    if (Rml::Element* minimap = document_->GetElementById("minimap")) {
        minimap->SetProperty("display", "flex");
        minimap->SetProperty("visibility", "visible");
        minimap->SetProperty("opacity", "1");
    }
    if (Rml::Element* grid = document_->GetElementById("minimap-grid"))
        grid->SetInnerRML(gridMarkup);
    if (Rml::Element* label = document_->GetElementById("minimap-coords"))
        label->SetInnerRML(coordinates);
}

void RmlUiRuntime::SetChatMessage(const std::string& message) {
    if (document_ == nullptr) return;
    if (Rml::Element* element = document_->GetElementById("chat-message")) {
        element->SetInnerRML(message);
        element->SetProperty("display", message.empty() ? "none" : "block");
    }
}

void RmlUiRuntime::SetChestButtonVisible(bool visible) {
    if (document_ == nullptr) return;
    if (Rml::Element* element = document_->GetElementById("starter-chest-open"))
        element->SetProperty("display", visible ? "block" : "none");
}

void RmlUiRuntime::SetQuickbarSlot(int slot,const std::string& label,bool selected) {
    if(document_==nullptr||slot<0||slot>=9)return;
    const std::string id="slot-"+std::to_string(slot);
    if(Rml::Element* element=document_->GetElementById(id)) {
        element->SetInnerRML(label);
        element->SetProperty("border", selected ? "2dp #fff0c8" : "1dp #98a8b7bb");
        element->SetProperty("background-color", selected ? "#596f4bf2" : "#101923e8");
        element->SetProperty("border-radius", "3dp");
    }
}

void RmlUiRuntime::SetInventoryContents(const std::string& markup) {
    if (document_ == nullptr) return;
    Rml::Element* container = document_->GetElementById("inventory-items");
    if (container == nullptr) return;
    container->SetInnerRML(markup);
    inventoryListeners_.clear();
    craftingListeners_.clear();
    // Creative mode creates one native select action for each placeable block.
    for (int id = 1; id < 512; ++id) {
        const std::string elementId = "creative-select-" + std::to_string(id);
        Rml::Element* element = document_->GetElementById(elementId);
        if (element == nullptr) continue;
        const std::string action = elementId;
        inventoryListeners_.push_back(std::make_unique<ActionListener>(callback_, action));
        element->AddEventListener("click", inventoryListeners_.back().get());
    }
    for (int slot = 0; slot < 36; ++slot) {
        const std::string elementId = "inventory-slot-" + std::to_string(slot);
        Rml::Element* element = document_->GetElementById(elementId);
        if (element == nullptr) continue;
        inventoryListeners_.push_back(std::make_unique<ActionListener>(callback_, elementId));
        element->AddEventListener("click", inventoryListeners_.back().get());
    }
    for (int slot = 0; slot < 4; ++slot) {
        const std::string elementId = "gear-slot-" + std::to_string(slot);
        Rml::Element* element = document_->GetElementById(elementId);
        if (element == nullptr) continue;
        inventoryListeners_.push_back(std::make_unique<ActionListener>(callback_, elementId));
        element->AddEventListener("click", inventoryListeners_.back().get());
    }
}

void RmlUiRuntime::SetCraftingContents(const std::string& markup) {
    if (document_ == nullptr) return;
    Rml::Element* container = document_->GetElementById("crafting-recipes");
    if (container == nullptr) return;
    container->SetInnerRML(markup);
    craftingListeners_.clear();
    for (int index = 0; index < 128; ++index) {
        const std::string elementId = "crafting-recipe-" + std::to_string(index);
        Rml::Element* element = document_->GetElementById(elementId);
        if (element == nullptr) continue;
        const std::string action = element->GetAttribute<Rml::String>("data-action", "");
        if (action.empty()) continue;
        craftingListeners_.push_back(std::make_unique<ActionListener>(callback_, action));
        element->AddEventListener("click", craftingListeners_.back().get());
    }
}

void RmlUiRuntime::SetEquipmentContents(const std::string& markup) {
    if (document_ == nullptr) return;
    if (Rml::Element* element = document_->GetElementById("equipment-items"))
        element->SetInnerRML(markup);
}

void RmlUiRuntime::SetInventoryCatalogContents(const std::string& markup) {
    if (document_ == nullptr) return;
    if (Rml::Element* element = document_->GetElementById("inventory-catalog"))
        element->SetInnerRML(markup);
}

void RmlUiRuntime::SetPanelVisible(const char* elementId, bool visible) {
    if (document_ == nullptr || elementId == nullptr) return;
    if (Rml::Element* element = document_->GetElementById(elementId))
        element->SetProperty("display", visible ? "block" : "none");
}

} // namespace minefade
