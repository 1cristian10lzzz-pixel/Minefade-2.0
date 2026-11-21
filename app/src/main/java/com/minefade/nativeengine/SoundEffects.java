package com.minefade.nativeengine;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.util.Log;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;
import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.Map;
import org.json.JSONObject;

/** Low-latency local game audio with load-state tracking and live master volume. */
public final class SoundEffects {
    private static final String TAG = "MinefadeAudio";
    private final SoundPool pool;
    private final SharedPreferences preferences;
    private final Set<Integer> loadedSamples = Collections.synchronizedSet(new HashSet<>());
    private final Map<String, Integer> modSoundOverrides = new LinkedHashMap<>();
    private final Set<Integer> modSampleIds = new HashSet<>();
    private final AtomicInteger variationSequence = new AtomicInteger();
    private final int blockBreak;
    private final int blockPlace;
    private final int uiClick;
    private final int itemPickup;
    private final int jump;
    private final int footstep;
    private final int blockWood;
    private final int blockStone;
    private final int blockDirt;
    private final int blockLeaves;
    private final int sheepCall;
    private final int cowCall;
    private final int birdCall;
    private final int pigCall;
    private final int zombieCall;
    private final int skeletonCall;
    private final int rabbitCall;
    private final int chestOpen;
    private final int commandSuccess;
    private final int waterSplash;
    private volatile float masterVolume;
    private volatile boolean released;

    private final SharedPreferences.OnSharedPreferenceChangeListener volumeListener =
            (sharedPreferences, key) -> {
                if ("sound_volume".equals(key)) {
                    masterVolume = clamp(sharedPreferences.getFloat("sound_volume", 0.82f));
                }
            };

    public SoundEffects(Context context) {
        preferences = context.getSharedPreferences("minefade_ui", Context.MODE_PRIVATE);
        masterVolume = clamp(preferences.getFloat("sound_volume", 0.82f));
        AudioAttributes attributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build();
        pool = new SoundPool.Builder().setMaxStreams(8).setAudioAttributes(attributes).build();
        pool.setOnLoadCompleteListener((soundPool, sampleId, status) -> {
            if (released) return;
            if (status == 0) loadedSamples.add(sampleId);
            else Log.w(TAG, "A sound sample failed to load: " + sampleId + " status=" + status);
        });
        blockBreak = load(context, "audio/block_break.wav");
        blockPlace = load(context, "audio/block_place.wav");
        uiClick = load(context, "audio/ui_click.wav");
        itemPickup = load(context, "audio/item_pickup.wav");
        jump = load(context, "audio/jump.wav");
        footstep = load(context, "audio/footstep.wav");
        blockWood = load(context, "audio/block_wood.wav");
        blockStone = load(context, "audio/block_stone.wav");
        blockDirt = load(context, "audio/block_dirt.wav");
        blockLeaves = load(context, "audio/block_leaves.wav");
        sheepCall = load(context, "audio/sheep.wav");
        cowCall = load(context, "audio/cow.wav");
        birdCall = load(context, "audio/bird.wav");
        pigCall = load(context, "audio/pig.wav");
        zombieCall = load(context, "audio/zombie.wav");
        skeletonCall = load(context, "audio/skeleton.wav");
        rabbitCall = load(context, "audio/rabbit.wav");
        chestOpen = load(context, "audio/chest_open.wav");
        commandSuccess = load(context, "audio/command_success.wav");
        waterSplash = load(context, "audio/water.wav");
        preferences.registerOnSharedPreferenceChangeListener(volumeListener);
    }

    private int load(Context context, String asset) {
        try (android.content.res.AssetFileDescriptor descriptor = context.getAssets().openFd(asset)) {
            return pool.load(descriptor, 1);
        } catch (Exception error) {
            Log.w(TAG, "Optional sound is unavailable: " + asset, error);
            return 0;
        }
    }

    private static float clamp(float value) {
        return Math.max(0.0f, Math.min(1.0f, value));
    }

