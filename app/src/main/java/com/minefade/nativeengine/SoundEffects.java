package com.minefade.nativeengine;

import android.content.Context;
import android.content.SharedPreferences;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.util.Log;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;

/** Low-latency local game audio with load-state tracking and live master volume. */
public final class SoundEffects {
    private static final String TAG = "MinefadeAudio";
    private final SoundPool pool;
    private final SharedPreferences preferences;
    private final Set<Integer> loadedSamples = Collections.synchronizedSet(new HashSet<>());
    private final Map<String, ModSound> modSoundOverrides = new HashMap<>();
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

    private static final class ModSound {
        final int sampleId;
        final float volume;
        ModSound(int sampleId, float volume) {
            this.sampleId = sampleId;
            this.volume = volume;
        }
    }

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
        loadModSoundOverrides(context);
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

    /**
     * Load sound overrides from enabled MFMod packages. Only bounded WAV/OGG
     * files under the package assets directory are opened; package paths are
     * canonicalized to prevent traversal or symlink escape.
     */
    private void loadModSoundOverrides(Context context) {
        final File modsRoot = new File(context.getFilesDir(), "mods");
        File[] packages = modsRoot.listFiles(File::isDirectory);
        if (packages == null) return;
        java.util.Arrays.sort(packages, (a, b) -> a.getName().compareTo(b.getName()));
        for (File packageRoot : packages) {
            if (modSoundOverrides.size() >= 32 || !isPackageEnabled(packageRoot)) continue;
            final File manifest = new File(new File(packageRoot, "data"), "sounds.json");
            if (!manifest.isFile() || manifest.length() <= 0L || manifest.length() > 65536L) continue;
            try {
                final byte[] data;
                try (InputStream input = new FileInputStream(manifest);
                     ByteArrayOutputStream output = new ByteArrayOutputStream((int) manifest.length())) {
                    byte[] buffer = new byte[2048];
                    int count;
                    while ((count = input.read(buffer)) != -1) {
                        if (output.size() + count > 65536) throw new IllegalArgumentException("sounds.json too large");
                        output.write(buffer, 0, count);
                    }
                    data = output.toByteArray();
                }
                JSONObject sounds = new JSONObject(new String(data, StandardCharsets.UTF_8));
                final String canonicalRoot = packageRoot.getCanonicalPath() + File.separator;
                Iterator<String> keys = sounds.keys();
                while (keys.hasNext() && modSoundOverrides.size() < 32) {
                    String event = keys.next();
                    if (!isSupportedSoundEvent(event)) continue;
                    Object value = sounds.get(event);
                    String relative;
                    float volume = 1.0f;
                    if (value instanceof String) {
                        relative = (String) value;
                    } else if (value instanceof JSONObject) {
                        JSONObject descriptor = (JSONObject) value;
                        relative = descriptor.getString("file");
                        double configuredVolume = descriptor.optDouble("volume", 1.0);
                        if (!Double.isFinite(configuredVolume) || configuredVolume < 0.0 ||
                                configuredVolume > 2.0) continue;
                        volume = (float) configuredVolume;
                    } else continue;
                    if (relative == null || !relative.startsWith("assets/") ||
                            relative.contains("..") || relative.indexOf('\\') >= 0 ||
                            !(relative.endsWith(".wav") || relative.endsWith(".ogg"))) continue;
                    File audioFile = new File(packageRoot, relative);
                    String canonicalFile = audioFile.getCanonicalPath();
                    if (!canonicalFile.startsWith(canonicalRoot) || !audioFile.isFile() ||
                            audioFile.length() <= 0L || audioFile.length() > 4L * 1024L * 1024L) continue;
                    int sample = pool.load(canonicalFile, 1);
                    if (sample != 0) modSoundOverrides.put(event, new ModSound(sample, volume));
                }
            } catch (Exception error) {
                Log.w(TAG, "Ignoring invalid mod sound package: " + packageRoot.getName(), error);
            }
        }
    }

