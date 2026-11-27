package com.minefade.nativeengine;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Intent;
import android.net.Uri;
import android.app.AlertDialog;
import android.text.InputType;
import android.widget.EditText;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.graphics.Color;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;

/**
 * Activity lifecycle and native engine host.
 *
 * The home screen is shown before loading any native library or creating EGL.
 * This prevents a missing GL resource/library from presenting as a blank screen.
 */
public final class MainActivity extends Activity {
    private static final long ENGINE_TIMEOUT_MS = 18000L;

    private FrameLayout root;
    private MinefadeHomeScreen homeScreen;
    private MinefadeSurfaceView surfaceView;
    private SoundEffects soundEffects;
    private LanMultiplayerService lanMultiplayer;
    private FrameLayout loadingOverlay;
    private FrameLayout gameContainer;
    private FrameLayout fallbackOverlay;
    private final TextView[] fallbackQuickbarSlots = new TextView[9];
    private final View[] fallbackQuickbarViews = new View[9];
    private final View[] fallbackMovementViews = new View[4];
    private final java.util.HashMap<Integer, FallbackPointerState> fallbackPointers = new java.util.HashMap<>();
    private View fallbackCrouchView;
    private View fallbackJumpView;
    private View fallbackAttackView;
    private View fallbackInventoryView;
    private View fallbackPerspectiveView;
    private View fallbackPauseView;
    private TextView fallbackTargetLabel;
    private TextView fallbackLevelLabel;
    private TextView fallbackArmorLabel;
    private ProgressBar fallbackHealthBar;
    private ProgressBar fallbackHungerBar;
    private ProgressBar fallbackXpBar;
    private final java.util.ArrayList<String> chatMessages = new java.util.ArrayList<>();
    private TextView activeChatHistory;
    private TextView commandButton;
    private ScrollView activeChatScroll;
    private final Runnable progressionRefresh = new Runnable() {
        @Override public void run() {
            if (!activityResumed || !engineReady || surfaceView == null) return;
            surfaceView.requestProgressionSnapshot(MainActivity.this::updateProgressionPreferences);
            mainHandler.postDelayed(this, 1000L);
        }
    };
    private final Runnable fallbackQuickbarRefresh = new Runnable() {
        @Override public void run() {
            if (fallbackOverlay == null || surfaceView == null || !engineReady) return;
            surfaceView.requestQuickbarSnapshot(MainActivity.this::updateFallbackQuickbar);
            mainHandler.postDelayed(this, 650L);
        }
    };
    private final Runnable fallbackHudRefresh = new Runnable() {
        @Override public void run() {
            if (fallbackOverlay == null || surfaceView == null || !engineReady) return;
            surfaceView.requestHudSnapshot(MainActivity.this::updateFallbackHudSnapshot);
            mainHandler.postDelayed(this, 250L);
        }
    };
    private static final class FallbackPointerState {
        int holdAction = -1;
        int clickTag = 0;
        View target;
        boolean camera;
        float lastX;
        float lastY;
    }

    private static final int FALLBACK_TAG_INVENTORY = -101;
    private static final int FALLBACK_TAG_PAUSE = -102;
    private static final int FALLBACK_TAG_PERSPECTIVE = -103;
    private static final int FALLBACK_TAG_QUICKBAR_BASE = -200;

