package com.minefade.nativeengine;

import android.content.Context;
import android.net.Uri;
import android.system.Os;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.HashMap;

/** Data-only .mfmod packs. No native code or executable scripts are loaded. */
public final class ModManager {
    private static final int MAX_ZIP_BYTES = 16 * 1024 * 1024;
    private static final int MAX_UNPACKED_BYTES = 32 * 1024 * 1024;
    private static final int MAX_ENTRY_BYTES = 4 * 1024 * 1024;
    private static final int MAX_ZIP_ENTRIES = 256;
    private static final int MAX_CONTENT_DEFINITIONS = 256;
    private static final int ATLAS_TILE_COUNT = 1242;
    private static final int TILE_CELL = 18;
    private static final int TILE_SIZE = 16;

    public static final class AtlasData {
        public final byte[] pixels;
        public final int width;
        public final int height;
        AtlasData(byte[] pixels, int width, int height) {
            this.pixels = pixels;
            this.width = width;
            this.height = height;
        }
    }

    private static final class AtlasEntry {
        final Bitmap[] faces;
        final String registryLine;
        AtlasEntry(Bitmap[] faces, String registryLine) {
            this.faces = faces;
            this.registryLine = registryLine;
        }
    }

    public static final class ModInfo {
        public final File file;
        public final String id;
        public final String name;
        public final String version;
        public final String description;
        public final boolean enabled;
        public final Map<String, String> settings;
        public final boolean contentPack;

