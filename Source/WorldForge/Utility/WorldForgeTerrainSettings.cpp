#include "WorldForgeTerrainSettings.h"

UWorldForgeTerrainSettings::UWorldForgeTerrainSettings()
{
    // Voxel
    ChunkWorldSize = 3200.0f;
    VoxelSize = 100.0f;
    TerrainSurfaceZOffset = 50.0f;
    bAddGlobalFoundation = true;
    GlobalFoundationDepthWorld = 5000.0f;

    // Perlin
    DefaultPerlinSettings.BaseFrequency = 0.005f;
    DefaultPerlinSettings.BaseAmplitude = 3.5f;
    DefaultPerlinSettings.OctaveCount = 4;
    DefaultPerlinSettings.Lacunarity = 2.0f;
    DefaultPerlinSettings.Persistence = 0.5f;
    DefaultPerlinSettings.MinHeight = 4;
    DefaultPerlinSettings.SeedOffset = 0;

    // Terrain bands
    // CRITICAL: SandMax MUST be below DirtMin for Grass to exist.
    // Classification order: Water → Sand(<=SandMax) → Snow → Rock → Dirt(>=DirtMin) → Grass(fallthrough)
    // Grass lives in the gap between SandMax and DirtMin for gentle slopes.
    //
    // With normalization fix (MaxElev = SeaLevel + CHR*0.6 + MtnMax*0.5):
    //   MinElev = -4, MaxElev = 121, Range = 125
    //   Sea level normalizes to ~0.112
    //   Band layout:  Water[0–0.11] Sand[0.11–0.15] Grass[0.15–0.38] Dirt[0.38–0.60] Rock[0.60+] Snow[0.88+]
    SandMaxElevationNormalized = 0.15f;       // was 0.35 — sand is just the beach/shoreline fringe
    DirtMinElevationNormalized = 0.52f;       // synced with header — Grass band needs 0.15–0.52
    RockMinElevationNormalized = 0.70f;       // was 0.75 — slight lower to catch more steep mid-terrain
    HighRockMinElevationNormalized = 0.60f;   // was 0.42 — was FAR too low, consuming the entire Dirt band
    SnowMinElevationNormalized = 0.88f;       // was 0.90 — slightly lower for broader snow caps

    // Base
    BaseHeight = 4000.0f;
    HeightMultiplier = 6000.0f;
    ContinentScale = 0.00005f;

    // Detail
    DetailScale = 0.002f;
    DetailAmplitude = 120.0f;
    DetailOctaves = 3;

    // Water
    SeaLevelVoxels = 10;

    // v2 terrain height system
    WaterThreshold = 0.48f;        // must match Step04 water biome threshold
    OceanDepthVoxels = 30;         // ocean floor depth below sea level
    MaxLandHeightVoxels = 150;     // peak height above sea level
    LandElevationExponent = 0.65f; // < 1.0 expands lowlands, compresses peaks

    // Macro
    ContinentFrequency = 0.002f;
    ContinentHeightRangeVoxels = 150;
    // ContinentExponent < 1.0 expands mid-range elevations above sea level.
    // At 1.0 (linear), tiles need to be >0.60 to escape Sand — that's 60%+ water+sand.
    // At 0.7, a tile at 0.52 produces ContinentHeight ~13 → Surface ~23 → normalized ~0.22 → Grass.
    // This is the single most impactful change for reducing ocean dominance.
    ContinentExponent = 0.7f;              // was 1.0
    BasinFrequency = 0.00055f;
    BasinThreshold = 0.82f;
    LakeBasinDepthVoxels = 14;

    // Rivers
    RiverFrequency = 0.001f;
    RiverThreshold = 0.95f;
    RiverCarveDepthVoxels = 6;
    RiverSharpness = 1.5f;

    // Caves
    bEnableCaves = true;
    CaveFrequency = 0.01f;
    CaveThreshold = 0.45f;
    CaveSecondOctaveMultiplier = 2.0f;
    CavePrimaryWeight = 0.7f;
    CaveSecondaryWeight = 0.3f;

    // v4.0 — 3D SDF worm caves
    CaveWormFrequency = 0.012f;
    CaveWormThreshold = 0.35f;
    CaveCarveStrength = 18.0f;
    CaveDepthFadeStart = 8.0f;
    CaveDepthFadeEnd = 128.0f;

    // v4.0 — 3D SDF canyons
    CanyonRidgeFrequency = 0.004f;
    CanyonDomainWarpStrength = 200.0f;
    CanyonDomainWarpFrequency = 0.003f;
    CanyonMaxDepthVoxels = 40.0f;

    // v4.0 — Lava tubes
    bEnableLavaTubes = true;
    LavaTubeZCompress = 0.3f;

    // Resource veins (Batch 3)
    bOverrideResourceNoise = false;
    MetalFrequency = 0.018f;
    MetalZScale = 0.6f;
    MetalSeedOffset = 25000;
    MetalSpawnThreshold = 0.72f;
    MetalSurveyThreshold = 0.45f;
    CrystalFrequency = 0.022f;
    CrystalZScale = 0.5f;
    CrystalSeedOffset = 29000;
    CrystalSpawnThreshold = 0.78f;
    CrystalSurveyThreshold = 0.50f;
    FossilFrequency = 0.015f;
    FossilZScale = 0.8f;
    FossilSeedOffset = 33000;
    FossilSpawnThreshold = 0.75f;
    FossilSurveyThreshold = 0.48f;

    // Performance
    bUseFastTerrainSampling = false;

    // Debug
    bLogGridConversions = false;
    bLogTerrainGeneration = true;
    bLogTerrainRetry = true;
    bUseFallbackPlacedBlockVisuals = true;
    bPreferSurfaceAnchoredPlacedBlocks = false;
    bLogPlacedBlockVisuals = true;
    FallbackPlacedBlockVerticalNudge = 1.0f;

    // Runtime
    TerrainRetryIntervalSeconds = 0.25f;
    MaxTerrainRetryCount = 40;

    // v2.1 mountain ridge detail
    MountainRidgeHeightScale = 60.0f;
    MountainRidgeWidthScale = 1.0f;
    MountainCragFrequency = 0.06f;
    MountainCragAmplitude = 6.0f;

    // Classification helpers
    CoastBandVoxels = 1.5f;                // was 2.0 — narrower coastal sand strip
    DirtSlopeThresholdDegrees = 25.0f;     // synced with header — 15° was too aggressive
    RockSlopeThresholdDegrees = 12.0f;
    CliffSlopeThresholdDegrees = 18.0f;
    NormalizedElevationMaxExtraVoxels = 24.0f;
}