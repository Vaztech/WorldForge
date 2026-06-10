#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/DataTable.h"
#include "VoxelWorldManager.h"
#include "WorldForgeTerrainSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "WorldForge Terrain Settings"))
class WORLDFORGE_API UWorldForgeTerrainSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UWorldForgeTerrainSettings();

    virtual FName GetCategoryName() const override
    {
        return FName(TEXT("WorldForge"));
    }

    // ============================================================
    // 1. Voxel
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel", meta = (ClampMin = "1.0"))
    float ChunkWorldSize = 3200.0f;

    // CUBE-VOXEL-SCALE: single source of truth for grid resolution.
    // One terrain tile (ChunkWorldSize wide) is divided into SubdivisionsPerTile cells
    // per axis. This drives BOTH the render block grid AND the mining/selection voxel
    // grid so they always align. Default 16 → 16x16 cells per tile → 200uu cells at the
    // 3200uu tile size (2m DF-style). Change ONLY this to re-scale the whole grid:
    //   VoxelSize           = ChunkWorldSize / SubdivisionsPerTile  (derived in ctor)
    //   render BlocksPerTile = SubdivisionsPerTile                  (fed at build time)
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel", meta = (ClampMin = "1", ClampMax = "64"))
    int32 SubdivisionsPerTile = 16;

    // Derived from ChunkWorldSize / SubdivisionsPerTile in the constructor. Kept as a
    // stored field for editor visibility and back-compat with callers that read it
    // directly, but it must NOT be edited independently — change SubdivisionsPerTile.
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel", meta = (ClampMin = "1.0"))
    float VoxelSize = 200.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel")
    float TerrainSurfaceZOffset = 50.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel")
    bool bAddGlobalFoundation = true;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel", meta = (ClampMin = "0.0"))
    float GlobalFoundationDepthWorld = 5000.0f;

    // ============================================================
    // 2. Perlin
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Perlin")
    FPerlinOctaveSettings DefaultPerlinSettings;

    // ============================================================
    // 3. Terrain (Elevation Bands)
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SandMaxElevationNormalized = 0.15f;

    // FIX: Was 0.38f — with the land bias shifting mean tile elevation to ~0.60,
    // a typical land tile produces NormElev ~0.45, already past the old Dirt threshold.
    // The Grass band (SandMax=0.15 to DirtMin=0.38) only existed in a razor-thin
    // coastal transition zone that was already consumed by the Sand coast-band check.
    // Raising to 0.52 gives Grass the NormElev range 0.15–0.52, which covers the
    // broad lowland/plains territory between coast and foothills.
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Classification", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DirtMinElevationNormalized = 0.52f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RockMinElevationNormalized = 0.70f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Classification", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HighRockMinElevationNormalized = 0.60f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SnowMinElevationNormalized = 0.88f;

    // ============================================================
    // 4. Base (Height Scaling)
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Base", meta = (ClampMin = "0.0"))
    float BaseHeight = 4000.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Base", meta = (ClampMin = "0.0"))
    float HeightMultiplier = 6000.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Base", meta = (ClampMin = "0.0"))
    float ContinentScale = 0.00005f;

    // ============================================================
    // 5. Detail
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Detail", meta = (ClampMin = "0.0"))
    float DetailScale = 0.002f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Detail", meta = (ClampMin = "0.0"))
    float DetailAmplitude = 120.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Detail", meta = (ClampMin = "1", ClampMax = "8"))
    int32 DetailOctaves = 3;

    // ============================================================
    // 6. Water
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Water")
    int32 SeaLevelVoxels = 10;

    // ── v2 Terrain Height System ──
    // These define how Tile.Elevation (0–1) maps to voxel height.
    // Single source of truth — no secondary mountain/basin/river noise.

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|v2", meta = (ClampMin = "0.05", ClampMax = "0.95"))
    float WaterThreshold = 0.48f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|v2", meta = (ClampMin = "1"))
    int32 OceanDepthVoxels = 30;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|v2", meta = (ClampMin = "1"))
    int32 MaxLandHeightVoxels = 150;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|v2", meta = (ClampMin = "0.1", ClampMax = "2.0"))
    float LandElevationExponent = 0.65f;

    // ============================================================
