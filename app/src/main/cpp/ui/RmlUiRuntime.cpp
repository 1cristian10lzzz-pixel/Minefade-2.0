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
#minimap { position: absolute; z-index: 5; top: 54dp; left: 10dp; width: 88dp; padding: 5dp; background: #1a344bc9; border: 1dp #d7e8ef88; border-radius: 10dp; }
#minimap-grid { width: 78dp; height: 78dp; display: flex; flex-direction: row; flex-wrap: wrap; gap: 1dp; border: 1dp #e2f0f4aa; border-radius: 5dp; overflow: hidden; background: #173047; }
#minimap-north { position: absolute; right: 4dp; top: 3dp; width: 15dp; height: 15dp; text-align: center; color: #ffffff; font-size: 9dp; background: #142638cc; border-radius: 8dp; }
.map-tile { width: 10dp; height: 10dp; border-radius: 1dp; }
.map-low { background: #6c8064; }
.map-mid { background: #52805b; }
.map-high { background: #8b9185; }
.map-water { background: #326f9a; }
.map-player { background: #d9ed94; border: 1dp #f8ffe8; border-radius: 5dp; }
#minimap-coords { margin-top: 3dp; text-align: center; color: #d4e4e9; font-size: 7dp; }
.actions { display: flex; flex-direction: row; align-items: center; gap: 6dp; }
button { color: #f7fbff; background: #142638bb; border: 1dp #d7e8ef66; border-radius: 9dp; padding: 6dp 9dp; font-size: 11dp; }
button, .slot, #minimap, .panel, #chat-message { transition: background-color 0.14s linear, border-color 0.14s linear, opacity 0.18s linear; }
.slot { transition: background-color 0.14s linear, border-color 0.14s linear; }
#target-progress-fill { transition: width 0.10s linear; }
#pickup { transition: opacity 0.16s linear; }
button:hover { background: #365a77; }
button:active { background: #47789b; }
#crosshair { display: none; position: absolute; left: 50%; top: 50%; width: 12dp; height: 12dp; margin-left: -6dp; margin-top: -6dp; pointer-events: none; }
#crosshair-v { position: absolute; left: 5dp; top: 0dp; width: 2dp; height: 12dp; background: #ffffffcc; }
#crosshair-h { position: absolute; left: 0dp; top: 5dp; width: 12dp; height: 2dp; background: #ffffffcc; }
#hint { display: none; }
.panel { position: absolute; left: 50%; top: 50%; width: 310dp; margin-left: -155dp; margin-top: -112dp; padding: 16dp; background: #101d2bf5; border: 1dp #8ca9bd99; border-radius: 16dp; display: none; }
.panel h1 { font-size: 20dp; margin: 0dp 0dp 10dp 0dp; color: #ffffff; }
.panel p { font-size: 12dp; color: #c8d3db; margin: 3dp 0dp 12dp 0dp; }
.panel .actions { flex-wrap: wrap; }
#quickbar { position: absolute; z-index: 7; bottom: 10dp; left: 50%; margin-left: -150dp; display: flex; flex-direction: row; gap: 2dp; padding: 3dp; background: #15263836; border: 1dp #dcebf044; border-radius: 9dp; }
.slot { position: relative; width: 30dp; min-width: 20dp; height: 41dp; text-align: center; padding: 2dp; display: flex; align-items: center; justify-content: center; background: #223b52d8; border: 1dp #b5cad877; border-radius: 6dp; color: #ffffff; font-size: 9dp; }
.slot:active { background: #486b78; }
.slot img { width: 22dp; height: 22dp; display: block; margin: auto; }
.slot .slot-count { position: absolute; right: 1dp; bottom: 0dp; color: #ffffff; font-size: 7dp; text-shadow: 0dp 1dp 2dp #000000; }
.slot-durability { position: absolute; left: 3dp; right: 3dp; bottom: 2dp; height: 3dp; background: #000000aa; border-radius: 2dp; overflow: hidden; }
.slot-durability span { display: block; height: 3dp; background: #9ad27d; }
.inventory-row { position: relative; flex-wrap: wrap; }
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
#selected-item { position: absolute; z-index: 6; left: 50%; bottom: 59dp; margin-left: -76dp; width: 152dp; display: none; flex-direction: row; align-items: center; justify-content: center; gap: 6dp; color: #ffffff; font-size: 10dp; text-shadow: 0dp 1dp 3dp #000000; pointer-events: none; }
#selected-item img { width: 22dp; height: 22dp; }
#pickup { position: absolute; z-index: 5; right: 12dp; bottom: 54dp; color: #ffffff; font-size: 11dp; text-shadow: 0dp 1dp 3dp #000000; background: #102131bb; border: 1dp #d9e9a455; border-radius: 9dp; padding: 6dp 9dp; pointer-events: none; }

#inventory-panel { z-index: 9; }
#starter-chest-panel { z-index: 11; width: 280dp; margin-left: -140dp; max-height: 70%; }
#starter-chest-panel .actions { justify-content: center; }
#starter-chest-panel button { min-width: 104dp; }
#chat-message { position: absolute; z-index: 12; left: 50%; bottom: 62dp; width: 78%; margin-left: -39%; padding: 7dp 10dp; border-radius: 10dp; border: 1dp #d9e9a466; background: #101d2be8; color: #f3f7f8; font-size: 10dp; text-align: center; display: none; pointer-events: none; }
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
<div id="minimap"><div id="minimap-grid"></div><div id="minimap-north">N</div><div id="minimap-coords">X 0 · Z 0</div></div>
<div id="crosshair"><div id="crosshair-v"></div><div id="crosshair-h"></div></div>
<div id="movement-pad">
  <div id="move-up" class="pad-arrow">↑</div><div id="move-down" class="pad-arrow">↓</div>
  <div id="move-left" class="pad-arrow">←</div><div id="move-right" class="pad-arrow">→</div>
  <button id="crouch">▣</button>
</div>
<div id="hint"></div>
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
  <div class="inventory-layout">
    <div class="inventory-left">
      <div class="inventory-heading">PERSONAJE · EQUIPO</div>
      <div class="inventory-equipment" id="equipment-items"></div>
      <div class="inventory-heading">INVENTARIO · 36 ESPACIOS</div>
      <div id="inventory-items" class="inventory-grid"></div>
      <div class="inventory-actions"><button id="crafting-open">Fabricar</button><button id="eat">Comer</button><button id="inventory-close">Cerrar</button></div>
    </div>
    <div class="inventory-right">
      <div class="inventory-heading">CATÁLOGO · OBJETOS</div>
      <div id="inventory-catalog" class="inventory-grid"></div>
    </div>
  </div>
</div>
<div id="crafting-panel" class="panel">
  <div class="recipe-title">MESA DE TRABAJO · RECETAS</div>
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
  <div id="starter-chest-items"></div>
  <div class="actions">
    <button id="starter-chest-take">Recoger todo</button>
    <button id="starter-chest-close">Cerrar</button>
  </div>
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
#stats { position: absolute; left: 10dp; top: 10dp; display: block; font-size: 10dp; color: #ffffff; }
#movement-pad { position: absolute; left: 14dp; bottom: 54dp; width: 132dp; height: 132dp; border-radius: 18dp; pointer-events: none; }
.pad-arrow { position: absolute; width: 42dp; height: 42dp; text-align: center; background: rgba(25, 50, 73, 0.88); border: 1dp solid #e1efb8; border-radius: 11dp; font-size: 20dp; }
#move-up { left: 45dp; top: 2dp; } #move-down { left: 45dp; bottom: 2dp; }
#move-left { left: 2dp; top: 45dp; } #move-right { right: 2dp; top: 45dp; }
#quickbar { position: absolute; bottom: 10dp; left: 50%; margin-left: -150dp; display: flex; gap: 2dp; padding: 4dp; background: #1b3044d9; border: 1dp #dcebf088; border-radius: 11dp; }
.slot { width: 30dp; min-width: 30dp; height: 39dp; margin-right: 0dp; padding: 2dp; background: #2a4259ed; border: 1dp #d8e6ee99; border-radius: 7dp; }
.slot img { width: 22dp; height: 22dp; }
#jump { position: absolute; right: 22dp; bottom: 60dp; width: 56dp; height: 56dp; font-size: 20dp; }
#attack { position: absolute; right: 24dp; bottom: 126dp; width: 50dp; height: 50dp; font-size: 17dp; }
#movement-pad #crouch { position: absolute; left: 45dp; top: 45dp; right: auto; bottom: auto; width: 42dp; height: 42dp; font-size: 15dp; pointer-events: auto; }
#minimap { position: absolute; top: 54dp; left: 10dp; width: 64dp; height: 64dp; border: 1dp solid #ffffff88; background-color: rgba(18, 43, 64, 0.70); }
#target { position: absolute; top: 58%; left: 50%; margin-left: -70dp; width: 140dp; text-align: center; display: none; }
#selected-item { position: absolute; left: 50%; bottom: 58dp; margin-left: -76dp; width: 152dp; text-align: center; display: none; }
#pickup { position: absolute; right: 12dp; bottom: 54dp; display: none; }
.panel { position: absolute; left: 50%; top: 50%; width: 280dp; margin-left: -140dp; margin-top: -100dp; padding: 12dp; border: 1dp solid #ffffffaa; border-radius: 12dp; background-color: rgba(21, 41, 61, 0.96); display: none; }
#pause-panel .actions { display: flex; }
#pause-panel button { width: 128dp; margin-right: 6dp; }
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
<div id="minimap"><div id="minimap-grid"></div><div id="minimap-north">N</div><div id="minimap-coords">X 0 · Z 0</div></div>
<div id="movement-pad"><div id="move-up" class="pad-arrow">↑</div><div id="move-down" class="pad-arrow">↓</div><div id="move-left" class="pad-arrow">←</div><div id="move-right" class="pad-arrow">→</div><button id="crouch">▣</button></div>
<div id="quickbar">
<button class="slot" id="slot-0">1</button><button class="slot" id="slot-1">2</button><button class="slot" id="slot-2">3</button>
<button class="slot" id="slot-3">4</button><button class="slot" id="slot-4">5</button><button class="slot" id="slot-5">6</button>
<button class="slot" id="slot-6">7</button><button class="slot" id="slot-7">8</button><button class="slot" id="slot-8">9</button>
</div>
<button id="jump">↑</button><button id="attack">⚔</button>
<div id="crosshair"></div>
<div id="target"><div id="target-name"></div><div id="target-progress-fill"></div></div>
<div id="selected-item"></div><div id="pickup"></div>
<div id="inventory-panel" class="panel">
<button id="inventory-close">Cerrar</button><div id="equipment-items"></div><div id="inventory-items"></div>
<div id="inventory-catalog"></div><button id="crafting-open">Fabricar</button>
</div>
<div id="crafting-panel" class="panel"><div id="crafting-recipes"></div><button id="crafting-close">Volver</button></div>
<div id="pause-panel" class="panel"><div class="actions"><button id="resume">Continuar</button><button id="save-exit">Guardar y salir</button></div></div>
<div id="starter-chest-panel" class="panel"><div id="starter-chest-items"></div>
<button id="starter-chest-take">Recoger</button><button id="starter-chest-close">Cerrar</button></div>
</body>
</rml>
)RML";
        document_ = context_->LoadDocumentFromMemory(kFallbackHud, "minefade://hud-safe.rml");
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
    AttachAction("inventory", "inventory");
    AttachAction("inventory-close", "inventory-close");
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
    __android_log_print(ANDROID_LOG_INFO, kTag, "RmlUi %s initialised with OpenGL ES backend", Rml::GetVersion().c_str());
    return true;
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
    // Context/document destruction invokes listener detach callbacks, so listeners must outlive it.
    document_ = nullptr;
    if (context_ != nullptr) {
        Rml::RemoveContext("minefade_hud");
        context_ = nullptr;
    }
    listeners_.clear();
    inventoryListeners_.clear();
    craftingListeners_.clear();
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
    if (!ready_ || context_ == nullptr) return true;

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

void RmlUiRuntime::ApplyResponsiveLayout() {
    if (document_ == nullptr) return;
    const float availableDp = static_cast<float>(width_) / density_ - 20.0f;
    const float slotWidth = std::clamp((availableDp - 28.0f) / 9.0f, 19.0f, 30.0f);
    const float totalWidth = slotWidth * 9.0f + 28.0f;
    if (Rml::Element* quickbar = document_->GetElementById("quickbar")) {
        quickbar->SetProperty("margin-left", "-" + std::to_string(totalWidth * 0.5f) + "dp");
    }
    for (int slot = 0; slot < 9; ++slot) {
        const std::string id = "slot-" + std::to_string(slot);
        if (Rml::Element* element = document_->GetElementById(id)) {
            const std::string value = std::to_string(slotWidth) + "dp";
            element->SetProperty("width", value);
            element->SetProperty("min-width", value);
        }
    }
}

void RmlUiRuntime::UpdateAndRender() {
    if (!ready_ || context_ == nullptr) return;
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
    if (Rml::Element* element = document_->GetElementById("starter-chest-items"))
        element->SetInnerRML(markup);
}

void RmlUiRuntime::SetMinimap(const std::string& gridMarkup, const std::string& coordinates) {
    if (document_ == nullptr) return;
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
        element->SetProperty("border",selected?"2dp #d9e9a4":"1dp #ffffff33");
        element->SetProperty("background-color",selected?"#d9e9a422":"#10192322");
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
