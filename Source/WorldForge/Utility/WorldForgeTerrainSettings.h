#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
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

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel", meta = (ClampMin = "1.0"))
    float VoxelSize = 100.0f;

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