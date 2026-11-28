package com.minefade.nativeengine;

/** Java-to-C++ boundary. All GL-facing calls are dispatched on the GLSurfaceView thread. */
public final class NativeBridge {
    static {
        System.loadLibrary("minefade_native");
    }

    private NativeBridge() { }

    public static native boolean nativeInit(float displayDensity, byte[] blockAtlasRgba, int atlasWidth, int atlasHeight, byte[] sheepModel, byte[] sheepTextureRgba);
    public static native boolean nativeRestoreGlContext(float displayDensity, byte[] blockAtlasRgba, int atlasWidth, int atlasHeight, byte[] sheepTextureRgba);
    public static native String nativeGetLastInitError();
    public static native boolean nativeIsUiReady();
    public static native void nativeFallbackAction(int action, boolean pressed);
    public static native void nativeResize(int width, int height);
    public static native void nativeRender();
    public static native boolean nativeConsumeEngineFailure();
    public static native boolean nativeConsumeChatRequest();
    public static native void nativeTouch(int action, int pointerId, float x, float y);
    public static native boolean nativePause();
    public static native void nativeResume();
    public static native void nativeSetStoragePath(String path);
    public static native void nativeSetPlayerIdentity(String identity);
    public static native boolean nativeCanModifyBlockForIdentity(String identity, int x, int z);
    public static native void nativeSetWorldOptions(int gameMode, boolean allowCheats, boolean starterChest, long seed, boolean flatWorld, int difficulty, boolean generateStructures);
    public static native void nativeSetModsDirectory(String path);
    public static native String nativeExecuteCommand(String command);
    public static native String nativeGetQuickbarSnapshot();
    public static native String nativeGetProgressionSnapshot();
    public static native String nativeGetInventorySnapshot();
    public static native String nativeGetHudSnapshot();
    public static native void nativeSelectInventorySlot(int slot);
    public static native boolean nativeConsumeExitRequest();
    public static native int nativeConsumeSoundEvents();
    public static native void nativeSetMultiplayerMode(boolean enabled, int playerSlot);
    public static native void nativeSetGraphicsOptions(int quality, int renderDistanceChunks, boolean waterWaves, float fieldOfView, boolean contactShadows);
    public static native void nativeSetShaderOptions(boolean lavaAnimations, boolean leafWind, float leafWindStrength);
    public static native void nativeSetInputOptions(float sensitivityX, float sensitivityY, float joystickDeadzone, boolean invertLookY, boolean leftHanded, int controlMode, boolean autoJump);
    public static native void nativeSetHudOptions(boolean showStats);
    public static native void nativeSetMinimapShape(int shape);
    public static native void nativeSetAnimationOptions(int quality, boolean enabled);
    public static native void nativeSetAvatarAppearance(int[] choices);
    public static native float[] nativeGetPlayerState();
    public static native void nativeSetRemotePlayers(float[] values);
    public static native int[] nativeConsumeBlockEdits();
    public static native void nativeApplyRemoteBlockEdits(int[] values);
}