// 7. Macro
// ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    float ContinentFrequency = 0.002f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    int32 ContinentHeightRangeVoxels = 150;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    float ContinentExponent = 0.7f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    float BasinFrequency = 0.00055f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    float BasinThreshold = 0.82f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    int32 LakeBasinDepthVoxels = 14;

    // ============================================================
    // 7A. Mountains
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "0"))
    int32 MountainRangeCount = 8;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "1"))
    int32 MountainMinLengthTiles = 24;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "1"))
    int32 MountainMaxLengthTiles = 80;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "1.0"))
    float MountainMinWidthVoxels = 10.0f;   // was 6.0 — broader base for highland regions

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "1.0"))
    float MountainMaxWidthVoxels = 28.0f;   // was 18.0 — wider ridges create more usable mountain terrain

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "0.0"))
    float MountainMinHeightVoxels = 12.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "0.0"))
    float MountainMaxHeightVoxels = 42.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "0.1"))
    float MountainFalloffPower = 1.6f;      // was 2.2 — gentler falloff = broader ridges, not needles

    // ── v2.1 Mountain Ridge Detail ──
    // Controls the localized ridge displacement from Step03 tectonic ranges.
    // Additive on top of the macro tile-elevation height.

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "0.0"))
    float MountainRidgeHeightScale = 40.0f;   // overall ridge height multiplier (voxels). 0 = no ridges.

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "0.1"))
    float MountainRidgeWidthScale = 0.8f;     // ridge width multiplier. Lower = narrower/sharper spines.

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "0.001"))
    float MountainCragFrequency = 0.06f;      // high-frequency noise for jagged peaks.

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Mountains", meta = (ClampMin = "0.0"))
    float MountainCragAmplitude = 6.0f;       // crag noise intensity in voxels.

    // ============================================================
    // 8. Rivers
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Rivers")
    float RiverFrequency = 0.001f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Rivers")
    float RiverThreshold = 0.95f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Rivers")
    int32 RiverCarveDepthVoxels = 6;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Rivers")
    float RiverSharpness = 1.5f;

    // ============================================================
    // 9. Caves
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves")
    bool bEnableCaves = true;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves", meta = (ClampMin = "0.0"))
    float CaveFrequency = 0.01f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CaveThreshold = 0.45f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves", meta = (ClampMin = "0.0"))
    float CaveSecondOctaveMultiplier = 2.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CavePrimaryWeight = 0.7f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CaveSecondaryWeight = 0.3f;

    // ============================================================
    // 9A. v4.0 — 3D SDF Worm Caves
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves|WormCaves", meta = (ClampMin = "0.001"))
    float CaveWormFrequency = 0.012f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves|WormCaves", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CaveWormThreshold = 0.55f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves|WormCaves", meta = (ClampMin = "0.0"))
    float CaveCarveStrength = 12.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves|WormCaves", meta = (ClampMin = "0.0"))
    float CaveDepthFadeStart = 8.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves|WormCaves", meta = (ClampMin = "0.0"))
    float CaveDepthFadeEnd = 128.0f;

    // ============================================================
    // 9B. v4.0 — 3D SDF Canyons
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Canyons", meta = (ClampMin = "0.001"))
    float CanyonRidgeFrequency = 0.004f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Canyons", meta = (ClampMin = "0.0"))
    float CanyonDomainWarpStrength = 200.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Canyons", meta = (ClampMin = "0.001"))
    float CanyonDomainWarpFrequency = 0.003f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Canyons", meta = (ClampMin = "0.0"))
    float CanyonMaxDepthVoxels = 40.0f;

    // ============================================================
    // 9C. v4.0 — Lava Tubes
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves|LavaTubes")
    bool bEnableLavaTubes = true;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Caves|LavaTubes", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float LavaTubeZCompress = 0.3f;

    // ============================================================
    // 9D. Resource Veins (Batch 3 — mirrors FResourceNoiseProfile)
    //
    // These are editor-visible overrides for the noise parameters
    // that FResourceNoiseRegistry uses.  When bOverrideResourceNoise
    // is true, FResourceNoiseRegistry::GetProfile() reads from
    // these settings instead of the compiled defaults.
    //
    // All three systems — WorldForgeTerrainGenerator (layer 6),
    // Step08_SeedResources(), and ResourceSpawnSubsystem — share
    // these parameters via FResourceNoiseRegistry.
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources")
    bool bOverrideResourceNoise = false;

    // ── Metal ──────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Metal", meta = (ClampMin = "0.001", EditCondition = "bOverrideResourceNoise"))
    float MetalFrequency = 0.018f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Metal", meta = (ClampMin = "0.1", EditCondition = "bOverrideResourceNoise"))
    float MetalZScale = 0.6f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Metal", meta = (EditCondition = "bOverrideResourceNoise"))
    int32 MetalSeedOffset = 25000;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Metal", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bOverrideResourceNoise"))
    float MetalSpawnThreshold = 0.72f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Metal", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bOverrideResourceNoise"))
    float MetalSurveyThreshold = 0.45f;

    // ── Crystal ────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Crystal", meta = (ClampMin = "0.001", EditCondition = "bOverrideResourceNoise"))
    float CrystalFrequency = 0.022f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Crystal", meta = (ClampMin = "0.1", EditCondition = "bOverrideResourceNoise"))
    float CrystalZScale = 0.5f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Crystal", meta = (EditCondition = "bOverrideResourceNoise"))
    int32 CrystalSeedOffset = 29000;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Crystal", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bOverrideResourceNoise"))
    float CrystalSpawnThreshold = 0.78f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Crystal", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bOverrideResourceNoise"))
    float CrystalSurveyThreshold = 0.50f;

    // ── Fossil ─────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Fossil", meta = (ClampMin = "0.001", EditCondition = "bOverrideResourceNoise"))
    float FossilFrequency = 0.015f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Fossil", meta = (ClampMin = "0.1", EditCondition = "bOverrideResourceNoise"))
    float FossilZScale = 0.8f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Fossil", meta = (EditCondition = "bOverrideResourceNoise"))
    int32 FossilSeedOffset = 33000;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Fossil", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bOverrideResourceNoise"))
    float FossilSpawnThreshold = 0.75f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|Fossil", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bOverrideResourceNoise"))
    float FossilSurveyThreshold = 0.48f;

    // ============================================================
    // 9E. Shatter Stone Asset References
    //
    // Set these in Project Settings → WorldForge to point at your
    // NV3D Shatter Stone pack assets.  VoxelWorldManager reads them
    // at terrain-ready time and auto-configures ResourceSpawnSubsystem.
    // ============================================================

    /**
     * The Shatter Stone Data Table containing ore node configurations.
     * e.g. DT_OreNodeConfig_MetalOre from the NV3D pack.
     * Used for ALL resource categories (Metal, Crystal, Fossil) — the
     * spawn subsystem maps categories to rows via PickOreRowName().
     */
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|ShatterStone")
    TSoftObjectPtr<UDataTable> ShatterStoneDataTable;

    /**
     * The Blueprint class to spawn for ore nodes.
     * e.g. BP_MetalOreNode from the NV3D pack.
     * Must be a child of AActor with the Shatter Stone destruction system.
     */
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Resources|ShatterStone")
    TSoftClassPtr<AActor> ShatterStoneNodeClass;

    // ============================================================
    // 9F. Wild Harvest Asset References
    //
    // Set these in Project Settings → WorldForge to point at your
    // NV3D Wild Harvest pack assets.  FarmingSubsystem reads them
    // at game start and auto-configures WildHarvestAdapter.
    //
    // Discovered folder structure:
    //   /WildHarvest/_Core/Blueprints/       — BP_CropPatch_Master, STR_CropConfig, E_CropStates
    //   /WildHarvest/_Core/Blueprints/Interfaces/ — BPI_WH_Fertilize, BPI_WH_Harvesting,
    //                                               BPI_WH_Interact, BPI_WH_Watering
    //   /WildHarvest/WH_LowPoly/Blueprints/  — BP_CropPatch_LowPoly, DT_LowPoly, E_LowPoly
    // ============================================================

    /**
     * Wild Harvest Data Table — the LowPoly pack uses a single DT_LowPoly
     * containing all crop configs.  Additional sub-packs may have their own.
     * Path: /Game/WildHarvest/WH_LowPoly/Blueprints/DT_LowPoly
     */
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Farming|WildHarvest")
    TSoftObjectPtr<UDataTable> WildHarvest_DT_LowPoly;

    /**
     * The master crop patch Blueprint class.
     * Path: /Game/WildHarvest/_Core/Blueprints/BP_CropPatch_Master
     * Sub-packs have children: e.g. BP_CropPatch_LowPoly
     */
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Farming|WildHarvest")
    TSoftClassPtr<AActor> WildHarvestCropPatchMasterClass;

    /**
     * The LowPoly sub-pack crop patch Blueprint.
     * Path: /Game/WildHarvest/WH_LowPoly/Blueprints/BP_CropPatch_LowPoly
     * Use this as the default spawn class if only the LowPoly pack is installed.
     */
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Farming|WildHarvest")
    TSoftClassPtr<AActor> WildHarvestCropPatchLowPolyClass;

    /**
     * The pickup Blueprint class for harvested crop items.
     * Path: /Game/WildHarvest/_Core/Blueprints/BP_CropPickup_Master
     */
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Farming|WildHarvest")
    TSoftClassPtr<AActor> WildHarvestCropPickupMasterClass;

    // ============================================================
    // 10. Performance
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Performance")
    bool bUseFastTerrainSampling = false;

    // ============================================================
    // 11. Debug
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Debug")
    bool bLogGridConversions = false;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Debug")
    bool bLogTerrainGeneration = true;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Debug")
    bool bLogTerrainRetry = true;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Debug")
    bool bUseFallbackPlacedBlockVisuals = true;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Debug")
    bool bPreferSurfaceAnchoredPlacedBlocks = false;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Debug")
    bool bLogPlacedBlockVisuals = true;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Debug")
    float FallbackPlacedBlockVerticalNudge = 1.0f;

    // ============================================================
    // 12. Runtime
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Runtime")
    float TerrainRetryIntervalSeconds = 0.25f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Runtime")
    int32 MaxTerrainRetryCount = 40;

    // ============================================================
    // Extra classification helpers
    // ============================================================

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Classification", meta = (ClampMin = "0.0"))
    float CoastBandVoxels = 1.5f;

    // FIX: Was 15.0f — Perlin terrain routinely exceeds 15° even on gentle slopes,
    // so the OR check (NormElev >= DirtMin || slope >= DirtSlope) caught nearly all
    // non-Sand land as Dirt, leaving zero Grass tiles.  At 25° only meaningfully
    // sloped terrain triggers the Dirt fallback, giving the Grass band (NormElev
    // 0.15-0.38) room to actually fire.
    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Classification", meta = (ClampMin = "0.0"))
    float DirtSlopeThresholdDegrees = 25.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Classification", meta = (ClampMin = "0.0"))
    float RockSlopeThresholdDegrees = 12.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Classification", meta = (ClampMin = "0.0"))
    float CliffSlopeThresholdDegrees = 18.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain|Normalization", meta = (ClampMin = "0.0"))
    float NormalizedElevationMaxExtraVoxels = 24.0f;
};