    private static boolean isPackageEnabled(File packageRoot) {
        File gameplay = new File(new File(packageRoot, "data"), "gameplay.mfmod");
        if (!gameplay.isFile() || gameplay.length() > 32768L) return false;
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new FileInputStream(gameplay), StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                int split = line.indexOf('=');
                if (split <= 0) continue;
                if ("enabled".equals(line.substring(0, split).trim()))
                    return "true".equalsIgnoreCase(line.substring(split + 1).trim());
            }
        } catch (Exception ignored) { }
        return false;
    }

    private static boolean isSupportedSoundEvent(String event) {
        switch (event) {
            case "block_break": case "block_place": case "ui_click":
            case "item_pickup": case "jump": case "footstep":
            case "block_stone": case "block_wood": case "block_dirt": case "block_leaves":
            case "sheep_call": case "cow_call": case "bird_call": case "pig_call":
            case "zombie_call": case "skeleton_call": case "rabbit_call":
            case "chest_open": case "command_success": case "water_splash":
                return true;
            default:
                return false;
        }
    }

    private void playEvent(String event, int fallbackSample, float gain, float rate) {
        ModSound override = modSoundOverrides.get(event);
        if (override != null) play(override.sampleId, gain * override.volume, rate);
        else play(fallbackSample, gain, rate);
    }

    private static float clamp(float value) {
        return Math.max(0.0f, Math.min(1.0f, value));
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
        if ((events & 1) != 0) playEvent("block_break", blockBreak, 0.82f, 1.0f);
        if ((events & 2) != 0) playEvent("block_place", blockPlace, 0.72f, 1.0f);
        if ((events & 4) != 0) playEvent("ui_click", uiClick, 0.48f, 1.0f);
        if ((events & 8) != 0) {
            playEvent("item_pickup", itemPickup, 0.88f, 1.0f);
            playEvent("ui_click", uiClick, 0.13f, 1.28f);
        }
        if ((events & 16) != 0) {
            playEvent("jump", jump, 0.74f, 1.0f);
            playEvent("footstep", footstep, 0.10f, 0.82f);
        }
        if ((events & 32) != 0) playEvent("footstep", footstep, 0.32f, 1.0f);
        if ((events & 64) != 0) {
            play(blockBreak, 0.27f, 0.76f);
            play(blockPlace, 0.10f, 1.18f);
            playEvent("block_stone", blockStone, 0.62f, 0.96f);
        }
        if ((events & 128) != 0) playEvent("block_wood", blockWood, 0.62f, 0.92f);
        if ((events & 256) != 0) playEvent("block_dirt", blockDirt, 0.66f, 0.94f);
        if ((events & 512) != 0) playEvent("block_leaves", blockLeaves, 0.60f, 1.08f);
        if ((events & 1024) != 0) playEvent("sheep_call", sheepCall, 0.76f, 1.0f);
        if ((events & 2048) != 0) playEvent("cow_call", cowCall, 0.76f, 0.93f);
        if ((events & 4096) != 0) playEvent("bird_call", birdCall, 0.62f, 1.08f);
        if ((events & 8192) != 0) playEvent("pig_call", pigCall, 0.72f, 1.0f);
        if ((events & 16384) != 0) playEvent("zombie_call", zombieCall, 0.78f, 0.88f);
        if ((events & 32768) != 0) playEvent("skeleton_call", skeletonCall, 0.72f, 0.96f);
        if ((events & 65536) != 0) playEvent("rabbit_call", rabbitCall, 0.56f, 1.12f);
        if ((events & 131072) != 0) playEvent("chest_open", chestOpen, 0.72f, 0.96f);
        if ((events & 262144) != 0) playEvent("command_success", commandSuccess, 0.54f, 1.0f);
        if ((events & 524288) != 0) playEvent("water_splash", waterSplash, 0.48f, 0.92f);
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
