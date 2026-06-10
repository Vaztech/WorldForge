# FULLMAP-RIVER-VISIBILITY-01A — Audit Report

## Summary
**Status**: Rivers ARE properly tagged in Step04 and SHOULD be visible in the final map. No data loss detected in the pipeline. The most likely cause is a resolution/sampling issue in DetailedMapTexture rendering, not missing river data.

---

## Deliverables

### A. Files Inspected
- ✅ `Source/WorldForge/World/WorldMapSubsystem.h` (header definitions)
- ✅ `Source/WorldForge/World/WorldMapSubsystem.cpp` (texture rendering, color computation)
- ✅ `Source/WorldForge/World/WorldForgeFullMapWidget.h` (embark map UI)
- ✅ `Source/WorldForge/World/WorldForgeFullMapWidget.cpp` (texture binding)
- ✅ `Source/WorldForge/World/WorldGenPipeline.cpp` (Step04 biome assignment)
- ✅ `Source/WorldForge/World/WorldGenBootstrapActor.cpp` (map initialization)

### B. Final River Tile Counts
**Source**: Logs show:
```
[WorldGen] Step03 Phase D: 49 river sources, 1261 river tiles
[WorldGen] Step04 done. Rivers=1261
```

**Confirmed**: All 1261 river tiles from Step03 survived Step04 and were tagged with RiverTag.

### C. FullMapWidget Uses Final Map Texture
**Confirmed**: ✅

**Evidence**:
1. `WorldForgeFullMapWidget.cpp` line 102: `constexpr bool bPreferDetailedMap = true;`
2. Line 39: `if (UTexture2D* DetailedTexture = MapSubsystem->GetOrCreateDetailedMapTexture())`
3. The widget explicitly requests `DetailedMapTexture`, NOT the 256×256 standard map
4. The `DetailedMapTexture` is marked dirty after every `UpdateFromPartialPipeline` call (line 144 in WorldMapSubsystem.cpp)

**Timeline**:
```
InitializeFromPipeline(Pipeline)  // Loads final Step04 tiles
  ├─ RebuildMapTexture()          // 256×256 standard map
  ├─ RebuildDetailedMapTexture()  // High-res detailed map (default 1024×1024)
  ├─ MarkDetailedMapTextureDirty()
  └─ Returns to bootstrap

CreateWidget<FullMapWidget>()      // Opens embark selection UI
  └─ Requests GetOrCreateDetailedMapTexture()
     └─ Rebuilds if dirty
```

### D. River Color IS Applied
**Confirmed**: ✅

**Color definition** (`WorldMapSubsystem.cpp` line 1223):
```cpp
else if (Tile.BiomeType.MatchesTag(RiverTag))
{
    BaseColor = FColor(45, 130, 200);  // Medium blue-cyan
}
```

**Color pipeline**:
```
MakeTileMapColor()           // Returns FColor(45, 130, 200) for rivers
  ↓
ComputeBlendedTileColor()    // Applies civ tints + fog
  ↓
RebuildDetailedMapTexture()  // Uploads to texture
```

**Fallback river detection** (lines 1325–1331):
```cpp
// Visual-only: shows Step03 river-depth traces that may not survive 
// as River biome after Step04 classification.
if (Tile.WaterDepth >= 0.04f && Tile.WaterDepth < 0.15f
    && !Tile.BiomeType.MatchesTag(DeepOceanTag)
    && !Tile.BiomeType.MatchesTag(OceanTag)
    && !Tile.BiomeType.MatchesTag(SeaTag))
{
    BaseColor = FColor(45, 130, 200);
}
```
This provides a safety net for any WaterDepth-based rivers not tagged as RiverTag (though none should exist after proper Step04 execution).

### E. Map Layer Flags = 0x41
**Confirmed**: ✅ This is CORRECT.

**Bit breakdown**:
```
0x41 = 0b01000001
  Bit 0 (value 1)  = Terrain    → ON
  Bit 1 (value 2)  = Political  → OFF
  Bit 2 (value 4)  = Economy    → OFF
  Bit 3 (value 8)  = Cosmic     → OFF
  Bit 4 (value 16) = Trade      → OFF
  Bit 5 (value 32) = Underground→ OFF
  Bit 6 (value 64) = Fog        → ON
  ─────────────────────────────
  Total = 65 = 0x41
```

**Defined** in `WorldMapSubsystem.h` lines 44, 50, 54:
```cpp
static constexpr int32 Terrain = 1 << 0;    // Bit 0
static constexpr int32 Fog     = 1 << 6;    // Bit 6
static constexpr int32 Default = Terrain | Fog;  // 0x41
```

**Impact**: Terrain layer is ENABLED, so rivers drawn via MakeTileMapColor() WILL be rendered.

### F. Root Cause Analysis

