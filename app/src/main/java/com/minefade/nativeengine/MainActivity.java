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
    private final java.util.ArrayList<String> chatMessages = new java.util.ArrayList<>();
    private TextView activeChatHistory;
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
                            showFallbackTouchControls();
                            Toast.makeText(MainActivity.this,
                                    "Se activaron los controles de respaldo para poder jugar.",
                                    Toast.LENGTH_LONG).show();
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
        TextView commandButton = statusButton("✉", false);
        commandButton.setTextSize(17);
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
        FrameLayout overlay = new FrameLayout(this);
        overlay.setClipChildren(false);
        overlay.setClipToPadding(false);
        overlay.setBackgroundColor(Color.TRANSPARENT);
        overlay.setClickable(true);
        overlay.setOnTouchListener((view, event) -> dispatchFallbackWorldTouch(event));

        // Minecraft-style D-pad: four separate directions and crouch in the centre.
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
        }
        pad.addView(up, new FrameLayout.LayoutParams(dp(44), dp(44), Gravity.TOP | Gravity.CENTER_HORIZONTAL));
        pad.addView(down, new FrameLayout.LayoutParams(dp(44), dp(44), Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL));
        pad.addView(left, new FrameLayout.LayoutParams(dp(44), dp(44), Gravity.LEFT | Gravity.CENTER_VERTICAL));
        pad.addView(right, new FrameLayout.LayoutParams(dp(44), dp(44), Gravity.RIGHT | Gravity.CENTER_VERTICAL));
        pad.addView(crouch, new FrameLayout.LayoutParams(dp(42), dp(42), Gravity.CENTER));
        attachFallbackHoldAction(up, 6);
        attachFallbackHoldAction(down, 7);
        attachFallbackHoldAction(left, 8);
        attachFallbackHoldAction(right, 9);
        attachFallbackHoldAction(crouch, 1);
        up.setContentDescription("Adelante"); down.setContentDescription("Atrás");
        left.setContentDescription("Mover a la izquierda"); right.setContentDescription("Mover a la derecha");
        crouch.setContentDescription("Agacharse");

        TextView jump = statusButton("↑", true);
        jump.setTextSize(22);
        jump.setContentDescription("Saltar");
        FrameLayout.LayoutParams jumpParams = new FrameLayout.LayoutParams(dp(58), dp(58),
                Gravity.BOTTOM | (leftHanded ? Gravity.LEFT : Gravity.RIGHT));
        if (leftHanded) jumpParams.leftMargin = dp(18); else jumpParams.rightMargin = dp(18);
        jumpParams.bottomMargin = dp(62);
        overlay.addView(jump, jumpParams);
        attachFallbackHoldAction(jump, 0);

        TextView attack = statusButton("⚔", false);
        attack.setTextSize(17);
        attack.setContentDescription("Atacar");
        FrameLayout.LayoutParams attackParams = new FrameLayout.LayoutParams(dp(48), dp(48),
                Gravity.BOTTOM | (leftHanded ? Gravity.LEFT : Gravity.RIGHT));
        if (leftHanded) attackParams.leftMargin = dp(82); else attackParams.rightMargin = dp(82);
        attackParams.bottomMargin = dp(126);
        overlay.addView(attack, attackParams);
        attachFallbackHoldAction(attack, 10);

        LinearLayout quickbar = new LinearLayout(this);
        quickbar.setOrientation(LinearLayout.HORIZONTAL);
        quickbar.setGravity(Gravity.CENTER);
        quickbar.setPadding(dp(4), dp(3), dp(4), dp(3));
        GradientDrawable quickbarBackground = new GradientDrawable();
        quickbarBackground.setColor(0x66162b3d);
        quickbarBackground.setCornerRadius(dp(8));
        quickbarBackground.setStroke(dp(1), 0x446d8aa0);
        quickbar.setBackground(quickbarBackground);
        FrameLayout.LayoutParams quickbarParams = new FrameLayout.LayoutParams(-2, dp(52),
                Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        quickbarParams.bottomMargin = dp(7);
        overlay.addView(quickbar, quickbarParams);
        final float density = getResources().getDisplayMetrics().density;
        final int screenWidthDp = Math.max(240, Math.round(getResources().getDisplayMetrics().widthPixels / density));
        final int slotWidthDp = Math.max(20, Math.min(36, (screenWidthDp - 30) / 9));
        for (int i = 0; i < 9; ++i) {
            final int slot = i;
            TextView item = statusButton(Integer.toString(i + 1), false);
            item.setTextSize(6);
            item.setGravity(Gravity.CENTER);
            item.setMaxLines(3);
            item.setIncludeFontPadding(false);
            item.setElevation(0f);
            item.setContentDescription("Seleccionar espacio " + (i + 1));
            LinearLayout.LayoutParams itemParams = new LinearLayout.LayoutParams(dp(slotWidthDp), dp(43));
            if (i > 0) itemParams.leftMargin = dp(2);
            quickbar.addView(item, itemParams);
            fallbackQuickbarSlots[i] = item;
            item.setOnClickListener(v -> {
                surfaceView.dispatchFallbackAction(20 + slot, true);
                surfaceView.requestQuickbarSnapshot(MainActivity.this::updateFallbackQuickbar);
            });
        }

        LinearLayout topButtons = new LinearLayout(this);
        topButtons.setOrientation(LinearLayout.HORIZONTAL);
        topButtons.setGravity(Gravity.CENTER_VERTICAL);
        FrameLayout.LayoutParams topParams = new FrameLayout.LayoutParams(-2, dp(42),
                Gravity.TOP | Gravity.RIGHT);
        topParams.topMargin = dp(8);
        topParams.rightMargin = dp(8);
        overlay.addView(topButtons, topParams);
        TextView perspective = statusButton("◉", false);
        perspective.setTextSize(17);
        perspective.setContentDescription("Cambiar perspectiva");
        topButtons.addView(perspective, new LinearLayout.LayoutParams(dp(40), dp(40)));
        perspective.setOnClickListener(v -> surfaceView.dispatchFallbackAction(2, true));
        TextView inventory = statusButton("▦", false);
        inventory.setTextSize(17);
        inventory.setContentDescription("Inventario");
        LinearLayout.LayoutParams inventoryParams = new LinearLayout.LayoutParams(dp(40), dp(40));
        inventoryParams.leftMargin = dp(6);
        topButtons.addView(inventory, inventoryParams);
        inventory.setOnClickListener(v -> showFallbackInventoryDialog());
        TextView pause = statusButton("Ⅱ", false);
        pause.setTextSize(17);
        pause.setContentDescription("Pausar");
        LinearLayout.LayoutParams pauseParams = new LinearLayout.LayoutParams(dp(40), dp(40));
        pauseParams.leftMargin = dp(6);
        topButtons.addView(pause, pauseParams);
        pause.setOnClickListener(v -> showFallbackPauseDialog());

        fallbackOverlay = overlay;
        gameContainer.addView(overlay, 1, new FrameLayout.LayoutParams(-1, -1));
        mainHandler.removeCallbacks(fallbackQuickbarRefresh);
        mainHandler.post(fallbackQuickbarRefresh);
    }

    private void attachFallbackHoldAction(View button, int action) {
        final int[] ownerPointer = {-1};
        button.setOnTouchListener((view, event) -> {
            final int state = event.getActionMasked();
            final int index = event.getActionIndex();
            if (state == MotionEvent.ACTION_DOWN) {
                ownerPointer[0] = event.getPointerId(index);
                surfaceView.dispatchFallbackAction(action, true);
            } else if ((state == MotionEvent.ACTION_POINTER_UP || state == MotionEvent.ACTION_UP)
                    && ownerPointer[0] == event.getPointerId(index)) {
                surfaceView.dispatchFallbackAction(action, false);
                ownerPointer[0] = -1;
            } else if (state == MotionEvent.ACTION_CANCEL) {
                if (ownerPointer[0] >= 0) surfaceView.dispatchFallbackAction(action, false);
                ownerPointer[0] = -1;
            }
            return true;
        });
    }

    private void updateProgressionPreferences(String snapshot) {
        if (snapshot == null || snapshot.isEmpty()) return;
        String[] values = snapshot.split("\\\\|", -1);
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

    private void updateFallbackQuickbar(String snapshot) {
        if (snapshot == null || snapshot.isEmpty()) return;
        String[] tokens = snapshot.split(";", -1);
        if (tokens.length < 10) return;
        int selected = 0;
        try { selected = Integer.parseInt(tokens[0]); } catch (NumberFormatException ignored) { }
        for (int i = 0; i < 9; i++) {
            TextView slot = fallbackQuickbarSlots[i];
            if (slot == null) continue;
            String[] item = tokens[i + 1].split("\\|", 3);
            String label = item.length >= 3 ? item[2] : "";
            String amount = item.length >= 2 ? item[1] : "0";
            if (label.length() > 5) label = label.substring(0, 5);
            slot.setText((i + 1) + "\n" + (label.isEmpty() ? "—" : label) + "\n"
                    + ("0".equals(amount) ? "" : amount));
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
        String inventory = surfaceView.executeCommand("inventario");
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(14), dp(12), dp(14), dp(12));
        TextView contents = statusText(inventory, 10, 0xffe2edf3, false);
        contents.setGravity(Gravity.TOP | Gravity.LEFT);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(contents, new ScrollView.LayoutParams(-1, -2));
        panel.addView(scroll, new LinearLayout.LayoutParams(-1, dp(240)));
        Button craft = new Button(this);
        craft.setText("FABRICACIÓN");
        craft.setAllCaps(false);
        panel.addView(craft, new LinearLayout.LayoutParams(-1, dp(44)));
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Inventario")
                .setView(panel)
                .setNegativeButton("Cerrar", null)
                .create();
        craft.setOnClickListener(v -> { dialog.dismiss(); showFallbackCraftingDialog(); });
        dialog.show();
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
