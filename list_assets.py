import unreal, os, collections

PROJECT_DIR = r"C:\Users\Vaz\Documents\Unreal Projects\WorldForge"

registry = unreal.AssetRegistryHelpers.get_asset_registry()
assets = registry.get_assets_by_path("/Game", recursive=True)

# Group by top-level pack folder and asset class
packs = collections.defaultdict(lambda: collections.defaultdict(list))
for a in assets:
    parts = str(a.package_name).split("/")
    if len(parts) < 3:
        continue
    pack = parts[2]                          # e.g. "FantasticDungeonPack"
    cls = str(a.asset_class_path.asset_name) # e.g. "StaticMesh"
    packs[pack][cls].append(str(a.package_name))

# Write a manifest to the project root
out_path = os.path.join(PROJECT_DIR, "AssetManifest.txt")
with open(out_path, "w", encoding="utf-8") as f:
    for pack in sorted(packs.keys()):
        f.write(f"=== {pack} ===\n")
        for cls in sorted(packs[pack].keys()):
            paths = sorted(packs[pack][cls])
            f.write(f"  [{cls}] ({len(paths)})\n")
            for p in paths:
                f.write(f"    {p}\n")
        f.write("\n")

unreal.log(f"Wrote manifest to {out_path}")