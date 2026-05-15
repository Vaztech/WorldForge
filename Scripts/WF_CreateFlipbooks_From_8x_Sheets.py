import os
import re
import unreal

# ============================================================
# WorldForge Crash-Safe 8x SpriteSheet → SpriteFrames → Flipbooks
#
# Source example:
#   Documentation/sprites/worldforge_sprites_v2/construct_folk/female/
#     construct_folk_female_idle_8x.png
#
# Destination example:
#   /Game/WorldForge/Characters/Sprites/ConstructFolk/Female/SpriteSheets/
#   /Game/WorldForge/Characters/Sprites/ConstructFolk/Female/SpriteFrames/Idle/
#   /Game/WorldForge/Characters/Sprites/ConstructFolk/Female/SpriteAnimations/
#
# Output flipbook:
#   FB_ConstructFolk_Female_Idle_8x
#
# Run from Unreal Python console:
# exec(open(r"C:/Users/Vaz/Documents/Unreal Projects/WorldForge/Scripts/WF_CreateFlipbooks_From_8x_Sheets.py").read())
# ============================================================

SOURCE_ROOT = r"C:/Users/Vaz/Documents/Unreal Projects/WorldForge/Documentation/sprites/worldforge_sprites_v2"
DEST_ROOT = "/Game/WorldForge/Characters/Sprites"

# Your sprite sheets are around 181x24 and appear to be horizontal 8-frame strips.
FRAMES_PER_SHEET = 8
USE_FULL_TEXTURE_HEIGHT = True

FPS = 8
ONLY_SPEED = "8x"

# Safety: process in batches so Unreal does not lock/crash.
MAX_FILES_PER_RUN = 16

# Process one race at a time. Change this after each successful run.
# Options:
# {"construct_folk"}, {"dwarf"}, {"high_elf"}, {"namekian"},
# {"fae_kin"}, {"tidecaller"}, {"oni"}, {"dark_elf"}
LIMIT_RACES = {"construct_folk"}

# Empty set means male + female.
LIMIT_SEXES = set()

# Core animation states you currently have.
LIMIT_ACTIONS = {
    "idle",
    "walk",
    "work",
    "carry",
    "fight",
    "hurt",
    "rest",
    "sleep",
}

# Set True to preview without creating assets.
DRY_RUN = False

# Existing assets are skipped/reused.
SKIP_EXISTING_FLIPBOOKS = True
SKIP_EXISTING_SPRITES = True
SKIP_EXISTING_TEXTURES = True


RACE_DISPLAY_NAMES = {
    "construct_folk": "ConstructFolk",
    "dark_elf": "DarkElf",
    "dwarf": "Dwarf",
    "fae_kin": "FaeKin",
    "high_elf": "HighElf",
    "namekian": "Namekian",
    "oni": "Oni",
    "tidecaller": "Tidecaller",
    "tidcealler": "Tidecaller",
    "tidcaller": "Tidecaller",
}

ACTION_DISPLAY_NAMES = {
    "idle": "Idle",
    "walk": "Walk",
    "work": "Work",
    "carry": "Carry",
    "fight": "Fight",
    "hurt": "Hurt",
    "rest": "Rest",
    "sleep": "Sleep",
    "dead": "Dead",
    "eat": "Eat",
    "social": "Social",
    "flee": "Flee",
    "mine": "Mine",
    "farm": "Farm",
    "build": "Build",
    "craft": "Craft",
    "research": "Research",
    "haul": "Haul",
    "doctor": "Doctor",
    "cook": "Cook",
    "guard": "Guard",
    "trade": "Trade",
    "pray": "Pray",
}


def log(message):
    unreal.log(f"[WorldForgeFlipbookTool] {message}")


def warn(message):
    unreal.log_warning(f"[WorldForgeFlipbookTool] {message}")