    /** Loads enabled pack overrides in a stable order; first mod wins on conflicts. */
    public synchronized void loadModOverrides(String modsDirectory) {
        for (int sampleId : modSampleIds) {
            loadedSamples.remove(sampleId);
            pool.unload(sampleId);
        }
        modSampleIds.clear();
        modSoundOverrides.clear();
        if (released || modsDirectory == null) return;
        File root = new File(modsDirectory);
        File[] children = root.listFiles();
        if (children == null) return;
        ArrayList<File> packs = new ArrayList<>();
        for (File child : children) if (child.isDirectory()) packs.add(child);
        packs.sort(Comparator.comparing(File::getName, String.CASE_INSENSITIVE_ORDER));
        for (File pack : packs) {
            File manifestIni = new File(pack, "manifest.ini");
            File manifestJson = new File(pack, "manifest.json");
            if (!manifestIni.isFile() || !manifestJson.isFile()) continue;
            boolean enabled = false;
            String expectedHash = "";
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                    new FileInputStream(manifestIni), StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null)
                    String trimmed = line.trim();
                    if ("enabled=true".equals(trimmed)) enabled = true;
                    else if (trimmed.startsWith("hash=")) expectedHash = trimmed.substring(5);
            } catch (Exception ignored) { continue; }
            if (!enabled || !ModManager.verifyContentHash(pack, expectedHash)) continue;
            try {
                byte[] bytes = readBounded(manifestJson, 1024 * 1024);
                JSONObject manifest = new JSONObject(new String(bytes, StandardCharsets.UTF_8));
                if (manifest.optInt("format", 0) != 2) continue;
                JSONObject sounds = manifest.optJSONObject("sounds");
                if (sounds == null) continue;
                java.util.Iterator<String> keys = sounds.keys();
                while (keys.hasNext()) {
                    String key = keys.next();
                    if (modSoundOverrides.containsKey(key)) continue;
                    String relative = sounds.optString(key, "");
                    String lower = relative.toLowerCase(java.util.Locale.ROOT);
                    if (!isSoundKey(key) || !relative.startsWith("audio/") ||
                            relative.contains("..") || relative.indexOf('\\') >= 0 ||
                            !(lower.endsWith(".ogg") || lower.endsWith(".wav"))) continue;
                    File audio = new File(pack, relative);
                    String canonicalRoot = pack.getCanonicalPath() + File.separator;
                    if (!audio.getCanonicalPath().startsWith(canonicalRoot) ||
                            !audio.isFile() || audio.length() < 12L ||
                            audio.length() > 4L * 1024L * 1024L) continue;
                    int sampleId = pool.load(audio.getAbsolutePath(), 1);
                    if (sampleId != 0) {
                        modSoundOverrides.put(key, sampleId);
                        modSampleIds.add(sampleId);
                    }
                }
            } catch (Exception error) {
                Log.w(TAG, "Ignored an invalid mod audio manifest: " + pack.getName(), error);
            }
        }
    }

    private static byte[] readBounded(File file, int maximum) throws Exception {
        try (FileInputStream input = new FileInputStream(file);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int count, total = 0;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > maximum) throw new java.io.IOException("Manifest too large");
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }

    private static boolean isSoundKey(String key) {
        switch (key) {
            case "block_break": case "block_place": case "ui_click": case "item_pickup":
            case "jump": case "footstep": case "block_wood": case "block_stone":
            case "block_dirt": case "block_leaves": case "sheep": case "cow":
            case "bird": case "pig": case "zombie": case "skeleton": case "rabbit":
            case "chest_open": case "command_success": case "water": return true;
            default: return false;
        }
    }

    private void playCue(String key, int defaultSample, float gain, float rate) {
        Integer replacementSample = modSoundOverrides.get(key);
        if (replacementSample != null && loadedSamples.contains(replacementSample))
            play(replacementSample, gain, rate);
        else
            play(defaultSample, gain, rate);
    }

    private void play(int sampleId, float gain, float rate) {
        if (released || sampleId == 0 || !loadedSamples.contains(sampleId)) return;
        final float volume = clamp(masterVolume * gain);
        if (volume <= 0.001f) return;
        // Tiny deterministic pitch/gain changes make repeated block and footstep
        // samples feel less mechanical without introducing expensive audio work.
        final int variation = Math.floorMod(variationSequence.getAndIncrement() * 37 + sampleId * 11, 9) - 4;
        final float variedRate = Math.max(0.88f, Math.min(1.14f, rate + variation * 0.0125f));
        final float variedVolume = clamp(volume * (0.97f + Math.floorMod(variation + 4, 5) * 0.015f));
        pool.play(sampleId, variedVolume, variedVolume, 1, 0, variedRate);
    }

    /** Menu feedback works before the GL world is launched. */
    public void playMenuClick() {
        if (!preferences.getBoolean("ui_sounds_enabled", true)) return;
        play(uiClick, 0.52f, 1.0f);
    }

    /**
     * Native event bits: base break/place/UI/pickup/jump/step = 1..32;
     * material cues stone/wood/soil/foliage = 64..512;
     * creature calls sheep/cow/bird/pig/zombie/skeleton/rabbit = 1024..65536;
     * chest/command/water = 131072/262144/524288.
     */
    public void playEvents(int events) {
        if (!preferences.getBoolean("sound_effects_enabled", true)) return;
        if ((events & 1) != 0) playCue("block_break", blockBreak, 0.82f, 1.0f);
        if ((events & 2) != 0) playCue("block_place", blockPlace, 0.72f, 1.0f);
        if ((events & 4) != 0) playCue("ui_click", uiClick, 0.48f, 1.0f);
        if ((events & 8) != 0) {
            playCue("item_pickup", itemPickup, 0.88f, 1.0f);
            playCue("ui_click", uiClick, 0.13f, 1.28f);
        }
        if ((events & 16) != 0) {
            playCue("jump", jump, 0.74f, 1.0f);
            playCue("footstep", footstep, 0.10f, 0.82f);
        }
        if ((events & 32) != 0) playCue("footstep", footstep, 0.32f, 1.0f);
        if ((events & 64) != 0) {
            playCue("block_break", blockBreak, 0.27f, 0.76f);
            playCue("block_place", blockPlace, 0.10f, 1.18f);
        }
        if ((events & 128) != 0) {
            playCue("footstep", footstep, 0.22f, 0.72f);
            playCue("block_place", blockPlace, 0.18f, 0.90f);
        }
        if ((events & 256) != 0) {
            playCue("footstep", footstep, 0.20f, 0.86f);
            playCue("block_break", blockBreak, 0.12f, 1.14f);
        }
        if ((events & 512) != 0) {
            playCue("block_leaves", blockLeaves, 0.60f, 1.08f);
            playCue("footstep", footstep, 0.10f, 1.18f);
        }
        if ((events & 64) != 0) playCue("block_stone", blockStone, 0.62f, 0.96f);
        if ((events & 128) != 0) playCue("block_wood", blockWood, 0.62f, 0.92f);
        if ((events & 256) != 0) playCue("block_dirt", blockDirt, 0.66f, 0.94f);
        if ((events & 1024) != 0) playCue("sheep", sheepCall, 0.76f, 1.0f);
        if ((events & 2048) != 0) playCue("cow", cowCall, 0.76f, 0.93f);
        if ((events & 4096) != 0) playCue("bird", birdCall, 0.62f, 1.08f);
        if ((events & 8192) != 0) playCue("pig", pigCall, 0.72f, 1.0f);
        if ((events & 16384) != 0) playCue("zombie", zombieCall, 0.78f, 0.88f);
        if ((events & 32768) != 0) playCue("skeleton", skeletonCall, 0.72f, 0.96f);
        if ((events & 65536) != 0) playCue("rabbit", rabbitCall, 0.56f, 1.12f);
        if ((events & 131072) != 0) playCue("chest_open", chestOpen, 0.72f, 0.96f);
        if ((events & 262144) != 0) playCue("command_success", commandSuccess, 0.54f, 1.0f);
        if ((events & 524288) != 0) playCue("water", waterSplash, 0.48f, 0.92f);
    }

    public int loadedSampleCount() {
        return loadedSamples.size();
    }

    public void release() {
        if (released) return;
        released = true;
        preferences.unregisterOnSharedPreferenceChangeListener(volumeListener);
        loadedSamples.clear();
        pool.release();
    }
}