    private WorldManager.WorldInfo requestedWorld;
    private WorldManager worldManager;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private boolean engineReady;
    private boolean activityResumed;
    private boolean requestedMultiplayer;
    private int requestedPlayerSlot;
    private final Runnable exitPoll = new Runnable() {
        @Override public void run() {
            if (surfaceView == null || isFinishing()) return;
            try {
                if (NativeBridge.nativeConsumeExitRequest()) {
                    finish();
                    return;
                }
            } catch (LinkageError | RuntimeException error) {
                android.util.Log.e("Minefade", "Exit polling failed", error);
            }
            mainHandler.postDelayed(this, 200L);
        }
    };
    private final Runnable engineTimeout = () -> {
        if (!engineReady && surfaceView != null) {
            showEngineError("No se pudo completar el arranque del mundo.",
                    "Android no terminó de crear la superficie gráfica dentro del tiempo esperado. Comprueba que el dispositivo admite OpenGL ES 3.0 y que hay memoria disponible. Puedes reintentar; tus mundos no se eliminan.");
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().setBackgroundDrawable(new GradientDrawable(
                GradientDrawable.Orientation.TL_BR,
                new int[]{0xff0b1726, 0xff10283a, 0xff0b1422}));
        getWindow().setStatusBarColor(0xff0b1726);
        getWindow().setNavigationBarColor(0xff0b1118);
        applyImmersiveMode();

        root = new FrameLayout(this);
        root.setBackgroundColor(0xff0b1726);
        setContentView(root);
        soundEffects = new SoundEffects(this);
        worldManager = new WorldManager(this);
        lanMultiplayer = new LanMultiplayerService(this);

        // Deliberately do not touch NativeBridge here. The menu must still work
        // if the native library is missing on a device or an ABI is unsupported.
        showHome();
    }

    private void applyImmersiveMode() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    private void saveCurrentWorldPreview() {
        if (surfaceView == null || requestedWorld == null || !engineReady) return;
        java.io.File worldFile = new java.io.File(requestedWorld.getPath());
        java.io.File worldDirectory = worldFile.getParentFile();
        if (worldDirectory == null) return;
        java.io.File preview = new java.io.File(worldDirectory, "world-preview.jpg");
        if (!surfaceView.captureWorldPreview(preview)) {
            android.util.Log.w("Minefade", "World thumbnail was not updated for " + requestedWorld.name);
        }
    }

    private void showHome() {
        mainHandler.removeCallbacks(progressionRefresh);
        mainHandler.removeCallbacks(engineTimeout);
        mainHandler.removeCallbacks(exitPoll);
        mainHandler.removeCallbacks(fallbackQuickbarRefresh);
        if (surfaceView != null) {
            saveCurrentWorldPreview();
            try {
                if (!surfaceView.pauseNativeEngine()) {
                    surfaceView.resumeNativeEngine();
                    if (engineReady) mainHandler.post(exitPoll);
                    Toast.makeText(this,
                            "No se pudo confirmar el guardado. La partida sigue abierta; libera espacio e inténtalo otra vez.",
                            Toast.LENGTH_LONG).show();
                    return;
                }
                surfaceView.onPause();
            } catch (RuntimeException error) {
                android.util.Log.w("Minefade", "Could not pause surface while returning home", error);
                surfaceView.resumeNativeEngine();
                if (engineReady) mainHandler.post(exitPoll);
                Toast.makeText(this, "No se pudo guardar la partida. No se cerró el mundo.", Toast.LENGTH_LONG).show();
                return;
            }
        }
        engineReady = false;
        loadingOverlay = null;
        surfaceView = null;
        gameContainer = null;
        fallbackOverlay = null;
        commandButton = null;
        activeChatHistory = null;
        activeChatScroll = null;
        java.util.Arrays.fill(fallbackQuickbarSlots, null);
        root.removeAllViews();

        if (lanMultiplayer != null) {
            lanMultiplayer.close();
            lanMultiplayer = new LanMultiplayerService(this);
        }
        homeScreen = new MinefadeHomeScreen(this, new MinefadeHomeScreen.Listener() {
            @Override public void onPlay() {
                java.util.List<WorldManager.WorldInfo> worlds = worldManager.listWorlds();
                if (worlds.isEmpty()) {
                    WorldManager.WorldInfo created = worldManager.createWorld("Mi mundo", System.currentTimeMillis(),
                            WorldManager.SURVIVAL, false, true);
                    if (created != null) startWorld(created);
                } else startWorld(worlds.get(0));
            }
            @Override public void onPlayWorld(WorldManager.WorldInfo world) { startWorld(world); }
            @Override public void onCreateWorld(WorldManager.WorldInfo world) { startWorld(world); }
            @Override public void onHostWorldMultiplayer(WorldManager.WorldInfo world) { startLanHost(world); }
            @Override public void onImportMod() { pickModFile(); }
            @Override public void onImportShader() { pickShaderFile(); }
            @Override public void onHostMultiplayer() { startLanHost(); }
            @Override public void onSearchMultiplayer() { searchLanGames(); }
            @Override public void onJoinMultiplayer(LanMultiplayerService.GameInfo game) { joinLanGame(game); }
        }, this::playMenuClick);
        root.addView(homeScreen, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        applyImmersiveMode();
    }

    private void playMenuClick() {
        if (soundEffects != null) soundEffects.playMenuClick();
    }

    private void startGame() {
        startGame(false, 0);
    }

    private void startWorld(WorldManager.WorldInfo world) {
        if (world == null) return;
        worldManager.markPlayed(world);
        String compatibility = new ModManager(this).pinOrCompareWorldMods(world.getPath());
        if (compatibility != null && !compatibility.isEmpty()) {
            Toast.makeText(this, compatibility, Toast.LENGTH_LONG).show();
        }
        if (world.multiplayerEnabled) startLanHost(world);
        else startGame(false, 0, world);
    }

    private void startGame(boolean multiplayer, int playerSlot) {
        startGame(multiplayer, playerSlot, null);
    }

    private void startGame(boolean multiplayer, int playerSlot, WorldManager.WorldInfo world) {
        mainHandler.removeCallbacks(progressionRefresh);
        mainHandler.removeCallbacks(engineTimeout);
        mainHandler.removeCallbacks(exitPoll);
        if (surfaceView != null) {
            saveCurrentWorldPreview();
            try {
                if (!surfaceView.pauseNativeEngine()) {
                    surfaceView.resumeNativeEngine();
                    if (engineReady) mainHandler.post(exitPoll);
                    Toast.makeText(this,
                            "No se pudo guardar el mundo actual. No se cambiará de partida para proteger tus datos.",
                            Toast.LENGTH_LONG).show();
                    return;
                }
                surfaceView.onPause();
            } catch (RuntimeException error) {
                android.util.Log.e("Minefade", "World transition aborted because the current save could not be confirmed", error);
                surfaceView.resumeNativeEngine();
                if (engineReady) mainHandler.post(exitPoll);
                Toast.makeText(this, "No se pudo guardar la partida actual. Inténtalo otra vez.", Toast.LENGTH_LONG).show();
                return;
            }
        }
        root.removeAllViews();
        homeScreen = null;
        surfaceView = null;
        loadingOverlay = null;
        fallbackOverlay = null;
        engineReady = false;

        showLoading("PREPARANDO MINEFADE", "Comprobando el motor gráfico y los recursos…");

        requestedMultiplayer = multiplayer;
        requestedPlayerSlot = Math.max(0, Math.min(31, playerSlot));
        requestedWorld = world;

        // Native setup now happens on the GLSurfaceView thread immediately before
        // nativeInit. This keeps all Engine/JNI state changes serialised with GL.
        String worldPath = world == null ? getFilesDir().getAbsolutePath() : world.getPath();
        long worldSeed = world == null ? 0x4D464445L : world.seed;
        int gameMode = world == null ? WorldManager.SURVIVAL : world.gameMode;
        boolean allowCheats = world != null && world.allowCheats;
        boolean starterChest = world != null && world.starterChest;
        boolean flatWorld = world != null && world.flatWorld;
        int difficulty = world == null ? 2 : world.difficulty;
        boolean generateStructures = world == null || world.generateStructures;
        String modsPath = new java.io.File(getFilesDir(), "mods").getAbsolutePath();
        surfaceView = new MinefadeSurfaceView(this, soundEffects,
                getResources().getDisplayMetrics().density,
                multiplayer, requestedPlayerSlot, worldPath, modsPath, worldSeed,
                gameMode, allowCheats, starterChest, flatWorld, difficulty, generateStructures,
                multiplayer ? lanMultiplayer : null,
                new MinefadeSurfaceView.InitializationListener() {
                    @Override public void onEngineReady() {
                        engineReady = true;
                        mainHandler.removeCallbacks(engineTimeout);
                        mainHandler.postDelayed(exitPoll, 200L);
                        mainHandler.removeCallbacks(progressionRefresh);
                        mainHandler.post(progressionRefresh);
                        hideLoadingOverlay();
                        if (surfaceView != null && !surfaceView.isNativeHudAvailable()) {
                            android.util.Log.e("Minefade", "RmlUi unavailable; installing Android touch fallback: "
                                    + surfaceView.nativeHudDiagnostic());
                            if (commandButton != null) commandButton.setVisibility(View.VISIBLE);
                            showFallbackTouchControls();
                            Toast.makeText(MainActivity.this,
                                    "Se activaron los controles de respaldo para poder jugar.",
                                    Toast.LENGTH_LONG).show();
                        } else if (commandButton != null) {
                            commandButton.setVisibility(View.GONE);
                        }
                    }

                    @Override public void onEngineError(String message) {
                        engineReady = false;
                        showEngineError("No se pudo iniciar el mundo.", message);
                    }
                });
        gameContainer = new FrameLayout(this);
        gameContainer.addView(surfaceView, new FrameLayout.LayoutParams(-1, -1));
        // Compact icon-only chat entry: keep the real command dialog available
        // without the oversized text banner covering the world view.
        commandButton = statusButton("✉", false);
        commandButton.setTextSize(17);
        commandButton.setVisibility(View.GONE);
        FrameLayout.LayoutParams commandParams = new FrameLayout.LayoutParams(dp(38), dp(38),
                Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        commandParams.topMargin = dp(8);
        gameContainer.addView(commandButton, commandParams);
        commandButton.setContentDescription("Abrir chat y comandos");
        commandButton.setOnClickListener(v -> showCommandDialog());
        root.addView(gameContainer, new FrameLayout.LayoutParams(-1, -1));
        // The GL view is created lazily after Activity.onResume(). Activate it now:
        // otherwise GLSurfaceView remains paused and onSurfaceCreated() never runs.
        if (activityResumed) {
            surfaceView.onResume();
            android.util.Log.i("Minefade", "Activated newly-created GL surface");
        }
        if (loadingOverlay != null) root.bringChildToFront(loadingOverlay);
        mainHandler.postDelayed(engineTimeout, ENGINE_TIMEOUT_MS);
        applyImmersiveMode();
    }

    /**
     * Emergency Android-native controls used only when the RmlUi HUD could not
     * initialise. The world stays open and continues to receive touches on the
     * GL thread; this is a recovery path, not a replacement for the normal HUD.
     */
    private void showFallbackTouchControls() {
        if (gameContainer == null || surfaceView == null || fallbackOverlay != null) return;
        boolean leftHanded = getSharedPreferences("minefade_ui", MODE_PRIVATE)
                .getBoolean("left_handed_controls", false);
        fallbackPointers.clear();
        FrameLayout overlay = new FrameLayout(this);
        overlay.setClipChildren(false);
        overlay.setClipToPadding(false);
        overlay.setBackgroundColor(Color.TRANSPARENT);
        overlay.setClickable(true);
        overlay.setFocusable(false);

        // Centre reticle and targeted-block label are also present when RmlUi is unavailable.
        TextView crosshair = statusText("+", 25, Color.WHITE, true);
        crosshair.setGravity(Gravity.CENTER);
        crosshair.setShadowLayer(dp(3), 0f, dp(1), Color.BLACK);
        crosshair.setClickable(false);
        FrameLayout.LayoutParams crosshairParams = new FrameLayout.LayoutParams(dp(28), dp(28),
                Gravity.CENTER);
        overlay.addView(crosshair, crosshairParams);
        fallbackTargetLabel = statusText("", 11, Color.WHITE, true);
        fallbackTargetLabel.setGravity(Gravity.CENTER);
        fallbackTargetLabel.setPadding(dp(7), dp(4), dp(7), dp(4));
        fallbackTargetLabel.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{0xe51a2a38, 0xd9111b25}));
        fallbackTargetLabel.setVisibility(View.GONE);
        fallbackTargetLabel.setClickable(false);
        FrameLayout.LayoutParams targetParams = new FrameLayout.LayoutParams(-2, dp(26),
                Gravity.CENTER_HORIZONTAL | Gravity.CENTER_VERTICAL);
        targetParams.topMargin = dp(56);
        overlay.addView(fallbackTargetLabel, targetParams);

        // Health, food, armour and XP are real engine values, not decorative labels.
        LinearLayout vitals = new LinearLayout(this);
        vitals.setOrientation(LinearLayout.HORIZONTAL);
        vitals.setGravity(Gravity.CENTER_VERTICAL);
        vitals.setPadding(dp(7), dp(4), dp(7), dp(4));
        vitals.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{0xd91a2b3a, 0xc70d1824}));
        vitals.setClickable(false);
        TextView heart = statusText("♥", 12, 0xffff7780, true);
        vitals.addView(heart, new LinearLayout.LayoutParams(dp(15), dp(18)));
        fallbackHealthBar = fallbackProgressBar(0xffe45d69);
        LinearLayout.LayoutParams healthParams = new LinearLayout.LayoutParams(dp(52), dp(8));
        healthParams.rightMargin = dp(7);
        vitals.addView(fallbackHealthBar, healthParams);
        TextView food = statusText("◆", 11, 0xffffbd62, true);
        vitals.addView(food, new LinearLayout.LayoutParams(dp(14), dp(18)));
        fallbackHungerBar = fallbackProgressBar(0xffe5a34e);
        LinearLayout.LayoutParams hungerParams = new LinearLayout.LayoutParams(dp(52), dp(8));
        hungerParams.rightMargin = dp(7);
        vitals.addView(fallbackHungerBar, hungerParams);
        fallbackArmorLabel = statusText("⬟0", 8, 0xffd4e4f0, true);
        vitals.addView(fallbackArmorLabel, new LinearLayout.LayoutParams(-2, dp(18)));
        fallbackLevelLabel = statusText("NV 1", 8, 0xffd7f49a, true);
        LinearLayout.LayoutParams levelParams = new LinearLayout.LayoutParams(-2, dp(18));
        levelParams.leftMargin = dp(6);
        levelParams.rightMargin = dp(4);
        vitals.addView(fallbackLevelLabel, levelParams);
        fallbackXpBar = fallbackProgressBar(0xffa9e65c);
        vitals.addView(fallbackXpBar, new LinearLayout.LayoutParams(dp(38), dp(5)));
        FrameLayout.LayoutParams vitalsParams = new FrameLayout.LayoutParams(-2, dp(27),
                Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        vitalsParams.bottomMargin = dp(63);
        overlay.addView(vitals, vitalsParams);

        // Four-way D-pad; all touches are routed by the parent so multiple fingers
        // can hold movement and turn the camera at the same time.
        FrameLayout pad = new FrameLayout(this);
        FrameLayout.LayoutParams padParams = new FrameLayout.LayoutParams(dp(144), dp(144),
                Gravity.BOTTOM | (leftHanded ? Gravity.RIGHT : Gravity.LEFT));
        if (leftHanded) padParams.rightMargin = dp(16); else padParams.leftMargin = dp(16);
        padParams.bottomMargin = dp(66);
        overlay.addView(pad, padParams);
        TextView up = statusButton("↑", false);
        TextView down = statusButton("↓", false);
        TextView left = statusButton("←", false);
        TextView right = statusButton("→", false);
        TextView crouch = statusButton("▣", true);
        for (TextView button : new TextView[]{up, down, left, right, crouch}) {
            button.setTextSize(19);
            button.setPadding(0, 0, 0, 0);
            button.setClickable(false);
            button.setFocusable(false);
        }
        up.setTag(6); down.setTag(7); left.setTag(8); right.setTag(9); crouch.setTag(1);
        fallbackMovementViews[0] = up;
        fallbackMovementViews[1] = down;
        fallbackMovementViews[2] = left;
        fallbackMovementViews[3] = right;
        fallbackCrouchView = crouch;
        pad.addView(up, new FrameLayout.LayoutParams(dp(44), dp(44), Gravity.TOP | Gravity.CENTER_HORIZONTAL));
        pad.addView(down, new FrameLayout.LayoutParams(dp(44), dp(44), Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL));
        pad.addView(left, new FrameLayout.LayoutParams(dp(44), dp(44), Gravity.LEFT | Gravity.CENTER_VERTICAL));
        pad.addView(right, new FrameLayout.LayoutParams(dp(44), dp(44), Gravity.RIGHT | Gravity.CENTER_VERTICAL));
        pad.addView(crouch, new FrameLayout.LayoutParams(dp(42), dp(42), Gravity.CENTER));
        up.setContentDescription("Adelante"); down.setContentDescription("Atrás");
        left.setContentDescription("Mover a la izquierda"); right.setContentDescription("Mover a la derecha");
        crouch.setContentDescription("Agacharse");

        TextView jump = statusButton("↑", true);
        jump.setTextSize(22);
        jump.setTag(0);
        jump.setClickable(false);
        jump.setContentDescription("Saltar");
        fallbackJumpView = jump;
        FrameLayout.LayoutParams jumpParams = new FrameLayout.LayoutParams(dp(58), dp(58),
                Gravity.BOTTOM | (leftHanded ? Gravity.LEFT : Gravity.RIGHT));
        if (leftHanded) jumpParams.leftMargin = dp(18); else jumpParams.rightMargin = dp(18);
        jumpParams.bottomMargin = dp(62);
        overlay.addView(jump, jumpParams);

        TextView attack = statusButton("⚔", false);
        attack.setTextSize(17);
        attack.setTag(10);
        attack.setClickable(false);
        attack.setContentDescription("Atacar");
        fallbackAttackView = attack;
        FrameLayout.LayoutParams attackParams = new FrameLayout.LayoutParams(dp(48), dp(48),
                Gravity.BOTTOM | (leftHanded ? Gravity.LEFT : Gravity.RIGHT));
        if (leftHanded) attackParams.leftMargin = dp(82); else attackParams.rightMargin = dp(82);
        attackParams.bottomMargin = dp(126);
        overlay.addView(attack, attackParams);

        LinearLayout quickbar = new LinearLayout(this);
        quickbar.setOrientation(LinearLayout.HORIZONTAL);
        quickbar.setGravity(Gravity.CENTER);
        quickbar.setPadding(dp(4), dp(3), dp(4), dp(3));
        GradientDrawable quickbarBackground = new GradientDrawable();
        quickbarBackground.setColor(0x66162b3d);
        quickbarBackground.setCornerRadius(dp(8));
        quickbarBackground.setStroke(dp(1), 0x446d8aa0);
        quickbar.setBackground(quickbarBackground);
        quickbar.setClickable(false);
        FrameLayout.LayoutParams quickbarParams = new FrameLayout.LayoutParams(-2, dp(52),
                Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        quickbarParams.bottomMargin = dp(7);
        overlay.addView(quickbar, quickbarParams);
        final float density = getResources().getDisplayMetrics().density;
        final int screenWidthDp = Math.max(240, Math.round(getResources().getDisplayMetrics().widthPixels / density));
        final int slotWidthDp = Math.max(20, Math.min(36, (screenWidthDp - 30) / 9));
        for (int i = 0; i < 9; ++i) {
            TextView item = statusButton(Integer.toString(i + 1), false);
            item.setTextSize(7);
            item.setGravity(Gravity.CENTER);
            item.setMaxLines(2);
            item.setIncludeFontPadding(false);
            item.setElevation(0f);
            item.setClickable(false);
            item.setFocusable(false);
            item.setTag(FALLBACK_TAG_QUICKBAR_BASE - i);
            item.setContentDescription("Seleccionar espacio " + (i + 1));
            LinearLayout.LayoutParams itemParams = new LinearLayout.LayoutParams(dp(slotWidthDp), dp(43));
            if (i > 0) itemParams.leftMargin = dp(2);
            quickbar.addView(item, itemParams);
            fallbackQuickbarSlots[i] = item;
            fallbackQuickbarViews[i] = item;
        }

        LinearLayout topButtons = new LinearLayout(this);
        topButtons.setOrientation(LinearLayout.HORIZONTAL);
        topButtons.setGravity(Gravity.CENTER_VERTICAL);
        topButtons.setClickable(false);
        FrameLayout.LayoutParams topParams = new FrameLayout.LayoutParams(-2, dp(42),
                Gravity.TOP | Gravity.RIGHT);
        topParams.topMargin = dp(8);
        topParams.rightMargin = dp(8);
        overlay.addView(topButtons, topParams);
        TextView perspective = statusButton("◉", false);
        perspective.setTextSize(17);
        perspective.setTag(FALLBACK_TAG_PERSPECTIVE);
        perspective.setClickable(false);
        perspective.setContentDescription("Cambiar perspectiva");
        fallbackPerspectiveView = perspective;
        topButtons.addView(perspective, new LinearLayout.LayoutParams(dp(40), dp(40)));
        TextView inventory = statusButton("▦", false);
        inventory.setTextSize(17);
        inventory.setTag(FALLBACK_TAG_INVENTORY);
        inventory.setClickable(false);
        inventory.setContentDescription("Inventario");
        fallbackInventoryView = inventory;
        LinearLayout.LayoutParams inventoryParams = new LinearLayout.LayoutParams(dp(40), dp(40));
        inventoryParams.leftMargin = dp(6);
        topButtons.addView(inventory, inventoryParams);
        TextView pause = statusButton("Ⅱ", false);
        pause.setTextSize(17);
        pause.setTag(FALLBACK_TAG_PAUSE);
        pause.setClickable(false);
        pause.setContentDescription("Pausar");
        fallbackPauseView = pause;
        LinearLayout.LayoutParams pauseParams = new LinearLayout.LayoutParams(dp(40), dp(40));
        pauseParams.leftMargin = dp(6);
        topButtons.addView(pause, pauseParams);

        // One parent dispatcher owns the full gesture stream; Android child touch
        // listeners previously swallowed secondary fingers and blocked camera look.
        setFallbackChildrenPassive(overlay);
        overlay.setOnTouchListener((view, event) -> dispatchFallbackOverlayTouch(event, overlay));
        fallbackOverlay = overlay;
        gameContainer.addView(overlay, 1, new FrameLayout.LayoutParams(-1, -1));
        mainHandler.removeCallbacks(fallbackQuickbarRefresh);
        mainHandler.removeCallbacks(fallbackHudRefresh);
        mainHandler.post(fallbackQuickbarRefresh);
        mainHandler.post(fallbackHudRefresh);
    }

    private ProgressBar fallbackProgressBar(int tint) {
        ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setIndeterminate(false);
        bar.setMax(100);
        bar.setProgress(100);
        bar.setProgressTintList(ColorStateList.valueOf(tint));
        bar.setProgressBackgroundTintList(ColorStateList.valueOf(0xff263747));
        bar.setClickable(false);
        bar.setFocusable(false);
        return bar;
    }

    private void setFallbackChildrenPassive(View view) {
        if (view instanceof android.view.ViewGroup) {
            android.view.ViewGroup group = (android.view.ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i)
                setFallbackChildrenPassive(group.getChildAt(i));
        }
        if (view.getParent() != null) {
            view.setOnClickListener(null);
            view.setOnTouchListener(null);
            view.setClickable(false);
            view.setLongClickable(false);
            view.setFocusable(false);
        }
    }

    private View findFallbackTarget(View overlay, float x, float y) {
        int[] origin = new int[2];
        overlay.getLocationOnScreen(origin);
        return findFallbackTargetRecursive((android.view.ViewGroup) overlay,
                origin[0] + x, origin[1] + y);
    }

    private View findFallbackTargetRecursive(android.view.ViewGroup group, float screenX, float screenY) {
        for (int i = group.getChildCount() - 1; i >= 0; --i) {
            View child = group.getChildAt(i);
            if (child.getVisibility() != View.VISIBLE) continue;
            int[] location = new int[2];
            child.getLocationOnScreen(location);
            float right = location[0] + child.getWidth();
            float bottom = location[1] + child.getHeight();
            if (screenX < location[0] || screenX >= right ||
                    screenY < location[1] || screenY >= bottom) continue;
            if (child instanceof android.view.ViewGroup) {
                View nested = findFallbackTargetRecursive(
                        (android.view.ViewGroup) child, screenX, screenY);
                if (nested != null) return nested;
            }
            if (child.getTag() instanceof Integer) return child;
        }
        return null;
    }

    private boolean dispatchFallbackOverlayTouch(MotionEvent event, View overlay) {
        if (surfaceView == null || !engineReady) return true;
        final int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) {
            for (java.util.Map.Entry<Integer, FallbackPointerState> entry :
                    new java.util.ArrayList<>(fallbackPointers.entrySet())) {
                FallbackPointerState state = entry.getValue();
                if (state.holdAction >= 0) surfaceView.dispatchFallbackAction(state.holdAction, false);
                if (state.camera) surfaceView.dispatchFallbackTouch(3, entry.getKey(),
                        state.lastX, state.lastY);
            }
            fallbackPointers.clear();
            return true;
        }
        if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); ++i) {
                int pointerId = event.getPointerId(i);
                FallbackPointerState state = fallbackPointers.get(pointerId);
                if (state == null) continue;
                state.lastX = event.getX(i);
                state.lastY = event.getY(i);
                if (state.camera)
                    surfaceView.dispatchFallbackTouch(2, pointerId, state.lastX, state.lastY);
            }
            return true;
        }
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int index = event.getActionIndex();
            int pointerId = event.getPointerId(index);
            float x = event.getX(index);
            float y = event.getY(index);
            FallbackPointerState state = new FallbackPointerState();
            state.lastX = x;
            state.lastY = y;
            state.target = findFallbackTarget(overlay, x, y);
            if (state.target != null && state.target.getTag() instanceof Integer) {
                int tag = (Integer) state.target.getTag();
                if (tag >= 0) {
                    state.holdAction = tag;
                    surfaceView.dispatchFallbackAction(tag, true);
                } else {
                    state.clickTag = tag;
                }
            } else {
                state.camera = true;
                surfaceView.dispatchFallbackTouch(0, pointerId, x, y);
            }
            fallbackPointers.put(pointerId, state);
            return true;
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            int index = event.getActionIndex();
            int pointerId = event.getPointerId(index);
            float x = event.getX(index);
            float y = event.getY(index);
            FallbackPointerState state = fallbackPointers.remove(pointerId);
            if (state == null) return true;
            if (state.holdAction >= 0) {
                surfaceView.dispatchFallbackAction(state.holdAction, false);
            } else if (state.camera) {
                surfaceView.dispatchFallbackTouch(1, pointerId, x, y);
            } else if (state.clickTag != 0 &&
                    findFallbackTarget(overlay, x, y) == state.target) {
                performFallbackClick(state.clickTag);
            }
            return true;
        }
        return true;
    }

    private void performFallbackClick(int tag) {
        if (surfaceView == null || !engineReady) return;
        if (tag == FALLBACK_TAG_INVENTORY) showFallbackInventoryDialog();
        else if (tag == FALLBACK_TAG_PAUSE) showFallbackPauseDialog();
        else if (tag == FALLBACK_TAG_PERSPECTIVE)
            surfaceView.dispatchFallbackAction(2, true);
        else if (tag <= FALLBACK_TAG_QUICKBAR_BASE &&
                tag > FALLBACK_TAG_QUICKBAR_BASE - 9) {
            int slot = FALLBACK_TAG_QUICKBAR_BASE - tag;
            surfaceView.dispatchFallbackAction(20 + slot, true);
            surfaceView.requestQuickbarSnapshot(MainActivity.this::updateFallbackQuickbar);
        }
    }

    private void updateProgressionPreferences(String snapshot) {
        if (snapshot == null || snapshot.isEmpty()) return;
        String[] values = snapshot.split(java.util.regex.Pattern.quote("|"), -1);
        if (values.length != 7 || !"v1".equals(values[0])) return;
        try {
            int mined = Math.max(0, Integer.parseInt(values[1]));
            int placed = Math.max(0, Integer.parseInt(values[2]));
            int collected = Math.max(0, Integer.parseInt(values[3]));
            int jumps = Math.max(0, Integer.parseInt(values[4]));
            boolean entered = "1".equals(values[5]);
            int xp = Math.max(0, Integer.parseInt(values[6]));
            getSharedPreferences("minefade_ui", MODE_PRIVATE).edit()
                    .putInt("achievement_blocks_mined", mined)
                    .putInt("achievement_blocks_placed", placed)
                    .putInt("achievement_items_collected", collected)
                    .putInt("achievement_jumps", jumps)
                    .putBoolean("achievement_world_entered", entered)
                    .putInt("achievement_xp", xp)
                    .apply();
        } catch (NumberFormatException ignored) {
            android.util.Log.w("Minefade", "Ignored malformed progression snapshot");
        }
    }

    private void updateFallbackHudSnapshot(String snapshot) {
        if (snapshot == null || snapshot.isEmpty() || fallbackOverlay == null) return;
        String[] values = snapshot.split("\\|", 8);
        if (values.length < 8 || !"v1".equals(values[0])) return;
        try {
            int health = Math.max(0, Math.min(100, Integer.parseInt(values[1])));
            int hunger = Math.max(0, Math.min(100, Integer.parseInt(values[2])));
            int armor = Math.max(0, Integer.parseInt(values[3]));
            int level = Math.max(1, Integer.parseInt(values[4]));
            int xp = Math.max(0, Math.min(100, Integer.parseInt(values[5])));
            int targetProgress = Math.max(0, Math.min(100, Integer.parseInt(values[6])));
            String target = values[7] == null ? "" : values[7].trim();
            if (fallbackHealthBar != null) fallbackHealthBar.setProgress(health);
            if (fallbackHungerBar != null) fallbackHungerBar.setProgress(hunger);
            if (fallbackXpBar != null) fallbackXpBar.setProgress(xp);
            if (fallbackLevelLabel != null) fallbackLevelLabel.setText("NV " + level);
            if (fallbackArmorLabel != null) fallbackArmorLabel.setText("⬟" + armor);
            if (fallbackTargetLabel != null) {
                if (target.isEmpty()) {
                    fallbackTargetLabel.setText("");
                    fallbackTargetLabel.setVisibility(View.GONE);
                } else {
                    fallbackTargetLabel.setText(target +
                            (targetProgress > 0 ? " · " + targetProgress + "%" : ""));
                    fallbackTargetLabel.setVisibility(View.VISIBLE);
                }
            }
        } catch (NumberFormatException error) {
            android.util.Log.w("Minefade", "Ignored malformed fallback HUD snapshot", error);
        }
    }

    private String fallbackItemGlyph(String name) {
        String key = name == null ? "" : name.toLowerCase(java.util.Locale.ROOT);
        if (key.contains("espada")) return "⚔";
        if (key.contains("pico")) return "⛏";
        if (key.contains("hacha")) return "🪓";
        if (key.contains("pala") || key.contains("azada")) return "⚒";
        if (key.contains("armadura") || key.contains("casco") || key.contains("botas")) return "◆";
        if (key.contains("semilla") || key.contains("planta")) return "🌱";
        if (key.contains("zanahoria")) return "🥕";
        if (key.contains("patata")) return "🥔";
        if (key.contains("manzana") || key.contains("carne") || key.contains("pan")) return "●";
        if (key.contains("antorcha")) return "✦";
        if (key.contains("palo")) return "╱";
        if (key.contains("madera") || key.contains("tablón")) return "▤";
        if (key.contains("piedra") || key.contains("adoquín")) return "▦";
        return key.isEmpty() ? "—" : "◆";
    }

    private void updateFallbackQuickbar(String snapshot) {
        if (snapshot == null || snapshot.isEmpty()) return;
        String[] tokens = snapshot.split(";", -1);
        if (tokens.length < 10) return;
        int selected = 0;
        try { selected = Integer.parseInt(tokens[0]); } catch (NumberFormatException ignored) { }
        for (int i = 0; i < 9; i++) {
            TextView slot = fallbackQuickbarSlots[i];
            if (slot == null) continue;
            String[] item = tokens[i + 1].split("\\|", 4);
            String id = item.length >= 1 ? item[0] : "0";
            String amount = item.length >= 2 ? item[1] : "0";
            int tile = -1;
            try { if (item.length >= 3) tile = Integer.parseInt(item[2]); }
            catch (NumberFormatException ignored) { }
            String label = item.length >= 4 ? item[3] : "";
            Bitmap bitmap = !"0".equals(id) && surfaceView != null
                    ? surfaceView.createBlockIcon(tile) : null;
            if (bitmap != null) {
                BitmapDrawable drawable = new BitmapDrawable(getResources(), bitmap);
                drawable.setBounds(0, 0, dp(24), dp(24));
                slot.setCompoundDrawables(null, drawable, null, null);
                slot.setCompoundDrawablePadding(0);
                slot.setText((i + 1) + "\n" + (("0".equals(amount) || "1".equals(amount)) ? "" : amount));
            } else {
                slot.setCompoundDrawables(null, null, null, null);
                slot.setText((i + 1) + "\n" + ("0".equals(amount) ? "—" : fallbackItemGlyph(label))
                        + "\n" + ("0".equals(amount) ? "" : amount));
            }
            slot.setContentDescription("Espacio " + (i + 1) + ": " +
                    ("0".equals(amount) ? "vacío" : label + ", " + amount));
            int stroke = i == selected ? 0xffa8ed75 : 0x887d9bad;
            int[] colors = i == selected ? new int[]{0xff3f634a, 0xff1c4339}
                    : new int[]{0xff294358, 0xff162c40};
            GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, colors);
            bg.setCornerRadius(dp(6));
            bg.setStroke(dp(i == selected ? 2 : 1), stroke);
            slot.setBackground(bg);
        }
    }

    private void showFallbackInventoryDialog() {
        if (surfaceView == null || !engineReady) return;
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(10), dp(10), dp(10), dp(8));
        TextView summary = statusText("36 espacios · toca un objeto para equiparlo o moverlo",
                10, 0xffdce8f0, false);
        panel.addView(summary, new LinearLayout.LayoutParams(-1, dp(34)));
        LinearLayout grid = new LinearLayout(this);
        grid.setOrientation(LinearLayout.VERTICAL);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(false);
        scroll.addView(grid, new ScrollView.LayoutParams(-1, -2));
        panel.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1f));
        Button craft = new Button(this);
        craft.setText("FABRICACIÓN");
        craft.setAllCaps(false);
        panel.addView(craft, new LinearLayout.LayoutParams(-1, dp(42)));
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Inventario")
                .setView(panel)
                .setNegativeButton("Cerrar", null)
                .create();
        dialog.setOnDismissListener(shown -> {
            if (surfaceView != null && engineReady) surfaceView.dispatchFallbackAction(4, true);
        });
        craft.setOnClickListener(v -> {
            dialog.dismiss();
            showFallbackCraftingDialog();
        });
        dialog.show();
        surfaceView.dispatchFallbackAction(3, true);
        surfaceView.requestInventorySnapshot(snapshot ->
                renderFallbackInventoryGrid(grid, summary, snapshot));
    }

    private void renderFallbackInventoryGrid(LinearLayout grid, TextView summary, String snapshot) {
        if (grid == null || snapshot == null || snapshot.isEmpty()) {
            if (summary != null) summary.setText("No se pudo leer el inventario del motor.");
            return;
        }
        String[] tokens = snapshot.split(";", -1);
        if (tokens.length < 37 || !tokens[0].startsWith("v1|")) {
            summary.setText("El motor devolvió un inventario incompleto.");
            return;
        }
        int selected = 0;
        try { selected = Integer.parseInt(tokens[0].substring(3)); }
        catch (NumberFormatException ignored) { }
        grid.removeAllViews();
        summary.setText("36 espacios · seleccionado " + (selected + 1) +
                " · toca para equipar, mover o seleccionar");
        for (int rowIndex = 0; rowIndex < 9; rowIndex++) {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(-1, dp(72));
            rowParams.bottomMargin = dp(3);
            grid.addView(row, rowParams);
            for (int column = 0; column < 4; column++) {
                final int slotIndex = rowIndex * 4 + column;
                String[] value = tokens[slotIndex + 1].split("\\|", 7);
                if (value.length < 7) continue;
                int itemId = parseIntOr(value[1], 0);
                int amount = parseIntOr(value[2], 0);
                int durability = parseIntOr(value[3], 0);
                int maxDurability = parseIntOr(value[4], 0);
                int tile = parseIntOr(value[5], -1);
                String name = value[6];
                TextView cell = statusButton("", false);
                cell.setGravity(Gravity.CENTER);
                cell.setTextSize(8);
                cell.setMaxLines(3);
                cell.setIncludeFontPadding(false);
                cell.setPadding(dp(2), dp(2), dp(2), dp(2));
                cell.setClickable(true);
                cell.setFocusable(true);
                if (itemId > 0 && amount > 0) {
                    Bitmap bitmap = surfaceView == null ? null : surfaceView.createBlockIcon(tile);
                    if (bitmap != null) {
                        BitmapDrawable drawable = new BitmapDrawable(getResources(), bitmap);
                        drawable.setBounds(0, 0, dp(29), dp(29));
                        cell.setCompoundDrawables(null, drawable, null, null);
                        cell.setText(name + "\n×" + amount);
                    } else {
                        cell.setText(fallbackItemGlyph(name) + "\n" + name + "\n×" + amount);
                    }
                    if (maxDurability > 0)
                        cell.setContentDescription(name + ", " + amount + ", durabilidad " +
                                durability + " de " + maxDurability);
                    else cell.setContentDescription(name + ", " + amount);
                } else {
                    cell.setText("—\nVacío");
                    cell.setContentDescription("Espacio vacío " + (slotIndex + 1));
                }
                GradientDrawable background = new GradientDrawable(
                        GradientDrawable.Orientation.TOP_BOTTOM,
                        slotIndex == selected
                                ? new int[]{0xff42664c, 0xff1e4436}
                                : new int[]{0xff31485d, 0xff1a2b3b});
                background.setCornerRadius(dp(6));
                background.setStroke(dp(slotIndex == selected ? 2 : 1),
                        slotIndex == selected ? 0xffa8ed75 : 0x887d9bad);
                cell.setBackground(background);
                LinearLayout.LayoutParams cellParams =
                        new LinearLayout.LayoutParams(0, -1, 1f);
                if (column > 0) cellParams.leftMargin = dp(3);
                row.addView(cell, cellParams);
                cell.setOnClickListener(v -> {
                    if (surfaceView == null || !engineReady) return;
                    surfaceView.selectInventorySlot(slotIndex);
                    surfaceView.requestInventorySnapshot(next ->
                            renderFallbackInventoryGrid(grid, summary, next));
                    surfaceView.requestQuickbarSnapshot(MainActivity.this::updateFallbackQuickbar);
                });
            }
        }
    }

    private int parseIntOr(String value, int fallback) {
        try { return Integer.parseInt(value); }
        catch (NumberFormatException ignored) { return fallback; }
    }

    private void showFallbackCraftingDialog() {
        final String[] recipeIds = {
                "planks","sticks","table","torch","chest","furnace","glass","stone",
                "wooden_door","spruce_door","birch_door","jungle_door","acacia_door","dark_oak_door",
                "trapdoor","ladder","fence","fence_gate","wooden_slab","stone_slab",
                "oak_stairs","spruce_stairs","birch_stairs","jungle_stairs","stone_stairs",
                "stonebrick","stone_brick_stairs","sandstone","sandstone_stairs","cobblestone_wall",
                "iron_bars","glass_pane","iron_block","gold_block","copper_block","diamond_block",
                "emerald_block","redstone_block","coal_block","hopper","anvil","stonecutter",
                "dropper","piston","redstone_torch","redstone_lamp","wooden_pickaxe","stone_pickaxe",
                "iron_pickaxe","copper_pickaxe","diamond_pickaxe","wooden_axe","stone_axe","iron_axe",
                "wooden_sword","stone_sword","iron_sword","iron_ingot","gold_ingot","copper_ingot",
                "golden_rail","detector_rail","activator_rail"
        };
        final String[] labels = {
                "Tablones","Palos","Mesa de trabajo","Antorchas","Cofre","Horno","Cristal (horno)","Piedra (horno)",
                "Puerta","Puerta de abeto","Puerta de abedul","Puerta de jungla","Puerta de acacia","Puerta de roble oscuro",
                "Trampilla","Escalera de mano","Valla","Puerta de valla","Losa de madera","Losa de piedra (cortapiedras)",
                "Escaleras de roble","Escaleras de abeto","Escaleras de abedul","Escaleras de jungla","Escaleras de piedra",
                "Ladrillos de piedra","Escaleras de ladrillo de piedra","Arenisca","Escaleras de arenisca","Muro de adoquín",
                "Rejas de hierro","Paneles de cristal","Bloque de hierro","Bloque de oro","Bloque de cobre","Bloque de diamante",
                "Bloque de esmeralda","Bloque de redstone","Bloque de carbón","Tolva","Yunque","Cortapiedras",
                "Soltador","Pistón","Antorcha de redstone","Lámpara de redstone","Pico de madera","Pico de piedra",
                "Pico de hierro","Pico de cobre","Pico de diamante","Hacha de madera","Hacha de piedra","Hacha de hierro",
                "Espada de madera","Espada de piedra","Espada de hierro","Lingote de hierro (horno)","Lingote de oro (horno)",
                "Lingote de cobre (horno)","Raíl propulsor","Raíl detector","Raíl activador"
        };
        new AlertDialog.Builder(this).setTitle("Libro de fabricación")
                .setItems(labels, (dialog, which) -> {
                    if (surfaceView == null) return;
                    String result = surfaceView.executeCommand("/fabricar " + recipeIds[which]);
                    Toast.makeText(this, result, Toast.LENGTH_LONG).show();
                    mainHandler.post(fallbackQuickbarRefresh);
                }).setNegativeButton("Cerrar", null).show();
    }

    private void appendChatMessage(String sender, String message) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post(() -> appendChatMessage(sender, message));
            return;
        }
        String line = sender + ": " + message;
        chatMessages.add(line);
        while (chatMessages.size() > 80) chatMessages.remove(0);
        if (activeChatHistory != null) {
            activeChatHistory.setText(String.join("\n", chatMessages));
            if (activeChatScroll != null) activeChatScroll.post(
                    () -> activeChatScroll.fullScroll(View.FOCUS_DOWN));
        }
    }

    private boolean dispatchFallbackWorldTouch(MotionEvent event) {
        if (surfaceView == null || !engineReady) return true;
        final int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_CANCEL) {
            final int pointerAction = action == MotionEvent.ACTION_CANCEL ? 3 : 2;
            for (int i = 0; i < event.getPointerCount(); i++) {
                surfaceView.dispatchFallbackTouch(pointerAction, event.getPointerId(i), event.getX(i), event.getY(i));
            }
            return true;
        }
        final int mapped;
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: mapped = 0; break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: mapped = 1; break;
            case MotionEvent.ACTION_CANCEL: mapped = 3; break;
            default: return true;
        }
        final int index = event.getActionIndex();
        surfaceView.dispatchFallbackTouch(mapped, event.getPointerId(index),
                event.getX(index), event.getY(index));
        return true;
    }

    private void showFallbackPauseDialog() {
        if (surfaceView == null || !engineReady) return;
        surfaceView.dispatchFallbackAction(3, true);
        final boolean[] handled = {false};
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Minefade · Pausa")
                .setItems(new CharSequence[]{"Reanudar", "Guardar y salir"}, (shown, which) -> {
                    handled[0] = true;
                    if (which == 0) {
                        surfaceView.dispatchFallbackAction(4, true);
                    } else {
                        surfaceView.dispatchFallbackAction(5, true);
                    }
                })
                .setOnCancelListener(shown -> {
                    if (!handled[0] && surfaceView != null)
                        surfaceView.dispatchFallbackAction(4, true);
                })
                .create();
        dialog.show();
    }

    void showCommandDialog() {
        if (surfaceView == null || !engineReady) {
            Toast.makeText(this, "El mundo todavía se está preparando.", Toast.LENGTH_SHORT).show();
            return;
        }
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(12), dp(10), dp(12), dp(8));
        panel.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{0xff142638, 0xff0d1a28}));
        ScrollView historyScroll = new ScrollView(this);
        historyScroll.setFillViewport(false);
        historyScroll.setVerticalScrollBarEnabled(true);
        TextView history = statusText(chatMessages.isEmpty()
                ? "Chat local y multijugador\nEscribe un mensaje o /ayuda para los comandos."
                : String.join("\n", chatMessages), 10, 0xffdce8ef, false);
        history.setGravity(Gravity.TOP | Gravity.LEFT);
        history.setPadding(dp(8), dp(8), dp(8), dp(8));
        history.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{0xff0b1724, 0xff13283a}));
        historyScroll.addView(history, new ScrollView.LayoutParams(-1, -2));
        panel.addView(historyScroll, new LinearLayout.LayoutParams(-1, dp(220)));
        activeChatHistory = history;
        activeChatScroll = historyScroll;

        LinearLayout sendRow = new LinearLayout(this);
        sendRow.setOrientation(LinearLayout.HORIZONTAL);
        sendRow.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams sendRowParams = new LinearLayout.LayoutParams(-1, dp(46));
        sendRowParams.topMargin = dp(8);
        panel.addView(sendRow, sendRowParams);
        EditText messageInput = new EditText(this);
        messageInput.setSingleLine(true);
        messageInput.setTextColor(Color.WHITE);
        messageInput.setHintTextColor(0xff9fb4c1);
        messageInput.setHint("Mensaje o /comando");
        messageInput.setTextSize(12);
        messageInput.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        messageInput.setPadding(dp(10), 0, dp(10), 0);
        messageInput.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{0xff1b3347, 0xff132638}));
        sendRow.addView(messageInput, new LinearLayout.LayoutParams(0, -1, 1f));
        Button send = new Button(this);
        send.setText("ENVIAR");
        send.setTextSize(10);
        send.setTextColor(0xfff1f7fa);
        send.setAllCaps(false);
        LinearLayout.LayoutParams sendParams = new LinearLayout.LayoutParams(dp(88), dp(42));
        sendParams.leftMargin = dp(7);
        sendRow.addView(send, sendParams);

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Chat de Minefade")
                .setView(panel)
                .setNegativeButton("Cerrar", null)
                .create();
        final Runnable[] submit = new Runnable[1];
        submit[0] = () -> {
            String typed = messageInput.getText().toString().trim();
            if (typed.isEmpty()) return;
            if (typed.startsWith("/")) {
                String result = surfaceView.executeCommand(typed);
                appendChatMessage("Comando", "> " + typed + " · " + result);
            } else {
                appendChatMessage("Tú", typed);
                if (lanMultiplayer != null && lanMultiplayer.isConnected()) {
                    lanMultiplayer.sendChat(typed);
                } else {
                    Toast.makeText(this, "Mensaje guardado en el chat local.", Toast.LENGTH_SHORT).show();
                }
            }
            messageInput.setText("");
        };
        send.setOnClickListener(v -> submit[0].run());
        messageInput.setOnEditorActionListener((view, actionId, event) -> {
            submit[0].run();
            return true;
        });
        dialog.setOnDismissListener(d -> {
            activeChatHistory = null;
            activeChatScroll = null;
        });
        dialog.show();
        messageInput.requestFocus();
    }

    private void pickModFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, 7401);
    }

    private void pickShaderFile() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, 7402);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;
        if (requestCode == 7401) {
            ModManager manager = new ModManager(this);
            ModManager.ModInfo imported = manager.importMod(data.getData());
            if (imported == null) {
                Toast.makeText(this, "No se pudo importar. Usa un paquete .mfmod válido.", Toast.LENGTH_LONG).show();
            } else {
                Toast.makeText(this, "Mod importado: " + imported.name, Toast.LENGTH_LONG).show();
                if (homeScreen != null) homeScreen.refreshMods();
            }
        } else if (requestCode == 7402) {
            ShaderPackManager manager = new ShaderPackManager(this);
            ShaderPackManager.PackInfo imported = manager.importShader(data.getData());
            if (imported == null) {
                Toast.makeText(this, "No se pudo importar. Usa un perfil .mfshader v1 compatible.", Toast.LENGTH_LONG).show();
            } else {
                manager.activate(imported, getSharedPreferences("minefade_ui", MODE_PRIVATE));
                Toast.makeText(this, "Perfil shader activado: " + imported.name, Toast.LENGTH_LONG).show();
                if (homeScreen != null) homeScreen.refreshShaders();
            }
        }
    }

    private LanMultiplayerService.Listener createLanListener(boolean host) {
        return createLanListener(host, null);
    }

    private LanMultiplayerService.Listener createLanListener(boolean host, WorldManager.WorldInfo hostedWorld) {
        return new LanMultiplayerService.Listener() {
            @Override public void onStatus(String status) {
                if (homeScreen != null) homeScreen.setMultiplayerStatus(status);
            }
            @Override public void onGamesChanged(java.util.List<LanMultiplayerService.GameInfo> games) {
                if (homeScreen != null) homeScreen.setNearbyGames(games);
            }
            @Override public void onConnected(int localPlayerId) {
                if (host || localPlayerId > 0) startGame(true, localPlayerId, hostedWorld);
            }
            @Override public void onDisconnected(String reason) {
                if (homeScreen != null) homeScreen.setMultiplayerStatus(reason);
            }
            @Override public void onChatMessage(String player, String message) {
                appendChatMessage(player, message);
            }
        };
    }

    private void startLanHost() {
        startLanHost(null);
    }

    private void startLanHost(WorldManager.WorldInfo world) {
        if (lanMultiplayer == null) return;
        lanMultiplayer.setContentFingerprint(new ModManager(this).enabledModsFingerprint());
        long seed = world == null ? 0x4D464445L : world.seed;
        int mode = world == null ? WorldManager.SURVIVAL : world.gameMode;
        boolean cheats = world != null && world.allowCheats;
        boolean chest = world != null && world.starterChest;
        boolean flat = world != null && world.flatWorld;
        int difficulty = world == null ? 2 : world.difficulty;
        boolean structures = world == null || world.generateStructures;
        lanMultiplayer.startHost("Jugador", seed, mode, cheats, chest, flat,
                difficulty, structures, createLanListener(true, world));
        if (homeScreen != null) homeScreen.setMultiplayerStatus(world == null
                ? "Creando partida y publicándola en la red local…"
                : "Abriendo «" + world.name + "» en la red local…");
    }

    private void searchLanGames() {
        if (lanMultiplayer == null) return;
        lanMultiplayer.setContentFingerprint(new ModManager(this).enabledModsFingerprint());
        lanMultiplayer.discover(createLanListener(false));
        if (homeScreen != null) homeScreen.setNearbyGames(java.util.Collections.emptyList());
    }

    private void joinLanGame(LanMultiplayerService.GameInfo game) {
        if (lanMultiplayer == null || game == null) return;
        lanMultiplayer.setContentFingerprint(new ModManager(this).enabledModsFingerprint());
        WorldManager.WorldInfo localWorld = worldManager.createWorld(
                game.name + " (LAN)", game.worldSeed, game.gameMode, game.allowCheats,
                game.starterChest, game.flatWorld, game.difficulty, game.generateStructures, false);
        if (localWorld == null) {
            Toast.makeText(this, "No se pudo preparar el mundo compartido. Comprueba el espacio disponible.",
                    Toast.LENGTH_LONG).show();
            return;
        }
        lanMultiplayer.join(game, "Jugador", createLanListener(false, localWorld));
        if (homeScreen != null) homeScreen.setMultiplayerStatus("Conectando a " + game.name + "…");
    }

    private void showLoading(String title, String detail) {
        FrameLayout overlay = new FrameLayout(this);
        overlay.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{0xf20b1726, 0xf2122a3b, 0xf20b1726}));
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setGravity(Gravity.CENTER);
        card.setPadding(dp(24), dp(22), dp(24), dp(22));
        GradientDrawable cardBg = new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{0xff1c3548, 0xff13283a});
        cardBg.setCornerRadius(dp(20));
        cardBg.setStroke(dp(1), 0x77658da1);
        card.setBackground(cardBg);
        card.setElevation(dp(8));

        ProgressBar spinner = new ProgressBar(this);
        spinner.setIndeterminate(true);
        LinearLayout.LayoutParams spinnerParams = new LinearLayout.LayoutParams(dp(38), dp(38));
        spinnerParams.bottomMargin = dp(12);
        card.addView(spinner, spinnerParams);

        TextView titleView = statusText(title, 14, 0xffffffff, true);
        titleView.setGravity(Gravity.CENTER);
        card.addView(titleView);
        TextView detailView = statusText(detail, 10, 0xffb5c8d3, false);
        detailView.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams detailParams = new LinearLayout.LayoutParams(-1, -2);
        detailParams.topMargin = dp(7);
        card.addView(detailView, detailParams);

        FrameLayout.LayoutParams cardParams = new FrameLayout.LayoutParams(
                Math.min(getResources().getDisplayMetrics().widthPixels - dp(40), dp(420)),
                -2, Gravity.CENTER);
        overlay.addView(card, cardParams);
        loadingOverlay = overlay;
        root.addView(overlay, new FrameLayout.LayoutParams(-1, -1));
        root.bringChildToFront(overlay);
    }

    private void showEngineError(String title, String detail) {
        mainHandler.removeCallbacks(engineTimeout);
        if (loadingOverlay == null) showLoading(title, detail);
        loadingOverlay.removeAllViews();
        loadingOverlay.setBackground(new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{0xf20b1726, 0xf2122a3b, 0xf20b1726}));

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setGravity(Gravity.CENTER);
        card.setPadding(dp(22), dp(20), dp(22), dp(18));
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[]{0xff23384a, 0xff15283a});
        bg.setCornerRadius(dp(19));
        bg.setStroke(dp(1), 0xffa56e65);
        card.setBackground(bg);
        card.setElevation(dp(8));

        TextView symbol = statusText("!", 29, 0xffffb19a, true);
        symbol.setGravity(Gravity.CENTER);
        card.addView(symbol);
        TextView heading = statusText(title, 15, Color.WHITE, true);
        heading.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams hp = new LinearLayout.LayoutParams(-1, -2);
        hp.topMargin = dp(7);
        card.addView(heading, hp);
        TextView message = statusText(detail, 10, 0xffc4d1d9, false);
        message.setGravity(Gravity.CENTER);
        message.setTextIsSelectable(true);
        message.setMovementMethod(new android.text.method.ScrollingMovementMethod());
        ScrollView diagnosticScroll = new ScrollView(this);
        diagnosticScroll.setFillViewport(false);
        diagnosticScroll.setVerticalScrollBarEnabled(true);
        diagnosticScroll.setOverScrollMode(View.OVER_SCROLL_IF_CONTENT_SCROLLS);
        diagnosticScroll.addView(message, new ScrollView.LayoutParams(-1, -2));
        LinearLayout.LayoutParams mp = new LinearLayout.LayoutParams(-1, dp(112));
        mp.topMargin = dp(8);
        card.addView(diagnosticScroll, mp);

        TextView copyDiagnostics = statusButton("COPIAR DIAGNÓSTICO", false);
        LinearLayout.LayoutParams copyParams = new LinearLayout.LayoutParams(-1, dp(34));
        copyParams.topMargin = dp(9);
        card.addView(copyDiagnostics, copyParams);
        copyDiagnostics.setOnClickListener(v -> {
            ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            if (clipboard != null) {
                clipboard.setPrimaryClip(ClipData.newPlainText("Minefade - diagnóstico de arranque", detail));
                Toast.makeText(this, "Diagnóstico copiado", Toast.LENGTH_SHORT).show();
            }
        });

        LinearLayout actions = new LinearLayout(this);
        actions.setGravity(Gravity.CENTER);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams ap = new LinearLayout.LayoutParams(-1, dp(42));
        ap.topMargin = dp(16);
        card.addView(actions, ap);
        TextView retry = statusButton("REINTENTAR", true);
        retry.setOnClickListener(v -> startGame(requestedMultiplayer, requestedPlayerSlot, requestedWorld));
        actions.addView(retry, new LinearLayout.LayoutParams(0, -1, 1f));
        TextView back = statusButton("VOLVER", false);
        back.setOnClickListener(v -> showHome());
        LinearLayout.LayoutParams backParams = new LinearLayout.LayoutParams(0, -1, 1f);
        backParams.leftMargin = dp(8);
        actions.addView(back, backParams);

        FrameLayout.LayoutParams cardParams = new FrameLayout.LayoutParams(
                Math.min(getResources().getDisplayMetrics().widthPixels - dp(36), dp(440)),
                -2, Gravity.CENTER);
        loadingOverlay.addView(card, cardParams);
        root.bringChildToFront(loadingOverlay);
    }

    private TextView statusText(String value, int size, int color, boolean bold) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        view.setTypeface(android.graphics.Typeface.create("sans-serif",
                bold ? android.graphics.Typeface.BOLD : android.graphics.Typeface.NORMAL));
        view.setIncludeFontPadding(false);
        return view;
    }

    private TextView statusButton(String value, boolean primary) {
        TextView view = statusText(value, 10, primary ? 0xff112219 : 0xffedf4f7, true);
        view.setGravity(Gravity.CENTER);
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,
                primary ? new int[]{0xffb8f27b, 0xff81d15b}
                        : new int[]{0xff314b60, 0xff20384a});
        bg.setCornerRadius(dp(10));
        bg.setStroke(dp(1), primary ? 0xffd3ffac : 0xff55738a);
        view.setBackground(bg);
        return view;
    }

    private void hideLoadingOverlay() {
        if (loadingOverlay == null) return;
        FrameLayout old = loadingOverlay;
        loadingOverlay = null;
        old.animate().alpha(0f).setDuration(220L).withEndAction(() -> {
            if (old.getParent() == root) root.removeView(old);
        }).start();
    }

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    public void onBackPressed() {
        if (surfaceView != null) {
            showHome();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onPause() {
        activityResumed = false;
        mainHandler.removeCallbacks(exitPoll);
        mainHandler.removeCallbacks(engineTimeout);
        mainHandler.removeCallbacks(progressionRefresh);
        if (surfaceView != null) {
            saveCurrentWorldPreview();
            surfaceView.pauseNativeEngine();
            surfaceView.onPause();
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        activityResumed = true;
        applyImmersiveMode();
        if (surfaceView != null) {
            surfaceView.onResume();
            surfaceView.resumeNativeEngine();
            if (engineReady) {
                mainHandler.post(exitPoll);
                mainHandler.post(progressionRefresh);
            }
        }
    }

    @Override
    protected void onDestroy() {
        mainHandler.removeCallbacks(exitPoll);
        mainHandler.removeCallbacks(engineTimeout);
        mainHandler.removeCallbacks(progressionRefresh);
        if (lanMultiplayer != null) lanMultiplayer.close();
        if (soundEffects != null) soundEffects.release();
        super.onDestroy();
    }
}