def ensure_dir(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def clean_name(name):
    name = re.sub(r"[^A-Za-z0-9_]", "_", name)
    name = re.sub(r"_+", "_", name)
    return name.strip("_")


def parse_sheet_name(path_or_filename):
    stem = os.path.splitext(os.path.basename(path_or_filename))[0].lower()

    match = re.match(
        r"^(?P<race>.+)_(?P<sex>female|male)_(?P<action>.+)_(?P<speed>\d+x)$",
        stem,
    )

    if not match:
        return None

    return {
        "race_key": match.group("race"),
        "sex_key": match.group("sex"),
        "action_key": match.group("action"),
        "speed": match.group("speed"),
    }


def display_race(race_key):
    return RACE_DISPLAY_NAMES.get(
        race_key,
        "".join(part.title() for part in race_key.split("_")),
    )


def display_action(action_key):
    return ACTION_DISPLAY_NAMES.get(
        action_key,
        "".join(part.title() for part in action_key.split("_")),
    )


def should_process(parsed):
    if parsed["speed"] != ONLY_SPEED:
        return False

    if LIMIT_RACES and parsed["race_key"] not in LIMIT_RACES:
        return False

    if LIMIT_SEXES and parsed["sex_key"] not in LIMIT_SEXES:
        return False

    if LIMIT_ACTIONS and parsed["action_key"] not in LIMIT_ACTIONS:
        return False

    return True


def force_sprite_texture_settings(texture):
    if not texture:
        return

    try:
        texture.set_editor_property("srgb", True)
    except Exception:
        pass

    try:
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    except Exception:
        pass

    try:
        texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    except Exception:
        pass

    try:
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    except Exception:
        pass

    try:
        texture.set_editor_property("defer_compression", False)
    except Exception:
        pass


def import_texture(source_file, destination_path, texture_name):
    ensure_dir(destination_path)

    asset_path = f"{destination_path}/{texture_name}"

    if SKIP_EXISTING_TEXTURES and unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        texture = unreal.EditorAssetLibrary.load_asset(asset_path)
        force_sprite_texture_settings(texture)
        return texture

    if DRY_RUN:
        log(f"DRY RUN import texture: {source_file} -> {asset_path}")
        return None

    task = unreal.AssetImportTask()
    task.filename = source_file
    task.destination_path = destination_path
    task.destination_name = texture_name
    task.automated = True
    task.save = False
    task.replace_existing = False

    try:
        texture_factory = unreal.TextureFactory()
        task.options = texture_factory
    except Exception:
        pass

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    if not task.imported_object_paths:
        warn(f"Failed to import texture: {source_file}")
        return None

    texture = unreal.EditorAssetLibrary.load_asset(task.imported_object_paths[0])

    if texture:
        force_sprite_texture_settings(texture)
        unreal.EditorAssetLibrary.save_loaded_asset(texture)

    return texture


def get_texture_size(texture):
    try:
        return int(texture.blueprint_get_size_x()), int(texture.blueprint_get_size_y())
    except Exception:
        pass

    warn(f"Could not read texture size for {texture.get_name()}; using 181x24 fallback.")
    return 181, 24


def create_sprite(texture, sprite_path, sprite_name, x, y, w, h):
    ensure_dir(sprite_path)

    asset_path = f"{sprite_path}/{sprite_name}"

    if SKIP_EXISTING_SPRITES and unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return unreal.EditorAssetLibrary.load_asset(asset_path)

    if DRY_RUN:
        log(f"DRY RUN create sprite: {asset_path} rect=({x},{y},{w},{h})")
        return None

    factory = unreal.PaperSpriteFactory()

    try:
        factory.set_editor_property("initial_texture", texture)
    except Exception:
        pass

    sprite = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        sprite_name,
        sprite_path,
        unreal.PaperSprite,
        factory,
    )

    if not sprite:
        warn(f"Failed to create sprite: {asset_path}")
        return None

    try:
        sprite.set_editor_property("source_texture", texture)
    except Exception as ex:
        warn(f"Could not set source_texture for {sprite_name}: {ex}")

    try:
        sprite.set_editor_property("source_uv", unreal.Vector2D(float(x), float(y)))
    except Exception as ex:
        warn(f"Could not set source_uv for {sprite_name}: {ex}")

    try:
        sprite.set_editor_property("source_dimension", unreal.Vector2D(float(w), float(h)))
    except Exception as ex:
        warn(f"Could not set source_dimension for {sprite_name}: {ex}")

    try:
        sprite.set_editor_property("pivot_mode", unreal.SpritePivotMode.CENTER_CENTER)
    except Exception:
        pass

    unreal.EditorAssetLibrary.save_loaded_asset(sprite)
    return sprite


def create_flipbook(sprites, flipbook_path, flipbook_name):
    ensure_dir(flipbook_path)

    asset_path = f"{flipbook_path}/{flipbook_name}"

    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        if SKIP_EXISTING_FLIPBOOKS:
            log(f"Flipbook already exists, skipping: {asset_path}")
            return unreal.EditorAssetLibrary.load_asset(asset_path)

        flipbook = unreal.EditorAssetLibrary.load_asset(asset_path)
    else:
        if DRY_RUN:
            log(f"DRY RUN create flipbook: {asset_path}")
            return None

        factory = unreal.PaperFlipbookFactory()
        flipbook = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            flipbook_name,
            flipbook_path,
            unreal.PaperFlipbook,
            factory,
        )

    if not flipbook:
        warn(f"Failed to create flipbook: {asset_path}")
        return None

    if DRY_RUN:
        return flipbook

    keyframes = []

    for sprite in sprites:
        if not sprite:
            continue

        keyframe = unreal.PaperFlipbookKeyFrame()
        keyframe.set_editor_property("sprite", sprite)
        keyframe.set_editor_property("frame_run", 1)
        keyframes.append(keyframe)

    try:
        flipbook.set_editor_property("frames_per_second", float(FPS))
    except Exception as ex:
        warn(f"Could not set FPS on {flipbook_name}: {ex}")

    try:
        flipbook.set_editor_property("key_frames", keyframes)
    except Exception as ex:
        warn(f"Could not set keyframes on {flipbook_name}: {ex}")

    unreal.EditorAssetLibrary.save_loaded_asset(flipbook)
    return flipbook


