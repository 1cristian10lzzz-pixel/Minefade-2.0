package com.minefade.nativeengine;

import android.content.Context;
import android.net.Uri;
import android.system.Os;
import org.json.JSONObject;
import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.security.MessageDigest;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/** Data-only .mfmod packs. No native code or executable scripts are loaded. */
public final class ModManager {
    public static final class ModInfo {
        public final File file;
        public final String id;
        public final String name;
        public final String version;
        public final String description;
        public final boolean enabled;
        public final Map<String, String> settings;

        ModInfo(File file, Map<String,String> values) {
            this.file = file;
            this.id = values.getOrDefault("id", file.getName());
            this.name = values.getOrDefault("name", id);
            this.version = values.getOrDefault("version", "1");
            this.description = values.getOrDefault("description", "Paquete de datos");
            this.enabled = Boolean.parseBoolean(values.getOrDefault("enabled", "false"));
            this.settings = values;
        }
    }

    private final Context context;
    private final File modsDirectory;

    public ModManager(Context context) {
        this.context = context.getApplicationContext();
        modsDirectory = new File(this.context.getFilesDir(), "mods");
        if (!modsDirectory.exists()) modsDirectory.mkdirs();
        // Remove only the two legacy auto-installed/demo packs. The library
        // must contain packages explicitly imported by the player.
        new File(modsDirectory, "minefade_ultimate_core.mfmod").delete();
        new File(modsDirectory, "minefade_ultimate_core.mfmod.part").delete();
        new File(modsDirectory, "example_fauna.mfmod").delete();
        new File(modsDirectory, "example_fauna.mfmod.tmp").delete();
    }

    public File getDirectory() { return modsDirectory; }

    public synchronized List<ModInfo> listMods() {
        ArrayList<ModInfo> result = new ArrayList<>();
        File[] files = modsDirectory.listFiles();
        if (files == null) return result;
        for (File file : files) {
            if (file.isFile() && file.getName().toLowerCase(Locale.ROOT).endsWith(".mfmod")) {
                ModInfo info = read(file);
                if (info != null) result.add(info);
            } else if (file.isDirectory()) {
                File manifest = new File(file, "manifest.json");
                File gameplay = new File(new File(file, "data"), "gameplay.mfmod");
                if (!manifest.isFile() || !gameplay.isFile()) continue;
                ModInfo info = read(gameplay);
                if (info != null && file.getName().equals(info.id)) result.add(info);
            }
        }
        result.sort(Comparator.comparing(m -> m.name.toLowerCase(Locale.ROOT)));
        return result;
    }