        ModInfo(File file, Map<String,String> values) {
            this.file = file;
            this.id = values.getOrDefault("id", file.getName());
            this.name = values.getOrDefault("name", id);
            this.version = values.getOrDefault("version", "1");
            this.description = values.getOrDefault("description", "Paquete de datos");
            this.enabled = Boolean.parseBoolean(values.getOrDefault("enabled", "false"));
            this.settings = values;
            this.contentPack = "manifest.ini".equals(file.getName());
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
                ModInfo info = readContentManifest(new File(file, "manifest.ini"));
                if (info != null) result.add(info);
            }
        }
        result.sort(Comparator.comparing(m -> m.id.toLowerCase(Locale.ROOT)));
        return result;
    }

    public synchronized ModInfo importMod(Uri uri) {
        if (uri == null) return null;
        File upload = new File(modsDirectory, ".import-" + System.currentTimeMillis() + ".tmp");
        try (InputStream input = context.getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(upload, false)) {
            if (input == null) return null;
            byte[] buffer = new byte[8192];
            int count, total = 0;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > MAX_ZIP_BYTES) throw new IOException("El mod supera 16 MB comprimido.");
                output.write(buffer, 0, count);
            }
            output.flush();
            output.getFD().sync();
        } catch (Exception error) {
            upload.delete();
            return null;
        }

        try {
            if (isZipFile(upload)) return importContentZip(upload);
            if (upload.length() > 32768L) return null;
            ModInfo validated = read(upload);
            if (validated == null) return null;
            for (ModInfo existing : listMods()) {
                if (existing.id.equalsIgnoreCase(validated.id)) return null;
            }
            String raw = uri.getLastPathSegment() == null ? "mod.mfmod" : uri.getLastPathSegment();
            String safe = new File(raw).getName().replaceAll("[^a-zA-Z0-9._-]", "_");
            if (!safe.toLowerCase(Locale.ROOT).endsWith(".mfmod")) safe += ".mfmod";
            File destination = new File(modsDirectory, System.currentTimeMillis() + "-" + safe);
            Os.rename(upload.getAbsolutePath(), destination.getAbsolutePath());
            return new ModInfo(destination, validated.settings);
        } catch (Exception ignored) {
            return null;
        } finally {
            upload.delete();
        }
    }

    private static boolean isZipFile(File file) {
        try (InputStream input = new FileInputStream(file)) {
            return input.read() == 'P' && input.read() == 'K';
        } catch (Exception ignored) { return false; }
    }

    private ModInfo importContentZip(File archive) throws Exception {
        File staging = new File(modsDirectory, ".pending-" + System.currentTimeMillis());
        if (!staging.mkdir()) return null;
        boolean installed = false;
        try {
            Set<String> seen = new HashSet<>();
            int entryCount = 0, unpacked = 0;
            try (ZipInputStream zip = new ZipInputStream(new FileInputStream(archive))) {
                ZipEntry entry;
                byte[] buffer = new byte[8192];
                while ((entry = zip.getNextEntry()) != null) {
                    if (++entryCount > MAX_ZIP_ENTRIES) throw new IOException("Demasiados archivos.");
                    String name = entry.getName();
                    validateZipPath(entry.isDirectory() && name.endsWith("/")
                            ? name.substring(0, name.length() - 1) : name);
                    if (entry.isDirectory()) { zip.closeEntry(); continue; }
                    if (!seen.add(name)) throw new IOException("Archivo ZIP duplicado.");
                    if (!"manifest.json".equals(name) &&
                            !(name.startsWith("textures/") && name.toLowerCase(Locale.ROOT).endsWith(".png")))
                        throw new IOException("El paquete contiene un tipo de archivo no permitido.");
                    File target = new File(staging, name);
                    String canonicalRoot = staging.getCanonicalPath() + File.separator;
                    if (!target.getCanonicalPath().startsWith(canonicalRoot))
                        throw new IOException("Ruta de archivo inválida.");
                    File parent = target.getParentFile();
                    if (parent != null && !parent.exists() && !parent.mkdirs())
                        throw new IOException("No se pudo crear la carpeta de recursos.");
                    try (FileOutputStream output = new FileOutputStream(target, false)) {
                        int entryBytes = 0, count;
                        while ((count = zip.read(buffer)) != -1) {
                            entryBytes += count;
                            unpacked += count;
                            if (entryBytes > MAX_ENTRY_BYTES || unpacked > MAX_UNPACKED_BYTES)
                                throw new IOException("El contenido descomprimido supera el límite.");
                            output.write(buffer, 0, count);
                        }
                        output.flush();
                    }
                    zip.closeEntry();
                }
            }

            File manifestFile = new File(staging, "manifest.json");
            if (!manifestFile.isFile() || manifestFile.length() <= 0 || manifestFile.length() > 1024 * 1024)
                throw new IOException("Falta manifest.json o es demasiado grande.");
            String json = new String(readAll(manifestFile, 1024 * 1024), StandardCharsets.UTF_8);
            JSONObject manifest = new JSONObject(json);
            validateContentManifest(staging, manifest);
            String id = manifest.getString("id");
            for (ModInfo existing : listMods()) {
                if (existing.id.equalsIgnoreCase(id)) throw new IOException("Ya existe un mod con ese ID.");
            }

            Map<String,String> metadata = new LinkedHashMap<>();
            metadata.put("format", "2");
            metadata.put("id", id);
            metadata.put("name", manifest.getString("name"));
            metadata.put("version", manifest.getString("version"));
            metadata.put("description", manifest.optString("description", ""));
            metadata.put("enabled", "false");
            StringBuilder ini = new StringBuilder("# Minefade content mod v2\n");
            for (Map.Entry<String,String> item : metadata.entrySet())
                ini.append(item.getKey()).append('=').append(item.getValue().replace("\n", " ")).append('\n');
            if (!writeAtomically(new File(staging, "manifest.ini"),
                    ini.toString().getBytes(StandardCharsets.UTF_8)))
                throw new IOException("No se pudo finalizar el manifiesto del mod.");

            File destination = new File(modsDirectory, id);
            if (destination.exists()) throw new IOException("La carpeta del mod ya existe.");
            Os.rename(staging.getAbsolutePath(), destination.getAbsolutePath());
            installed = true;
            return readContentManifest(new File(destination, "manifest.ini"));
        } finally {
            if (!installed) deleteRecursively(staging);
        }
    }

    private static void validateZipPath(String name) throws IOException {
        if (name == null || name.isEmpty() || name.startsWith("/") || name.indexOf('\\') >= 0 ||
                name.indexOf('\0') >= 0 || name.matches("^[A-Za-z]:.*"))
            throw new IOException("Ruta ZIP no válida.");
        String[] parts = name.split("/");
        for (String part : parts) {
            if ("..".equals(part) || ".".equals(part) || part.isEmpty())
                throw new IOException("El paquete contiene una ruta insegura.");
        }
    }

    private static byte[] readAll(File file, int maximum) throws IOException {
        try (InputStream input = new FileInputStream(file);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count, total = 0;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > maximum) throw new IOException("Archivo demasiado grande.");
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }

    private static void deleteRecursively(File file) {
        if (file == null || !file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteRecursively(child);
        }
        file.delete();
    }

    private static void validateContentManifest(File root, JSONObject manifest) throws Exception {
        if (manifest.optInt("format", 0) != 2) throw new IOException("Formato MFMod no compatible.");
        String id = manifest.optString("id", "");
        String name = manifest.optString("name", "");
        String version = manifest.optString("version", "");
        String description = manifest.optString("description", "");
        if (!id.matches("[a-z0-9][a-z0-9_.-]{1,63}") ||
                name.trim().isEmpty() || name.length() > 96 ||
                !version.matches("[0-9]+\\.[0-9]+\\.[0-9]+([+-][A-Za-z0-9.-]+)?") ||
                description.length() > 512 || description.indexOf('|') >= 0 ||
                description.indexOf('\n') >= 0 || description.indexOf('\r') >= 0)
            throw new IOException("Los metadatos del manifiesto no son válidos.");
        validateLabel(name);

        JSONArray blocks = manifest.optJSONArray("blocks");
        JSONArray items = manifest.optJSONArray("items");
        JSONArray recipes = manifest.optJSONArray("recipes");
        int blockCount = blocks == null ? 0 : blocks.length();
        int itemCount = items == null ? 0 : items.length();
        int recipeCount = recipes == null ? 0 : recipes.length();
        if (blockCount + itemCount > 128 || recipeCount > 128)
            throw new IOException("El mod supera el número permitido de bloques, objetos o recetas.");

        Set<String> ids = new HashSet<>();
        if (blocks != null) for (int i = 0; i < blocks.length(); i++) {
            JSONObject block = blocks.getJSONObject(i);
            String localId = requireLocalId(block.optString("id", ""));
            if (!ids.add(localId)) throw new IOException("ID de contenido duplicado.");
            validateLabel(block.optString("name", ""));
            JSONObject textures = block.getJSONObject("textures");
            validateTexture(root, textures.getString("top"));
            validateTexture(root, textures.getString("side"));
            validateTexture(root, textures.getString("bottom"));
            finiteRange(block.optDouble("hardness", 1.5), 0.0, 100.0);
            validateSound(block.optString("sound", "stone"));
            integerRange(block.optInt("stack", 64), 1, 64);
            integerRange(block.optInt("light", 0), 0, 15);
            validateShape(block.optString("shape", "cube"));
            validateTool(block.optString("tool", "pickaxe"));
        }
        if (items != null) for (int i = 0; i < items.length(); i++) {
            JSONObject item = items.getJSONObject(i);
            String localId = requireLocalId(item.optString("id", ""));
            if (!ids.add(localId)) throw new IOException("ID de contenido duplicado.");
            validateLabel(item.optString("name", ""));
            String texture = item.optString("texture", "");
            if (!texture.isEmpty()) {
                validateTexture(root, texture);
            } else {
                JSONObject textures = item.getJSONObject("textures");
                validateTexture(root, textures.getString("top"));
                validateTexture(root, textures.getString("side"));
                validateTexture(root, textures.getString("bottom"));
            }
            integerRange(item.optInt("stack", 64), 1, 64);
        }
        if (recipes != null) for (int i = 0; i < recipes.length(); i++) {
            JSONObject recipe = recipes.getJSONObject(i);
            requireLocalId(recipe.optString("id", ""));
            validateLabel(recipe.optString("name", ""));
            JSONObject output = recipe.getJSONObject("output");
            validateReference(output.optString("id", ""), id);
            integerRange(output.optInt("count", 1), 1, 64);
            JSONArray ingredients = recipe.getJSONArray("ingredients");
            if (ingredients.length() < 1 || ingredients.length() > 5)
                throw new IOException("Una receta necesita entre 1 y 5 ingredientes.");
            for (int k = 0; k < ingredients.length(); k++) {
                JSONObject ingredient = ingredients.getJSONObject(k);
                validateReference(ingredient.optString("id", ""), id);
                integerRange(ingredient.optInt("count", 1), 1, 64);
            }
        }
    }

    private static String requireLocalId(String value) throws IOException {
        if (value == null || !value.matches("[a-z0-9][a-z0-9_.-]{0,63}"))
            throw new IOException("ID de recurso inválido.");
        return value;
    }
    private static void validateLabel(String value) throws IOException {
        if (value == null || value.trim().isEmpty() || value.length() > 96 ||
                value.indexOf('|') >= 0 || value.indexOf('\n') >= 0 || value.indexOf('\r') >= 0)
            throw new IOException("Nombre de recurso inválido.");
    }
    private static String validateReference(String value, String ownId) throws IOException {
        if (value == null || value.isEmpty() || value.length() > 128 ||
                value.indexOf('|') >= 0 || value.indexOf('\n') >= 0)
            throw new IOException("Referencia de recurso inválida.");
        if (value.startsWith("minecraft:")) return value.substring("minecraft:".length());
        if (value.indexOf(':') >= 0) {
            String[] parts = value.split(":", -1);
            if (parts.length != 2 || !parts[0].matches("[a-z0-9][a-z0-9_.-]{1,63}") ||
                    !parts[1].matches("[a-z0-9][a-z0-9_.-]{0,63}"))
                throw new IOException("Referencia con namespace inválida.");
            return value;
        }
        if (!value.matches("[a-z0-9][a-z0-9_.-]{0,63}"))
            throw new IOException("Referencia de recurso inválida.");
        return value;
    }
    private static void validateTexture(File root, String path) throws IOException {
        if (path == null || !path.startsWith("textures/") ||
                !path.toLowerCase(Locale.ROOT).endsWith(".png") ||
                path.indexOf('\\') >= 0 || path.contains(".."))
            throw new IOException("Ruta de textura inválida.");
        File file = new File(root, path);
        if (!file.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator) || !file.isFile())
            throw new IOException("No se encontró una textura del manifiesto.");
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(file.getAbsolutePath(), options);
        if (options.outWidth < 1 || options.outHeight < 1 ||
                options.outWidth > 128 || options.outHeight > 128)
            throw new IOException("Las texturas deben ser PNG de hasta 128×128.");
    }
    private static boolean oneOf(String value, String... choices) {
        if (value == null) return false;
        for (String choice : choices) if (choice.equals(value)) return true;
        return false;
    }

    private static void validateSound(String value) throws IOException {
        if (!oneOf(value, "stone", "wood", "gravel", "sand", "grass", "glass", "metal"))
            throw new IOException("Tipo de sonido no permitido.");
    }
    private static void validateShape(String value) throws IOException {
        if (!oneOf(value, "cube", "slab", "stairs", "cross_plant", "thin", "pane",
                "fence", "door", "trapdoor", "fluid", "scaffolding"))
            throw new IOException("Forma de bloque no permitida.");
    }
    private static void validateTool(String value) throws IOException {
        if (!oneOf(value, "none", "pickaxe", "axe", "shovel", "shears"))
            throw new IOException("Herramienta no permitida.");
    }
    private static void finiteRange(double value, double minimum, double maximum) throws IOException {
        if (Double.isNaN(value) || Double.isInfinite(value) || value < minimum || value > maximum)
            throw new IOException("Valor fuera de rango.");
    }
    private static void integerRange(int value, int minimum, int maximum) throws IOException {
        if (value < minimum || value > maximum) throw new IOException("Valor entero fuera de rango.");
    }

    private ModInfo readContentManifest(File file) {
        if (file == null || !file.isFile() || file.length() <= 0L || file.length() > 4096L) return null;
        Map<String,String> values = new LinkedHashMap<>();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new FileInputStream(file), StandardCharsets.UTF_8))) {
            String line;
            int count = 0;
            while ((line = reader.readLine()) != null) {
                if (++count > 16) return null;
                int split = line.indexOf('=');
                if (split <= 0) continue;
                String key = line.substring(0, split).trim();
                String value = line.substring(split + 1).trim();
                if (!oneOf(key, "format", "id", "name", "version", "description", "enabled") ||
                        value.indexOf('\n') >= 0 || value.indexOf('\r') >= 0 || value.length() > 512 ||
                        values.containsKey(key)) return null;
                values.put(key, value);
            }
            if (!"2".equals(values.get("format")) ||
                    !values.getOrDefault("id", "").matches("[a-z0-9][a-z0-9_.-]{1,63}") ||
                    values.getOrDefault("name", "").trim().isEmpty() ||
                    !"true".equals(values.get("enabled")) && !"false".equals(values.get("enabled")))
                return null;
            return new ModInfo(file, values);
        } catch (Exception ignored) { return null; }
    }

    public synchronized AtlasData buildModAtlas(byte[] baseAtlas, int width, int height) {
        if (baseAtlas == null || width != 576 || height != 702 ||
                baseAtlas.length != width * height * 4) return new AtlasData(baseAtlas, width, height);
        ArrayList<AtlasEntry> entries = new ArrayList<>();
        Map<File, ArrayList<String>> blockLines = new HashMap<>();
        Map<File, ArrayList<String>> recipeLines = new HashMap<>();
        ArrayList<Bitmap> ownedBitmaps = new ArrayList<>();
        try {
            List<ModInfo> mods = listMods();
            for (ModInfo mod : mods) {
                if (!mod.contentPack || !mod.enabled) continue;
                File root = mod.file.getParentFile();
                JSONObject manifest = new JSONObject(new String(
                        readAll(new File(root, "manifest.json"), 1024 * 1024), StandardCharsets.UTF_8));
                validateContentManifest(root, manifest);
                ArrayList<String> lines = new ArrayList<>();
                JSONArray blocks = manifest.optJSONArray("blocks");
                if (blocks != null) for (int i = 0; i < blocks.length(); i++) {
                    JSONObject block = blocks.getJSONObject(i);
                    JSONObject textures = block.getJSONObject("textures");
                    Bitmap[] faces = loadFaces(root, textures.getString("top"),
                            textures.getString("side"), textures.getString("bottom"));
                    for (Bitmap face : faces) ownedBitmaps.add(face);
                    int tile = ATLAS_TILE_COUNT + entries.size() * 3;
                    String key = mod.id + ":" + block.getString("id");
                    int flags = block.optBoolean("transparent", false)
                            ? (1 | 4 | 64) : (1 | 2 | 64);
                    String line = registryLine(key, block.getString("name"),
                            (float) block.optDouble("hardness", 1.5),
                            tile, tile + 1, tile + 2, block.optString("sound", "stone"),
                            block.optInt("stack", 64), block.optInt("light", 0), flags,
                            block.optString("shape", "cube"), block.optString("tool", "pickaxe"));
                    entries.add(new AtlasEntry(faces, line));
                    lines.add(line);
                }
                JSONArray items = manifest.optJSONArray("items");
                if (items != null) for (int i = 0; i < items.length(); i++) {
                    JSONObject item = items.getJSONObject(i);
                    Bitmap[] faces;
                    if (item.has("texture")) {
                        String texture = item.getString("texture");
                        faces = loadFaces(root, texture, texture, texture);
                    } else {
                        JSONObject textures = item.getJSONObject("textures");
                        faces = loadFaces(root, textures.getString("top"),
                                textures.getString("side"), textures.getString("bottom"));
                    }
                    for (Bitmap face : faces) ownedBitmaps.add(face);
                    int tile = ATLAS_TILE_COUNT + entries.size() * 3;
                    String key = mod.id + ":" + item.getString("id");
                    String line = registryLine(key, item.getString("name"), 0.0f,
                            tile, tile + 1, tile + 2, "", item.optInt("stack", 64),
                            0, 0, "none", "none");
                    entries.add(new AtlasEntry(faces, line));
                    lines.add(line);
                }
                blockLines.put(root, lines);

                ArrayList<String> recipes = new ArrayList<>();
                JSONArray recipeArray = manifest.optJSONArray("recipes");
                if (recipeArray != null) for (int i = 0; i < recipeArray.length(); i++) {
                    JSONObject recipe = recipeArray.getJSONObject(i);
                    JSONObject output = recipe.getJSONObject("output");
                    StringBuilder line = new StringBuilder("mod:")
                            .append(mod.id).append(':').append(recipe.getString("id")).append('|')
                            .append(recipe.getString("name")).append('|')
                            .append(normalizeReference(output.getString("id"), mod.id)).append('|')
                            .append(output.optInt("count", 1)).append('|')
                            .append(recipe.optBoolean("requiresTable", false));
                    JSONArray ingredients = recipe.getJSONArray("ingredients");
                    for (int k = 0; k < ingredients.length(); k++) {
                        JSONObject ingredient = ingredients.getJSONObject(k);
                        line.append('|').append(normalizeReference(ingredient.getString("id"), mod.id))
                                .append('|').append(ingredient.optInt("count", 1));
                    }
                    recipes.add(line.toString());
                }
                recipeLines.put(root, recipes);
            }

            if (entries.size() > MAX_CONTENT_DEFINITIONS)
                throw new IOException("Demasiados recursos de mods activos.");
            for (Map.Entry<File, ArrayList<String>> entry : blockLines.entrySet())
                writeAtomically(new File(entry.getKey(), "blocks.mfdata"),
                        joinLines(entry.getValue()).getBytes(StandardCharsets.UTF_8));
            for (Map.Entry<File, ArrayList<String>> entry : recipeLines.entrySet())
                writeAtomically(new File(entry.getKey(), "recipes.mfdata"),
                        joinLines(entry.getValue()).getBytes(StandardCharsets.UTF_8));

            int totalTiles = ATLAS_TILE_COUNT + entries.size() * 3;
            int rows = (totalTiles + 31) / 32;
            int atlasHeight = rows * TILE_CELL;
            byte[] atlas = new byte[width * atlasHeight * 4];
            System.arraycopy(baseAtlas, 0, atlas, 0, baseAtlas.length);
            for (int i = 0; i < entries.size(); i++) {
                AtlasEntry entry = entries.get(i);
                for (int face = 0; face < 3; face++)
                    blitTile(atlas, width, atlasHeight, ATLAS_TILE_COUNT + i * 3 + face, entry.faces[face]);
            }
            return new AtlasData(atlas, width, atlasHeight);
        } catch (Exception error) {
            for (Bitmap bitmap : ownedBitmaps) if (bitmap != null && !bitmap.isRecycled()) bitmap.recycle();
            // Never return a half-built atlas with registry files from a different layout.
            for (ModInfo mod : listMods()) if (mod.contentPack) {
                File root = mod.file.getParentFile();
                writeAtomically(new File(root, "blocks.mfdata"), new byte[0]);
                writeAtomically(new File(root, "recipes.mfdata"), new byte[0]);
            }
            return new AtlasData(baseAtlas, width, height);
        } finally {
            for (Bitmap bitmap : ownedBitmaps) if (bitmap != null && !bitmap.isRecycled()) bitmap.recycle();
        }
    }

    private Bitmap[] loadFaces(File root, String top, String side, String bottom) throws IOException {
        return new Bitmap[]{loadTexture(root, top), loadTexture(root, side), loadTexture(root, bottom)};
    }
    private Bitmap loadTexture(File root, String relative) throws IOException {
        File file = new File(root, relative);
        Bitmap source = BitmapFactory.decodeFile(file.getAbsolutePath());
        if (source == null) throw new IOException("No se pudo decodificar una textura PNG.");
        Bitmap scaled = Bitmap.createScaledBitmap(source, TILE_SIZE, TILE_SIZE, false);
        if (scaled != source) source.recycle();
        return scaled;
    }
    private static void blitTile(byte[] atlas, int width, int height, int tile, Bitmap bitmap) {
        int[] pixels = new int[TILE_SIZE * TILE_SIZE];
        bitmap.getPixels(pixels, 0, TILE_SIZE, 0, 0, TILE_SIZE, TILE_SIZE);
        int originX = (tile % 32) * TILE_CELL;
        int originY = (tile / 32) * TILE_CELL;
        for (int y = 0; y < TILE_CELL; y++) {
            int sy = Math.max(0, Math.min(TILE_SIZE - 1, y - 1));
            for (int x = 0; x < TILE_CELL; x++) {
                int sx = Math.max(0, Math.min(TILE_SIZE - 1, x - 1));
                int argb = pixels[sy * TILE_SIZE + sx];
                int at = ((originY + y) * width + originX + x) * 4;
                if (originY + y >= height || originX + x >= width) continue;
                atlas[at] = (byte) ((argb >> 16) & 255);
                atlas[at + 1] = (byte) ((argb >> 8) & 255);
                atlas[at + 2] = (byte) (argb & 255);
                atlas[at + 3] = (byte) ((argb >>> 24) & 255);
            }
        }
    }
    private static String registryLine(String key, String name, float hardness,
                                       int top, int side, int bottom, String sound,
                                       int stack, int light, int flags, String shape, String tool) {
        return key + "|" + name + "|" + hardness + "|" + top + "|" + side + "|" + bottom +
                "|" + sound + "|" + stack + "|" + light + "|" + flags + "|" + shape + "|" + tool;
    }
    private static String normalizeReference(String reference, String ownId) {
        if (reference.startsWith("minecraft:")) return reference.substring("minecraft:".length());
        return reference.indexOf(':') >= 0 ? reference : reference;
    }
    private static String joinLines(List<String> lines) {
        StringBuilder result = new StringBuilder();
        for (String line : lines) result.append(line).append('\n');
        return result.toString();
    }


    public synchronized boolean setEnabled(ModInfo info, boolean enabled) {
        if (info == null || info.file == null || !info.file.isFile()) return false;
        try {
            Map<String,String> values = new LinkedHashMap<>(info.settings);
            values.put("enabled", Boolean.toString(enabled));
            StringBuilder data = new StringBuilder(info.contentPack
                    ? "# Minefade content mod v2\n" : "# Minefade data mod v1\n");
            for (Map.Entry<String,String> entry : values.entrySet()) {
                String key = entry.getKey();
                String value = entry.getValue();
                if (key.matches("[a-zA-Z0-9_.-]{1,64}") && value.indexOf('\n') < 0 && value.indexOf('\r') < 0)
                    data.append(key).append('=').append(value).append('\n');
            }
            return writeAtomically(info.file, data.toString().getBytes(StandardCharsets.UTF_8));
        } catch (Exception ignored) {
            return false;
        }
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
            if (!isValidPack(values)) return null;
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