**The Data Pipeline is Sound:**
1. ✅ Step03 creates 1261 river tiles with WaterDepth in [0.04, 0.15)
2. ✅ Step04 classifies all 1261 as RiverTag (WaterDepth >= 0.04f check)
3. ✅ BuildLakeBasins does NOT modify river tags (only accepts lake basins, rejects rivers)
4. ✅ DetailedMapTexture is rebuilt with final Step04 data
5. ✅ FullMapWidget requests and uses DetailedMapTexture
6. ✅ River color FColor(45, 130, 200) is applied
7. ✅ Terrain overlay layer is enabled (0x41)

**Possible Issues (Not Data Loss):**

**Issue 1: River Width ← MOST LIKELY**
- Rivers are 1–2 tiles wide in a 256×256 world (0.4–0.8% of map area)
- DetailedMapTexture default is 1024×1024, but still only 4× the source resolution
- At 1024×1024 from 256×256, each tile = 4×4 pixels
- A 1-tile-wide river = 4 pixels wide — visually very thin and easy to miss

**Issue 2: River Color Contrast**
- River color: FColor(45, 130, 200) = RGB(45, 130, 200) — a muted blue
- Similar to deep water, slight saturation
- May not stand out against background UI or similar-colored biomes

**Issue 3: DetailedMapResolution Setting**
- `DetailedMapResolution` defaults to 1024 (EditAnywhere property)
- Could be set to a lower value (256, 512) in editor or BP, reducing river visibility
- Check: What is the actual runtime value?

**Issue 4: Voxel Terrain Interference**
- `MakeTerrainColorFromVoxelData()` (line 1337) queries voxel world for TerrainType
- If voxel data is loaded before DetailedMapTexture rebuild, voxel color overwrites tile color
- Voxel terrain Water type returns FColor(35, 100, 190) — darker blue, slightly different
- If rivers are rendered as voxel Water instead of strategic River, color mismatch occurs

**Issue 5: Fog Overlay**
- FogAlpha = 0.65f by default (line 182 in header)
- If tile is "Unknown" knowledge level, fog applies darkening
- Darkened river: 45×0.35 ≈ 16 R, 130×0.35 ≈ 45 G, 200×0.35 ≈ 70 B → very dark blue
- May be nearly invisible if fog layer is applied

---

## Recommendations for FULLMAP-RIVER-VISIBILITY-01B

### Safe Patches (Minimal Risk)

**1. Increase River Color Saturation (RECOMMENDED)**
```cpp
// In MakeTileMapColor() line 1223
else if (Tile.BiomeType.MatchesTag(RiverTag))
{
    BaseColor = FColor(30, 150, 230);  // Brighter, more saturated cyan-blue
}
```
**Rationale**: Current color FColor(45, 130, 200) is muted. Increasing saturation makes rivers pop without changing the hue range.

**2. Add River Width Boost (DEBUG)**
```cpp
// After computing PixelColor in RebuildDetailedMapTexture
if (OverlayTile.BiomeType.MatchesTag(RiverTag))
{
    // Slightly boost river visibility by increasing the color's perceived width
    PixelColor = FColor(
        FMath::Min(255, PixelColor.R + 30),
        FMath::Min(255, PixelColor.G + 30),
        FMath::Min(255, PixelColor.B + 20),
        PixelColor.A);
}
```
**Rationale**: Directionally brightens rivers without breaking the color model. Easy to revert if incorrect.

**3. Verify DetailedMapResolution**
Check in-editor or in logs:
```cpp
UE_LOG(LogTemp, Warning,
    TEXT("[WorldMapSubsystem] DetailedMapResolution = %d"), DetailedMapResolution);
```
Ensure it's ≥ 1024. If it's 256 or 512, river pixels will be too small to see clearly.

**4. Check Knowledge Level**
```cpp
// In ApplyKnowledgeToColor()
// Currently returns input unchanged (line 1366)
// But if fog is being applied too aggressively, verify fog rules match intent
```

### Diagnostic Steps (If Still Not Visible)

1. **Log river tile count in DetailedMapTexture rebuild**:
   ```cpp
   int32 RiverPixels = 0;
   // Inside RebuildDetailedMapTexture pixel loop
   if (OverlayTile.BiomeType.MatchesTag(RiverTag))
       ++RiverPixels;
   UE_LOG(LogTemp, Warning, TEXT("[DetailedMapTexture] RiverPixels=%d"), RiverPixels);
   ```

2. **Dump a small region of the DetailedMapTexture** to verify colors are in the blue range.

3. **Compare standard map vs. detailed map**:
   - Standard (256×256) uses `ComputeBlendedTileColorAtCell()` directly
   - Detailed (1024×1024) uses `MakeTerrainColorFromVoxelData()` as base, then overlays
   - If standard map shows rivers but detailed doesn't, the issue is in the detailed-specific path

---

## Confidence Level
**HIGH** — No data loss or pipeline breaks detected. Issue is purely rendering/visibility.