    public synchronized ModInfo importMod(Uri uri) {
        if (uri == null) return null;
        String raw = new File(uri.getLastPathSegment() == null ? "paquete.mfmod"
                : uri.getLastPathSegment()).getName();
        if (raw.toLowerCase(Locale.ROOT).endsWith(".mfmod3")) return importMod3(uri);
        if (raw.toLowerCase(Locale.ROOT).endsWith(".mfmod2")) return importMod2(uri);
        String safe = raw.replaceAll("[^a-zA-Z0-9._-]", "_");
        String lowerName = safe.toLowerCase(Locale.ROOT);
        if (!lowerName.endsWith(".mfmod")) safe += ".mfmod";
        else if (!safe.endsWith(".mfmod"))
            safe = safe.substring(0, safe.length() - ".mfmod".length()) + ".mfmod";

        String prefix = System.currentTimeMillis() + "-";
        File destination = new File(modsDirectory, prefix + safe);
        int suffix = 1;
        while (destination.exists()) destination = new File(modsDirectory,
                prefix + suffix++ + "-" + safe);
        File temporary = new File(modsDirectory, destination.getName() + ".part");

        try (InputStream input = context.getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(temporary, false)) {
            if (input == null) { temporary.delete(); return null; }
            byte[] buffer = new byte[4096];
            int total = 0, count;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > 32768) throw new IllegalArgumentException("El paquete supera 32 KB.");
                output.write(buffer, 0, count);
            }
            output.flush();
            output.getFD().sync();
        } catch (Exception error) {
            temporary.delete();
            return null;
        }

        ModInfo validated = read(temporary);
        if (validated == null) {
            temporary.delete();
            return null;
        }
        for (ModInfo existing : listMods()) {
            if (existing.id.equalsIgnoreCase(validated.id)) {
                temporary.delete();
                return existing;
            }
        }
        try {
            Os.rename(temporary.getAbsolutePath(), destination.getAbsolutePath());
            return new ModInfo(destination, validated.settings);
        } catch (Exception error) {
            temporary.delete();
            return null;
        }
    }

    public synchronized boolean setEnabled(ModInfo info, boolean enabled) {
        if (info == null || info.file == null || !info.file.isFile()) return false;
        byte[] previousGameplay;
        try (InputStream input = new FileInputStream(info.file);
             ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[2048];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (bytes.size() + count > 32768) return false;
                bytes.write(buffer, 0, count);
            }
            previousGameplay = bytes.toByteArray();
        } catch (Exception error) {
            return false;
        }

        File packageManifest = packageManifestFor(info.file);
        byte[] previousManifest = null;
        JSONObject manifest = null;
        if (packageManifest != null) {
            try (InputStream input = new FileInputStream(packageManifest);
                 ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[2048];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    if (bytes.size() + count > 8192) return false;
                    bytes.write(buffer, 0, count);
                }
                previousManifest = bytes.toByteArray();
                manifest = new JSONObject(new String(previousManifest, StandardCharsets.UTF_8));
            } catch (Exception error) {
                return false;
            }
        }

        try {
            Map<String,String> values = new LinkedHashMap<>(info.settings);
            values.put("enabled", Boolean.toString(enabled));
            StringBuilder data = new StringBuilder("# Minefade data mod v1");
            data.append((char) 10);
            for (Map.Entry<String,String> entry : values.entrySet()) {
                String key = entry.getKey();
                String value = entry.getValue();
                if (key.matches("[a-zA-Z0-9_.-]{1,64}") &&
                        value.indexOf((char) 10) < 0 && value.indexOf((char) 13) < 0) {
                    data.append(key).append('=').append(value).append((char) 10);
                }
            }
            byte[] updatedGameplay = data.toString().getBytes(StandardCharsets.UTF_8);
            if (!writeAtomically(info.file, updatedGameplay)) return false;
            if (manifest != null && packageManifest != null) {
                if (manifest.optInt("format", -1) == 3) {
                    JSONObject fileIndex = manifest.getJSONObject("files");
                    JSONObject gameplayRecord = fileIndex.getJSONObject("data/gameplay.mfmod");
                    gameplayRecord.put("size", updatedGameplay.length);
                    gameplayRecord.put("sha256", sha256(updatedGameplay));
                    manifest.put("files", fileIndex);
                } else {
                    manifest.put("sha256", sha256(updatedGameplay));
                }
                if (!writeAtomically(packageManifest,
                        manifest.toString().getBytes(StandardCharsets.UTF_8))) {
                    writeAtomically(info.file, previousGameplay);
                    return false;
                }
            }
            return true;
        } catch (Exception ignored) {
            if (previousManifest != null && packageManifest != null) {
                writeAtomically(packageManifest, previousManifest);
            }
            writeAtomically(info.file, previousGameplay);
            return false;
        }
    }

    /**
     * Installs a bounded, data-only MFMod v2 ZIP package.
     * Supported entries are deliberately limited to manifest.json and
     * data/gameplay.mfmod. Executable files, scripts and arbitrary resources
     * are rejected until their registries and sandboxed loaders exist.
     */
    private ModInfo importMod2(Uri uri) {
        File temporaryRoot = null;
        try {
            final Map<String, byte[]> entries = new LinkedHashMap<>();
            final Set<String> seen = new HashSet<>();
            int totalExpanded = 0;
            int entryCount = 0;
            try (InputStream raw = context.getContentResolver().openInputStream(uri);
                 ZipInputStream zip = raw == null ? null : new ZipInputStream(raw)) {
                if (zip == null) return null;
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (++entryCount > 3) throw new IOException("Demasiadas entradas.");
                    String name = entry.getName();
                    if ("data/".equals(name) && entry.isDirectory()) {
                        zip.closeEntry();
                        continue;
                    }
                    if (entry.isDirectory() || name == null || name.startsWith("/") ||
                            name.indexOf('\\') >= 0 || name.indexOf(':') >= 0 ||
                            name.contains("../") || name.equals("..") ||
                            !(name.equals("manifest.json") || name.equals("data/gameplay.mfmod")) ||
                            !seen.add(name)) {
                        throw new IOException("Ruta o entrada no permitida.");
                    }
                    final int maxEntry = name.equals("manifest.json") ? 8192 : 32768;
                    ByteArrayOutputStream output = new ByteArrayOutputStream(Math.min(maxEntry, 4096));
                    byte[] buffer = new byte[2048];
                    int count;
                    while ((count = zip.read(buffer)) != -1) {
                        if (output.size() + count > maxEntry || totalExpanded + count > 40960) {
                            throw new IOException("El paquete supera el límite de tamaño.");
                        }
                        output.write(buffer, 0, count);
                        totalExpanded += count;
                    }
                    entries.put(name, output.toByteArray());
                    zip.closeEntry();
                }
            }
            byte[] manifestBytes = entries.get("manifest.json");
            byte[] gameplayBytes = entries.get("data/gameplay.mfmod");
            if (manifestBytes == null || gameplayBytes == null || gameplayBytes.length == 0) return null;

            JSONObject manifest = new JSONObject(new String(manifestBytes, StandardCharsets.UTF_8));
            Set<String> allowedManifestKeys = new HashSet<>();
            allowedManifestKeys.add("format");
            allowedManifestKeys.add("id");
            allowedManifestKeys.add("name");
            allowedManifestKeys.add("version");
            allowedManifestKeys.add("engine_min");
            allowedManifestKeys.add("engine_max");
            allowedManifestKeys.add("entry");
            allowedManifestKeys.add("sha256");
            Iterator<String> keys = manifest.keys();
            while (keys.hasNext()) {
                if (!allowedManifestKeys.contains(keys.next())) return null;
            }
            if (manifest.length() != allowedManifestKeys.size() ||
                    manifest.optInt("format", -1) != 2 ||
                    !"data/gameplay.mfmod".equals(manifest.optString("entry", ""))) return null;

            String id = manifest.getString("id");
            String name = manifest.getString("name");
            String packageVersion = manifest.getString("version");
            String minimumEngine = manifest.getString("engine_min");
            String maximumEngine = manifest.getString("engine_max");
            String expectedHash = manifest.getString("sha256").toLowerCase(Locale.ROOT);
            if (!id.matches("[a-z0-9][a-z0-9_.-]{1,63}") ||
                    name.trim().isEmpty() || name.length() > 96 ||
                    !isSemanticVersion(packageVersion) ||
                    !isSemanticVersion(minimumEngine) || !isSemanticVersion(maximumEngine) ||
                    compareVersions(minimumEngine, currentEngineVersion()) > 0 ||
                    compareVersions(currentEngineVersion(), maximumEngine) > 0 ||
                    !expectedHash.matches("[0-9a-f]{64}") ||
                    !expectedHash.equals(sha256(gameplayBytes))) return null;

            File checkRoot = new File(modsDirectory, "." + id + "-" + System.nanoTime() + ".part");
            if (!checkRoot.mkdirs()) return null;
            temporaryRoot = checkRoot;
            File dataDirectory = new File(checkRoot, "data");
            if (!dataDirectory.mkdirs()) return null;
            File gameplayFile = new File(dataDirectory, "gameplay.mfmod");
            try (FileOutputStream output = new FileOutputStream(gameplayFile, false)) {
                output.write(gameplayBytes);
                output.flush();
                output.getFD().sync();
            }
            ModInfo validated = read(gameplayFile);
            if (validated == null || !id.equals(validated.id) ||
                    !name.equals(validated.name) || !"false".equals(validated.settings.get("enabled"))) {
                return null;
            }
            try (FileOutputStream output = new FileOutputStream(new File(checkRoot, "manifest.json"), false)) {
                output.write(manifestBytes);
                output.flush();
                output.getFD().sync();
            }

            for (ModInfo existing : listMods()) {
                if (existing.id.equalsIgnoreCase(id)) return existing;
            }
            File destination = new File(modsDirectory, id);
            if (destination.exists()) return null;
            Os.rename(checkRoot.getAbsolutePath(), destination.getAbsolutePath());
            temporaryRoot = null;
            return read(new File(new File(destination, "data"), "gameplay.mfmod"));
        } catch (Exception ignored) {
            return null;
        } finally {
            if (temporaryRoot != null) deleteTree(temporaryRoot);
        }
    }

    /**
     * Installs a bounded MFMod v3 content pack. The package contains only data
     * and allowlisted resources; no scripts, JAR/DEX or native libraries execute.
     */
    private ModInfo importMod3(Uri uri) {
        File temporaryRoot = null;
        try {
            Map<String, byte[]> entries = new LinkedHashMap<>();
            Set<String> seen = new HashSet<>();
            int expanded = 0;
            int entryCount = 0;
            try (InputStream raw = context.getContentResolver().openInputStream(uri);
                 InputStream bounded = raw == null ? null : new BoundedInputStream(raw, 20L * 1024L * 1024L);
                 ZipInputStream zip = bounded == null ? null : new ZipInputStream(bounded)) {
                if (zip == null) return null;
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    if (++entryCount > 257 || entry.isDirectory()) return null;
                    String name = entry.getName();
                    if (!isSafeMod3Entry(name) || !seen.add(name) || "manifest.json".equals(name))
                        return null;
                    int maximum = maxMod3EntryBytes(name);
                    ByteArrayOutputStream output = new ByteArrayOutputStream(Math.min(maximum, 4096));
                    byte[] buffer = new byte[4096];
                    int count;
                    while ((count = zip.read(buffer)) != -1) {
                        if (output.size() + count > maximum || expanded + count > 32 * 1024 * 1024)
                            return null;
                        output.write(buffer, 0, count);
                        expanded += count;
                    }
                    byte[] bytes = output.toByteArray();
                    if (name.endsWith(".rgba") && bytes.length != 1024) return null;
                    entries.put(name, bytes);
                    zip.closeEntry();
                }
            }

            byte[] manifestBytes = entries.remove("manifest.json");
            byte[] gameplayBytes = entries.get("data/gameplay.mfmod");
            if (manifestBytes == null || manifestBytes.length == 0 || manifestBytes.length > 65536 ||
                    gameplayBytes == null || gameplayBytes.length == 0) return null;
            JSONObject manifest = new JSONObject(new String(manifestBytes, StandardCharsets.UTF_8));
            Set<String> allowedKeys = new HashSet<>();
            Collections.addAll(allowedKeys, "format", "id", "name", "version", "engine_min",
                    "engine_max", "entry", "description", "capabilities", "dependencies", "files");
            if (manifest.length() != allowedKeys.size()) return null;
            Iterator<String> keyIterator = manifest.keys();
            while (keyIterator.hasNext()) if (!allowedKeys.contains(keyIterator.next())) return null;
            if (manifest.getInt("format") != 3 ||
                    !"data/gameplay.mfmod".equals(manifest.getString("entry"))) return null;

            String id = manifest.getString("id");
            String name = manifest.getString("name");
            String packageVersion = manifest.getString("version");
            String minimum = manifest.getString("engine_min");
            String maximum = manifest.getString("engine_max");
            String description = manifest.getString("description");
            if (!id.matches("[a-z0-9][a-z0-9_.-]{1,63}") ||
                    name.trim().isEmpty() || name.length() > 96 || description.length() > 512 ||
                    !isSemanticVersion(packageVersion) || !isSemanticVersion(minimum) ||
                    !isSemanticVersion(maximum) || compareVersions(minimum, maximum) > 0 ||
                    compareVersions(minimum, currentEngineVersion()) > 0 ||
                    compareVersions(currentEngineVersion(), maximum) > 0) return null;

            JSONObject files = manifest.getJSONObject("files");
            if (files.length() != entries.size() || files.length() > 256) return null;
            Iterator<String> fileIterator = files.keys();
            while (fileIterator.hasNext()) {
                String relative = fileIterator.next();
                byte[] bytes = entries.get(relative);
                if (bytes == null || !isSafeMod3Entry(relative)) return null;
                JSONObject record = files.getJSONObject(relative);
                if (record.length() != 2 || record.getInt("size") != bytes.length ||
                        !sha256(bytes).equals(record.getString("sha256"))) return null;
            }

            JSONObject dependencies = manifest.getJSONObject("dependencies");
            if (dependencies.length() > 64) return null;
            Iterator<String> dependencyIterator = dependencies.keys();
            while (dependencyIterator.hasNext()) {
                String dependency = dependencyIterator.next();
                if (!dependency.matches("[a-z0-9][a-z0-9_.-]{1,63}") ||
                        dependencies.getString(dependency).length() > 64) return null;
            }
            JSONArray capabilities = manifest.getJSONArray("capabilities");
            Set<String> allowedCapabilities = new HashSet<>();
            Collections.addAll(allowedCapabilities, "blocks", "items", "recipes", "entities",
                    "biomes", "structures", "textures", "models", "animations", "sounds", "ui");
            Set<String> declaredCapabilities = new HashSet<>();
            for (int i = 0; i < capabilities.length(); ++i) {
                String capability = capabilities.getString(i);
                if (!allowedCapabilities.contains(capability) || !declaredCapabilities.add(capability))
                    return null;
            }
            if (!validateMod3Definitions(entries, id, declaredCapabilities)) return null;

            File checkRoot = new File(modsDirectory, "." + id + "-" + System.nanoTime() + ".part");
            if (!checkRoot.mkdirs()) return null;
            temporaryRoot = checkRoot;
            writeMod3File(checkRoot, "manifest.json", manifestBytes);
            for (Map.Entry<String, byte[]> entry : entries.entrySet())
                writeMod3File(checkRoot, entry.getKey(), entry.getValue());

            File gameplayFile = new File(new File(checkRoot, "data"), "gameplay.mfmod");
            ModInfo validated = read(gameplayFile);
            if (validated == null || !id.equals(validated.id) || !name.equals(validated.name) ||
                    !"false".equals(validated.settings.get("enabled"))) return null;
            for (ModInfo existing : listMods()) {
                if (existing.id.equalsIgnoreCase(id)) return existing;
            }
            File destination = new File(modsDirectory, id);
            if (destination.exists()) return null;
            Os.rename(checkRoot.getAbsolutePath(), destination.getAbsolutePath());
            temporaryRoot = null;
            return read(new File(new File(destination, "data"), "gameplay.mfmod"));
        } catch (Exception ignored) {
            return null;
        } finally {
            if (temporaryRoot != null) deleteTree(temporaryRoot);
        }
    }

    private static final class BoundedInputStream extends FilterInputStream {
        private final long maximum;
        private long consumed;
        BoundedInputStream(InputStream input, long maximum) {
            super(input);
            this.maximum = maximum;
        }
        @Override public int read() throws IOException {
            int value = super.read();
            if (value >= 0 && ++consumed > maximum)
                throw new IOException("El ZIP comprimido supera 20 MiB.");
            return value;
        }
        @Override public int read(byte[] buffer, int offset, int length) throws IOException {
            int allowed = (int)Math.min(length, maximum - consumed + 1L);
            int count = super.read(buffer, offset, allowed);
            if (count > 0) {
                consumed += count;
                if (consumed > maximum)
                    throw new IOException("El ZIP comprimido supera 20 MiB.");
            }
            return count;
        }
    }

    private static boolean isSafeMod3Entry(String name) {
        if (name == null || name.isEmpty() || name.startsWith("/") ||
                name.indexOf('\\\\') >= 0 || name.indexOf(':') >= 0) return false;
        String[] parts = name.split("/", -1);
        for (String part : parts)
            if (part.isEmpty() || part.equals(".") || part.equals("..")) return false;
        Set<String> dataFiles = new HashSet<>();
        Collections.addAll(dataFiles, "data/gameplay.mfmod", "data/blocks.json", "data/items.json",
                "data/recipes.json", "data/entities.json", "data/biomes.json",
                "data/structures.json", "data/ui.json", "data/sounds.json");
        if (dataFiles.contains(name)) return true;
        if (!name.startsWith("assets/") || parts.length < 3) return false;
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.endsWith(".rgba") || lower.endsWith(".png") || lower.endsWith(".webp") ||
                lower.endsWith(".mfmodel") || lower.endsWith(".json") || lower.endsWith(".wav") ||
                lower.endsWith(".ogg") || lower.endsWith(".rml") || lower.endsWith(".rcss");
    }

    private static int maxMod3EntryBytes(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        if (lower.endsWith(".mfmodel")) return 8 * 1024 * 1024;
        if (lower.endsWith(".wav") || lower.endsWith(".ogg")) return 4 * 1024 * 1024;
        if (lower.endsWith(".rgba") || lower.endsWith(".png") || lower.endsWith(".webp"))
            return 2 * 1024 * 1024;
        if (lower.endsWith(".json") || lower.endsWith(".rml") || lower.endsWith(".rcss") ||
                lower.endsWith(".mfmod")) return 1024 * 1024;
        return 0;
    }

    private static void writeMod3File(File root, String relative, byte[] bytes) throws IOException {
        File target = new File(root, relative.replace("/", File.separator));
        String rootPath = root.getCanonicalPath() + File.separator;
        if (!target.getCanonicalPath().startsWith(rootPath)) throw new IOException("Ruta fuera del paquete.");
        File parent = target.getParentFile();
        if (parent == null || (!parent.isDirectory() && !parent.mkdirs()))
            throw new IOException("No se pudo crear el directorio del recurso.");
        try (FileOutputStream output = new FileOutputStream(target, false)) {
            output.write(bytes);
            output.flush();
            output.getFD().sync();
        }
    }

    private static boolean validMod3Id(String value, String modId) {
        if (value == null || !value.startsWith(modId + ":")) return false;
        return value.substring(modId.length() + 1).matches("[a-z0-9][a-z0-9_.-]{1,63}");
    }

    private static boolean validateMod3Definitions(Map<String, byte[]> files, String modId,
                                                    Set<String> capabilities) {
        try {
            byte[] raw = files.get("data/blocks.json");
            if (raw != null) {
                if (!capabilities.contains("blocks")) return false;
                JSONArray blocks = new JSONArray(new String(raw, StandardCharsets.UTF_8));
                if (blocks.length() > 256) return false;
                Set<String> ids = new HashSet<>();
                for (int i = 0; i < blocks.length(); i++) {
                    JSONObject block = blocks.getJSONObject(i);
                    String id = block.getString("id");
                    if (!validMod3Id(id, modId) || !ids.add(id) ||
                            block.getString("name").trim().isEmpty() ||
                            block.getString("name").length() > 96 ||
                            !isFiniteInRange(block.optString("hardness", "1"), 0.0f, 100.0f) ||
                            !isIntegerInRange(block.optString("stack_limit", "64"), 1, 999)) return false;
                    String shape = block.optString("shape", "cube");
                    if (!("cube".equals(shape) || "slab".equals(shape) || "stairs".equals(shape) ||
                            "cross_plant".equals(shape) || "thin".equals(shape) || "pane".equals(shape) ||
                            "fence".equals(shape) || "door".equals(shape) || "trapdoor".equals(shape)))
                        return false;
                    JSONObject textures = block.getJSONObject("textures");
                    for (String face : new String[]{"top", "side", "bottom"}) {
                        String resource = textures.getString(face);
                        byte[] pixels = files.get(resource);
                        if (pixels == null || !resource.endsWith(".rgba") || pixels.length != 1024) return false;
                    }
                }
            }
            raw = files.get("data/items.json");
            if (raw != null) {
                if (!capabilities.contains("items")) return false;
                JSONArray items = new JSONArray(new String(raw, StandardCharsets.UTF_8));
                if (items.length() > 256) return false;
                Set<String> ids = new HashSet<>();
                for (int i = 0; i < items.length(); i++) {
                    JSONObject item = items.getJSONObject(i);
                    String id = item.getString("id");
                    String icon = item.getString("icon");
                    byte[] pixels = files.get(icon);
                    String type = item.optString("type", "item");
                    if (!validMod3Id(id, modId) || !ids.add(id) ||
                            item.getString("name").trim().isEmpty() || pixels == null ||
                            !icon.endsWith(".rgba") || pixels.length != 1024 ||
                            !isIntegerInRange(item.optString("stack_limit", "64"), 1, 999) ||
                            !("item".equals(type) || "weapon".equals(type) ||
                              "tool".equals(type) || "armor".equals(type))) return false;
                    if ("weapon".equals(type) || "tool".equals(type)) {
                        if (!isFiniteInRange(item.optString("damage", "1"), 0.0f, 100.0f) ||
                                !isIntegerInRange(item.optString("durability", "100"), 1, 100000)) return false;
                    }
                    if ("armor".equals(type)) {
                        String slot = item.optString("armor_slot", "");
                        if (!("helmet".equals(slot) || "chestplate".equals(slot) ||
                                "leggings".equals(slot) || "boots".equals(slot)) ||
                                !isIntegerInRange(item.optString("protection", "1"), 0, 20) ||
                                !isIntegerInRange(item.optString("durability", "100"), 1, 100000))
                            return false;
                    }
                }
            }
            raw = files.get("data/recipes.json");
            if (raw != null) {
                if (!capabilities.contains("recipes")) return false;
                JSONArray recipes = new JSONArray(new String(raw, StandardCharsets.UTF_8));
                if (recipes.length() > 512) return false;
                Set<String> ids = new HashSet<>();
                for (int i = 0; i < recipes.length(); i++) {
                    JSONObject recipe = recipes.getJSONObject(i);
                    String id = recipe.getString("id");
                    if (!validMod3Id(id, modId) || !ids.add(id)) return false;
                    JSONObject output = recipe.getJSONObject("output");
                    if (!validMod3Id(output.getString("id"), modId) ||
                            !isIntegerInRange(output.optString("count", "1"), 1, 999)) return false;
                    JSONArray ingredients = recipe.getJSONArray("ingredients");
                    if (ingredients.length() < 1 || ingredients.length() > 9) return false;
                    for (int j = 0; j < ingredients.length(); j++) {
                        JSONObject ingredient = ingredients.getJSONObject(j);
                        if (!validMod3Id(ingredient.getString("id"), modId) ||
                                !isIntegerInRange(ingredient.optString("count", "1"), 1, 999)) return false;
                    }
                }
            }
            raw = files.get("data/entities.json");
            if (raw != null) {
                if (!capabilities.contains("entities")) return false;
                JSONArray entities = new JSONArray(new String(raw, StandardCharsets.UTF_8));
                if (entities.length() > 128) return false;
                Set<String> ids = new HashSet<>();
                for (int i = 0; i < entities.length(); i++) {
                    JSONObject entity = entities.getJSONObject(i);
                    String id = entity.getString("id");
                    String model = entity.getString("model");
                    String texture = entity.getString("texture");
                    if (!validMod3Id(id, modId) || !ids.add(id) ||
                            !model.endsWith(".mfmodel") || !files.containsKey(model) ||
                            !files.containsKey(texture) ||
                            !(texture.endsWith(".rgba") || texture.endsWith(".png") || texture.endsWith(".webp")) ||
                            !isFiniteInRange(entity.optString("width", "0.8"), 0.1f, 16.0f) ||
                            !isFiniteInRange(entity.optString("height", "1.0"), 0.1f, 32.0f) ||
                            !isFiniteInRange(entity.optString("eye_height", "0.7"), 0.1f, 32.0f))
                        return false;
                }
            }
            return true;
        } catch (Exception ignored) {
            return false;
        }
    }

    private boolean isValidInstalledV3Package(File gameplayFile, Map<String,String> gameplayValues) {
        File manifestFile = packageManifestFor(gameplayFile);
        if (manifestFile == null || manifestFile.length() <= 0L || manifestFile.length() > 65536L)
            return false;
        try {
            byte[] manifestBytes = readBoundedFile(manifestFile, 65536);
            JSONObject manifest = new JSONObject(new String(manifestBytes, StandardCharsets.UTF_8));
            Set<String> required = new HashSet<>();
            Collections.addAll(required, "format", "id", "name", "version", "engine_min",
                    "engine_max", "entry", "description", "capabilities", "dependencies", "files");
            if (manifest.length() != required.size()) return false;
            Iterator<String> keys = manifest.keys();
            while (keys.hasNext()) if (!required.contains(keys.next())) return false;
            if (manifest.getInt("format") != 3 ||
                    !"data/gameplay.mfmod".equals(manifest.getString("entry")) ||
                    !gameplayValues.get("id").equals(manifest.getString("id")) ||
                    !gameplayValues.get("name").equals(manifest.getString("name"))) return false;
            String version = manifest.getString("version");
            String minimum = manifest.getString("engine_min");
            String maximum = manifest.getString("engine_max");
            if (!isSemanticVersion(version) || !isSemanticVersion(minimum) ||
                    !isSemanticVersion(maximum) || compareVersions(minimum, maximum) > 0 ||
                    compareVersions(minimum, currentEngineVersion()) > 0 ||
                    compareVersions(currentEngineVersion(), maximum) > 0) return false;
            JSONObject files = manifest.getJSONObject("files");
            if (files.length() <= 0 || files.length() > 256) return false;
            File root = gameplayFile.getParentFile().getParentFile();
            String rootCanonical = root.getCanonicalPath() + File.separator;
            Iterator<String> fileKeys = files.keys();
            int seen = 0;
            while (fileKeys.hasNext()) {
                String relative = fileKeys.next();
                if (!isSafeMod3Entry(relative)) return false;
                File target = new File(root, relative.replace("/", File.separator));
                if (!target.getCanonicalPath().startsWith(rootCanonical) ||
                        !target.isFile()) return false;
                JSONObject record = files.getJSONObject(relative);
                if (record.length() != 2 || record.getLong("size") != target.length() ||
                        target.length() > maxMod3EntryBytes(relative) ||
                        !record.getString("sha256").equals(sha256(readBoundedFile(target, maxMod3EntryBytes(relative)))))
                    return false;
                seen++;
            }
            return seen == files.length();
        } catch (Exception ignored) {
            return false;
        }
    }

    private static byte[] readBoundedFile(File file, int maximum) throws IOException {
        if (file.length() < 0 || file.length() > maximum) throw new IOException("Archivo demasiado grande.");
        try (InputStream input = new FileInputStream(file);
             ByteArrayOutputStream output = new ByteArrayOutputStream((int)file.length())) {
            byte[] buffer = new byte[4096];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (output.size() + count > maximum) throw new IOException("Archivo demasiado grande.");
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }

    private static void deleteTree(File file) {
        if (file == null || !file.exists()) return;
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        file.delete();
    }

    private String currentEngineVersion() {
        try {
            String version = context.getPackageManager()
                    .getPackageInfo(context.getPackageName(), 0).versionName;
            return version == null || version.trim().isEmpty() ? "0.0.0" : version;
        } catch (android.content.pm.PackageManager.NameNotFoundException error) {
            return "0.0.0";
        }
    }

    private static boolean isSemanticVersion(String value) {
        return value != null && value.matches("[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}");
    }

    private static int compareVersions(String left, String right) {
        try {
            String[] a = left.split("\\.");
            String[] b = right.split("\\.");
            for (int i = 0; i < 3; i++) {
                int difference = Integer.compare(Integer.parseInt(a[i]), Integer.parseInt(b[i]));
                if (difference != 0) return difference;
            }
        } catch (Exception ignored) { return Integer.MAX_VALUE; }
        return 0;
    }

    private static String sha256(byte[] data) throws Exception {
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(data);
        StringBuilder hex = new StringBuilder(digest.length * 2);
        for (byte value : digest) hex.append(String.format(Locale.ROOT, "%02x", value & 0xff));
        return hex.toString();
    }

    /** Stable digest of the enabled settings mods, shared by world and LAN checks. */
    public synchronized String enabledModsFingerprint() {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            ArrayList<ModInfo> enabled = new ArrayList<>();
            for (ModInfo info : listMods()) if (info.enabled) enabled.add(info);
            enabled.sort(Comparator.comparing(info -> info.id));
            for (ModInfo info : enabled) {
                digest.update(info.id.getBytes(StandardCharsets.UTF_8));
                digest.update((byte) 0);
                digest.update(info.version.getBytes(StandardCharsets.UTF_8));
                digest.update((byte) 0);
                try (InputStream input = new FileInputStream(info.file)) {
                    byte[] buffer = new byte[4096];
                    int read;
                    while ((read = input.read(buffer)) != -1) digest.update(buffer, 0, read);
                }
                File packageManifest = packageManifestFor(info.file);
                if (packageManifest != null) {
                    try (InputStream input = new FileInputStream(packageManifest)) {
                        byte[] buffer = new byte[4096];
                        int read;
                        while ((read = input.read(buffer)) != -1) digest.update(buffer, 0, read);
                    }
                }
                digest.update((byte) 0xff);
            }
            byte[] result = digest.digest();
            StringBuilder hex = new StringBuilder(result.length * 2);
            for (byte value : result) hex.append(String.format(Locale.ROOT, "%02x", value & 0xff));
            return hex.toString();
        } catch (Exception error) {
            return "";
        }
    }

    /**
     * Pins the enabled settings-mod set for an existing world. A mismatch is
     * reported but never silently overwrites the saved fingerprint.
     */
    public synchronized String pinOrCompareWorldMods(String worldPath) {
        if (worldPath == null || worldPath.trim().isEmpty()) return "";
        String fingerprint = enabledModsFingerprint();
        if (fingerprint.isEmpty()) return "No se pudo calcular la huella de los mods activos.";
        File sidecar = new File(worldPath + ".mods");
        try {
            if (sidecar.isFile()) {
                byte[] bytes = new byte[(int) Math.min(sidecar.length(), 256L)];
                try (InputStream input = new FileInputStream(sidecar)) {
                    int count = input.read(bytes);
                    if (count <= 0) return "El manifiesto de mods del mundo está dañado.";
                    String pinned = new String(bytes, 0, count, StandardCharsets.UTF_8).trim();
                    if (!pinned.equals(fingerprint)) {
                        return "Este mundo fue guardado con otro conjunto de mods. Restaura esos mods antes de seguir jugando.";
                    }
                    return "";
                }
            }
            return writeAtomically(sidecar, (fingerprint + "\\n").getBytes(StandardCharsets.UTF_8))
                    ? "" : "No se pudo guardar el manifiesto de mods del mundo.";
        } catch (Exception error) {
            return "No se pudo comprobar el manifiesto de mods del mundo.";
        }
    }



    private boolean isValidInstalledV2Package(File gameplayFile,
                                                       Map<String,String> gameplayValues) {
        File manifestFile = packageManifestFor(gameplayFile);
        if (manifestFile == null) return true;
        if (manifestFile.length() <= 0L || manifestFile.length() > 8192L) return false;
        try {
            byte[] manifestBytes;
            try (InputStream input = new FileInputStream(manifestFile);
                 ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[2048];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    if (bytes.size() + count > 8192) return false;
                    bytes.write(buffer, 0, count);
                }
                manifestBytes = bytes.toByteArray();
            }
            JSONObject manifest = new JSONObject(new String(manifestBytes, StandardCharsets.UTF_8));
            Set<String> required = new HashSet<>();
            Collections.addAll(required, "format", "id", "name", "version",
                    "engine_min", "engine_max", "entry", "sha256");
            if (manifest.length() != required.size()) return false;
            Iterator<String> keys = manifest.keys();
            while (keys.hasNext()) if (!required.contains(keys.next())) return false;
            if (manifest.getInt("format") != 2 ||
                    !"data/gameplay.mfmod".equals(manifest.getString("entry")) ||
                    !gameplayValues.get("id").equals(manifest.getString("id")) ||
                    !gameplayValues.get("name").equals(manifest.getString("name"))) return false;
            String version = manifest.getString("version");
            String minimum = manifest.getString("engine_min");
            String maximum = manifest.getString("engine_max");
            if (!isSemanticVersion(version) || !isSemanticVersion(minimum) ||
                    !isSemanticVersion(maximum) || compareVersions(minimum, maximum) > 0 ||
                    compareVersions(minimum, currentEngineVersion()) > 0 ||
                    compareVersions(currentEngineVersion(), maximum) > 0) return false;

            byte[] payload;
            try (InputStream input = new FileInputStream(gameplayFile);
                 ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[2048];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    if (bytes.size() + count > 32768) return false;
                    bytes.write(buffer, 0, count);
                }
                payload = bytes.toByteArray();
            }
            String expected = manifest.getString("sha256").toLowerCase(Locale.ROOT);
            return expected.matches("[0-9a-f]{64}") && expected.equals(sha256(payload));
        } catch (Exception ignored) {
            return false;
        }
    }

    private static File packageManifestFor(File gameplayFile) {
        if (gameplayFile == null || gameplayFile.getParentFile() == null ||
                !"data".equals(gameplayFile.getParentFile().getName()) ||
                gameplayFile.getParentFile().getParentFile() == null) return null;
        File manifest = new File(gameplayFile.getParentFile().getParentFile(), "manifest.json");
        return manifest.isFile() ? manifest : null;
    }

    private static boolean writeAtomically(File target, byte[] bytes) {
        if (target == null || target.getParentFile() == null || bytes == null) return false;
        File temporary = new File(target.getParentFile(), target.getName() + ".tmp");
        try (FileOutputStream output = new FileOutputStream(temporary, false)) {
            output.write(bytes);
            output.flush();
            output.getFD().sync();
        } catch (Exception error) {
            temporary.delete();
            return false;
        }
        try {
            Os.rename(temporary.getAbsolutePath(), target.getAbsolutePath());
            return target.isFile() && target.length() == bytes.length;
        } catch (Exception error) {
            temporary.delete();
            return false;
        }
    }

    private ModInfo read(File file) {
        if (file == null || !file.isFile() || file.length() <= 0L || file.length() > 32768L) return null;
        Map<String,String> values = new LinkedHashMap<>();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new java.io.FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            int lines = 0;
            while ((line = reader.readLine()) != null) {
                if (++lines > 128) return null;
                String value = line.trim();
                if (value.isEmpty() || value.startsWith("#")) continue;
                int split = value.indexOf('=');
                if (split <= 0) return null;
                String key = value.substring(0, split).trim();
                String content = value.substring(split + 1).trim();
                if (!key.matches("[a-zA-Z0-9_.-]{1,64}") || content.length() > 512) return null;
                if (!isSupportedKey(key) || values.containsKey(key)) return null;
                values.put(key, content);
            }
            if (!isValidPack(values) ||
                    (!isValidInstalledV2Package(file, values) &&
                     !isValidInstalledV3Package(file, values))) return null;
            return new ModInfo(file, values);
        } catch (Exception ignored) {
            return null;
        }
    }

    /** Keep the Java importer and the native Engine::LoadEnabledMods contract in sync. */
    private static boolean isSupportedKey(String key) {
        switch (key) {
            case "id":
            case "name":
            case "version":
            case "description":
            case "enabled":
            case "spawn.sheep":
            case "spawn.cow":
            case "spawn.bird":
            case "spawn.pig":
            case "spawn.rabbit":
            case "spawn.zombie":
            case "spawn.skeleton":
            case "player.speed":
            case "player.jump":
            case "player.crouch_speed":
            case "entity.speed":
            case "entity.passive_speed":
            case "entity.hostile_speed":
            case "entity.flee_radius":
            case "entity.alert_radius":
            case "entity.spawn_radius":
            case "world.day_speed":
            case "world.entity_cap":
            case "world.spawn_interval":
            case "world.spawn_distance":
            case "world.passive_cap":
            case "drops.pickup_radius":
            case "drops.lifetime":
            case "drops.quantity":
            case "mining.speed":
                return true;
            default:
                return false;
        }
    }

    private static boolean isValidPack(Map<String,String> values) {
        String id = values.get("id");
        String name = values.get("name");
        String format = values.get("version");
        String enabled = values.get("enabled");
        String description = values.getOrDefault("description", "");
        if (id == null || !id.matches("[a-z0-9][a-z0-9_.-]{1,63}")) return false;
        if (name == null || name.trim().isEmpty() || name.length() > 96) return false;
        if (!"1".equals(format) && !"1.0".equals(format)) return false;
        if (!"true".equals(enabled) && !"false".equals(enabled)) return false;
        if (description.length() > 512) return false;

        for (String key : new String[]{"spawn.sheep", "spawn.cow", "spawn.bird", "spawn.pig",
                "spawn.rabbit", "spawn.zombie", "spawn.skeleton"}) {
            String value = values.get(key);
            if (value == null) continue;
            try {
                int count = Integer.parseInt(value);
                if (count < 0 || count > 24) return false;
            } catch (NumberFormatException invalid) {
                return false;
            }
        }
        if (!isFiniteInRange(values.get("player.speed"), 0.5f, 2.0f)) return false;
        if (!isFiniteInRange(values.get("player.jump"), 0.75f, 1.5f)) return false;
        if (!isFiniteInRange(values.get("player.crouch_speed"), 0.25f, 1.0f)) return false;
        if (!isFiniteInRange(values.get("entity.speed"), 0.5f, 2.0f)) return false;
        if (!isFiniteInRange(values.get("entity.passive_speed"), 0.5f, 2.0f)) return false;
        if (!isFiniteInRange(values.get("entity.hostile_speed"), 0.5f, 2.0f)) return false;
        if (!isFiniteInRange(values.get("entity.flee_radius"), 2.0f, 16.0f)) return false;
        if (!isFiniteInRange(values.get("entity.alert_radius"), 4.0f, 32.0f)) return false;
        if (!isFiniteInRange(values.get("entity.spawn_radius"), 1.0f, 8.0f)) return false;
        if (!isFiniteInRange(values.get("world.day_speed"), 0.25f, 4.0f)) return false;
        if (!isFiniteInRange(values.get("drops.pickup_radius"), 0.8f, 4.0f)) return false;
        if (!isFiniteInRange(values.get("drops.lifetime"), 30.0f, 900.0f)) return false;
        if (!isFiniteInRange(values.get("mining.speed"), 0.5f, 3.0f)) return false;
        if (!isIntegerInRange(values.get("world.entity_cap"), 8, 128)) return false;
        if (!isFiniteInRange(values.get("world.spawn_interval"), 30.0f, 900.0f)) return false;
        if (!isFiniteInRange(values.get("world.spawn_distance"), 8.0f, 48.0f)) return false;
        if (!isIntegerInRange(values.get("world.passive_cap"), 1, 64)) return false;
        if (!isIntegerInRange(values.get("drops.quantity"), 1, 4)) return false;
        return true;
    }

    private static boolean isIntegerInRange(String value, int minimum, int maximum) {
        if (value == null) return true;
        try {
            int number = Integer.parseInt(value);
            return number >= minimum && number <= maximum;
        } catch (NumberFormatException invalid) {
            return false;
        }
    }

    private static boolean isFiniteInRange(String value, float minimum, float maximum) {
        if (value == null) return true;
        try {
            float number = Float.parseFloat(value);
            return !Float.isNaN(number) && !Float.isInfinite(number)
                    && number >= minimum && number <= maximum;
        } catch (NumberFormatException invalid) {
            return false;
        }
    }
}