def get_frame_rects(texture_width, texture_height):
    frame_rects = []

    for frame_index in range(FRAMES_PER_SHEET):
        x0 = round((texture_width * frame_index) / FRAMES_PER_SHEET)
        x1 = round((texture_width * (frame_index + 1)) / FRAMES_PER_SHEET)

        width = max(1, x1 - x0)
        height = texture_height if USE_FULL_TEXTURE_HEIGHT else texture_height

        frame_rects.append((x0, 0, width, height))

    return frame_rects


def process_sheet(source_file):
    parsed = parse_sheet_name(source_file)

    if not parsed:
        return False

    if not should_process(parsed):
        return False

    race_key = parsed["race_key"]
    sex_key = parsed["sex_key"]
    action_key = parsed["action_key"]
    speed = parsed["speed"]

    race_name = display_race(race_key)
    sex_name = sex_key.title()
    action_name = display_action(action_key)

    asset_base = clean_name(f"{race_name}_{sex_name}_{action_name}_{speed}")

    base_path = f"{DEST_ROOT}/{race_name}/{sex_name}"
    texture_path = f"{base_path}/SpriteSheets"
    sprite_path = f"{base_path}/SpriteFrames/{action_name}"
    flipbook_path = f"{base_path}/SpriteAnimations"

    texture_name = f"T_{asset_base}"
    flipbook_name = f"FB_{asset_base}"

    flipbook_asset_path = f"{flipbook_path}/{flipbook_name}"

    if SKIP_EXISTING_FLIPBOOKS and unreal.EditorAssetLibrary.does_asset_exist(flipbook_asset_path):
        log(f"Already done: {flipbook_asset_path}")
        return True

    log(f"Processing: {source_file}")
    log(f"Race={race_name} Sex={sex_name} Action={action_name}")
    log(f"Output flipbook: {flipbook_path}/{flipbook_name}")

    texture = import_texture(source_file, texture_path, texture_name)

    if not texture and not DRY_RUN:
        return False

    if DRY_RUN:
        return True

    texture_width, texture_height = get_texture_size(texture)
    log(f"Texture size: {texture_width}x{texture_height}. Splitting into {FRAMES_PER_SHEET} frames.")

    frame_rects = get_frame_rects(texture_width, texture_height)

    sprites = []

    for frame_index, rect in enumerate(frame_rects):
        x, y, w, h = rect
        sprite_name = f"SPR_{asset_base}_{frame_index:02d}"

        sprite = create_sprite(
            texture,
            sprite_path,
            sprite_name,
            x,
            y,
            w,
            h,
        )

        if sprite:
            sprites.append(sprite)

    if not sprites:
        warn(f"No sprites created for {source_file}")
        return False

    flipbook = create_flipbook(sprites, flipbook_path, flipbook_name)
    return flipbook is not None


def collect_candidates():
    candidates = []

    for root, _, files in os.walk(SOURCE_ROOT):
        for filename in files:
            if not filename.lower().endswith(".png"):
                continue

            parsed = parse_sheet_name(filename)

            if not parsed:
                continue

            if not should_process(parsed):
                continue

            candidates.append(os.path.join(root, filename))

    return sorted(candidates)


def main():
    if not os.path.isdir(SOURCE_ROOT):
        warn(f"Source root does not exist: {SOURCE_ROOT}")
        return

    candidates = collect_candidates()

    if MAX_FILES_PER_RUN > 0:
        candidates = candidates[:MAX_FILES_PER_RUN]

    log(f"Starting run. Candidates={len(candidates)} MaxFiles={MAX_FILES_PER_RUN}")
    log(f"Limits: races={LIMIT_RACES or 'ALL'} sexes={LIMIT_SEXES or 'ALL'} actions={LIMIT_ACTIONS or 'ALL'}")

    total = len(candidates)
    success = 0

    for full_path in candidates:
        try:
            if process_sheet(full_path):
                success += 1
        except Exception as ex:
            warn(f"ERROR processing {full_path}: {ex}")

    log(f"Done. Created or updated {success}/{total} flipbooks from {ONLY_SPEED} sheets.")


main()