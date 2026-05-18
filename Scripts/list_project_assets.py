import unreal
import csv
import os
from collections import Counter

output_path = os.path.join(
    unreal.Paths.project_saved_dir(),
    "WorldForge_Asset_List_With_Counts.csv"
)

asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()

assets = asset_registry.get_assets_by_path(
    "/Game",
    recursive=True,
    include_only_on_disk_assets=False
)

rows = []
type_counts = Counter()

for asset in assets:
    asset_class = str(asset.asset_class_path.asset_name)

    type_counts[asset_class] += 1

    rows.append({
        "Asset Name": str(asset.asset_name),
        "Asset Class": asset_class,
        "Package Path": str(asset.package_path),
        "Package Name": str(asset.package_name),
        "Object Path": asset.get_full_name(),
    })

rows.sort(key=lambda x: (
    x["Asset Class"],
    x["Package Path"],
    x["Asset Name"]
))

with open(output_path, "w", newline="", encoding="utf-8") as csvfile:
    fieldnames = [
        "Asset Name",
        "Asset Class",
        "Package Path",
        "Package Name",
        "Object Path",
    ]

    writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

unreal.log("========== ASSET TYPE COUNTS ==========")

for asset_type, count in sorted(type_counts.items()):
    unreal.log(f"{asset_type}: {count}")

unreal.log("=======================================")
unreal.log(f"Exported {len(rows)} assets to: {output_path}")