// VoxelWorldManager.cpp
// ============================================================
// All terrain subsystem implementation in one place.
// Navigate with Ctrl+F on the section banners below.
//
// SECTIONS:
//   [INCLUDES]
//   [CORE LIFECYCLE]
//   [COORDINATE UTILITIES]
//   [NOISE MATH]
//   [TERRAIN CACHE]
//   [TERRAIN HEIGHTS]
//   [TERRAIN ANALYSIS]
//   [TERRAIN QUERIES]
//   [GENERATION ORCHESTRATOR]
//   [TERRAIN APPLICATION]
//   [PLACED BLOCK VISUALS]
// ============================================================

// ─── [INCLUDES] ─────────────────────────────────────────────

#include "VoxelWorldManager.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameplayTagContainer.h"
#include "VoxelWorld.h"
#include "VoxelWorldCreateInfo.h"

#if WITH_VOXEL_PLUGIN
#include "VoxelData/VoxelData.h"
#include "VoxelData/VoxelDataIncludes.h"
#include "VoxelValue.h"
#include "VoxelRender/IVoxelLODManager.h"
#endif

#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Utility/WorldForgeTerrainSettings.h"
#include "WorldForgeTerrainGenerator.h"
#include "Data/WorldForgeTags.h"
#include "Utility/WorldForgeTerrainMath.h"
#include "Mining/ResourceSpawnSubsystem.h"
#include "Farming/WildHarvestAdapter.h"  // ════ Wild Harvest Integration ════

// ─── [BIOME TAG HELPERS] ─────────────────────────────────────
// Cached-by-call tag accessors. Kept in an anonymous namespace
// so they stay translation-unit-local without leaking linkage.

namespace
{
    static FGameplayTag GetBiomeCoastalTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")), false);
    }

    static FGameplayTag GetBiomeLavaTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);
    }

    static FGameplayTag GetBiomeOceanTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Ocean")), false);
    }

    static FGameplayTag GetBiomeSeaTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Sea")), false);
    }

    static FGameplayTag GetBiomeLakeTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lake")), false);
    }

    static FGameplayTag GetBiomeWaterTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Water")), false);
    }

    static FGameplayTag GetBiomeDeepOceanTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.DeepOcean")), false);
    }
}

// ─── [CORE LIFECYCLE] ────────────────────────────────────────

void UVoxelWorldManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    const UWorldForgeTerrainSettings* Settings = GetDefault<UWorldForgeTerrainSettings>();
    if (Settings)
    {
        ChunkWorldSize = Settings->ChunkWorldSize;
        VoxelSize = Settings->VoxelSize;
        TerrainSurfaceZOffset = Settings->TerrainSurfaceZOffset;
        bAddGlobalFoundation = Settings->bAddGlobalFoundation;
        GlobalFoundationDepthWorld = Settings->GlobalFoundationDepthWorld;

        DefaultPerlinSettings = Settings->DefaultPerlinSettings;

        SandMaxElevationNormalized = Settings->SandMaxElevationNormalized;
        DirtMinElevationNormalized = Settings->DirtMinElevationNormalized;
        RockMinElevationNormalized = Settings->RockMinElevationNormalized;
        HighRockMinElevationNormalized = Settings->HighRockMinElevationNormalized;
        SnowMinElevationNormalized = Settings->SnowMinElevationNormalized;

        CoastBandVoxels = Settings->CoastBandVoxels;
        DirtSlopeThresholdDegrees = Settings->DirtSlopeThresholdDegrees;
        RockSlopeThresholdDegrees = Settings->RockSlopeThresholdDegrees;
        CliffSlopeThresholdDegrees = Settings->CliffSlopeThresholdDegrees;
        NormalizedElevationMaxExtraVoxels = Settings->NormalizedElevationMaxExtraVoxels;

        SeaLevelVoxels = Settings->SeaLevelVoxels;

        // v2 terrain height fields
        WaterThreshold = Settings->WaterThreshold;
        OceanDepthVoxels = Settings->OceanDepthVoxels;
        MaxLandHeightVoxels = Settings->MaxLandHeightVoxels;
        LandElevationExponent = Settings->LandElevationExponent;

        ContinentFrequency = Settings->ContinentFrequency;
        ContinentHeightRangeVoxels = Settings->ContinentHeightRangeVoxels;
        ContinentExponent = Settings->ContinentExponent;

        BasinFrequency = Settings->BasinFrequency;
        BasinThreshold = Settings->BasinThreshold;
        LakeBasinDepthVoxels = Settings->LakeBasinDepthVoxels;

        RiverFrequency = Settings->RiverFrequency;
        RiverThreshold = Settings->RiverThreshold;
        RiverCarveDepthVoxels = Settings->RiverCarveDepthVoxels;
        RiverSharpness = Settings->RiverSharpness;

        MountainRangeCount = Settings->MountainRangeCount;
        MountainMinLengthTiles = Settings->MountainMinLengthTiles;
        MountainMaxLengthTiles = Settings->MountainMaxLengthTiles;
        MountainMinWidthVoxels = Settings->MountainMinWidthVoxels;
        MountainMaxWidthVoxels = Settings->MountainMaxWidthVoxels;
        MountainMinHeightVoxels = Settings->MountainMinHeightVoxels;
        MountainMaxHeightVoxels = Settings->MountainMaxHeightVoxels;
        MountainFalloffPower = Settings->MountainFalloffPower;

        // v2.1 mountain ridge detail settings
        MountainRidgeHeightScale = Settings->MountainRidgeHeightScale;
        MountainRidgeWidthScale = Settings->MountainRidgeWidthScale;
        MountainCragFrequency = Settings->MountainCragFrequency;
        MountainCragAmplitude = Settings->MountainCragAmplitude;

        bUseFastTerrainSampling = Settings->bUseFastTerrainSampling;

        bLogGridConversions = Settings->bLogGridConversions;
        bLogTerrainGeneration = Settings->bLogTerrainGeneration;
        bLogTerrainRetry = Settings->bLogTerrainRetry;
        bUseFallbackPlacedBlockVisuals = Settings->bUseFallbackPlacedBlockVisuals;
        bPreferSurfaceAnchoredPlacedBlocks = Settings->bPreferSurfaceAnchoredPlacedBlocks;
        bLogPlacedBlockVisuals = Settings->bLogPlacedBlockVisuals;
        FallbackPlacedBlockVerticalNudge = Settings->FallbackPlacedBlockVerticalNudge;

        TerrainRetryIntervalSeconds = Settings->TerrainRetryIntervalSeconds;
        MaxTerrainRetryCount = Settings->MaxTerrainRetryCount;

        BaseHeight = Settings->BaseHeight;
        HeightMultiplier = Settings->HeightMultiplier;
        ContinentScale = Settings->ContinentScale;

        DetailScale = Settings->DetailScale;
        DetailAmplitude = Settings->DetailAmplitude;
        DetailOctaves = Settings->DetailOctaves;

        bEnableCaves = Settings->bEnableCaves;
        CaveFrequency = Settings->CaveFrequency;
        CaveThreshold = Settings->CaveThreshold;
        CaveSecondOctaveMultiplier = Settings->CaveSecondOctaveMultiplier;
        CavePrimaryWeight = Settings->CavePrimaryWeight;
        CaveSecondaryWeight = Settings->CaveSecondaryWeight;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[VoxelWorldManager] DIAGNOSTIC: DirtMin=%.3f DirtSlope=%.1f SandMax=%.3f HighRock=%.3f CoastBand=%.1f"),
        DirtMinElevationNormalized,
        DirtSlopeThresholdDegrees,
        SandMaxElevationNormalized,
        HighRockMinElevationNormalized,
        CoastBandVoxels);

    ClearTerrainSnapshot();

    // ── BUILD STAMP: If this line does NOT appear in the log, the editor
    // is still running a stale DLL.  Close UE, Rebuild in VS, relaunch.
    // v5.0: Hybrid Chunking Modification Path
    UE_LOG(LogTemp, Log,
        TEXT("[VoxelWorldManager] ===== BUILD v5.0 TIMESTAMP: %s %s ====="),
        TEXT(__DATE__), TEXT(__TIME__));

    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Initialized."));
}

void UVoxelWorldManager::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TerrainRetryTimerHandle);
        World->GetTimerManager().ClearTimer(BatchedTerrainRebuildTimerHandle);
    }

    ClearPendingTerrainRequest();
    ClearTerrainSnapshot();
    ClearTerrainCache();

    // Clear pipeline source data (not cleared by ClearTerrainCache)
    CachedMountainRanges.Reset();
    CachedVolcanoes.Reset();

    ClearPlacedBlockVisuals();

    Super::Deinitialize();
}

bool UVoxelWorldManager::IsReady()
{
    if (IsValid(VoxelWorldActor))
    {
        return true;
    }

    VoxelWorldActor = nullptr;
    return FindAndCacheVoxelWorld();
}

bool UVoxelWorldManager::IsTerrainReadyForGameplay() const
{
    if (!HasCachedTerrainData())
    {
        return false;
    }

    if (!HasTerrainSnapshot())
    {
        return false;
    }

    if (!IsValid(VoxelWorldActor))
    {
        return false;
    }

    return VoxelWorldActor->IsCreated();
}

bool UVoxelWorldManager::IsTerrainRuntimeStable() const
{
    return IsTerrainReadyForGameplay() && !HasPendingTerrainRequest();
}

bool UVoxelWorldManager::FindAndCacheVoxelWorld()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] FindAndCacheVoxelWorld failed: GetWorld() returned null."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Searching for VoxelWorld..."));

    for (TActorIterator<AVoxelWorld> It(World); It; ++It)
    {
        VoxelWorldActor = *It;
        UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Found: %s"), *It->GetName());
        return true;
    }

    VoxelWorldActor = nullptr;

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[VoxelWorldManager] No VoxelWorld actor found. Fix: Place a VoxelWorld actor in the current level.")
    );
    return false;
}

bool UVoxelWorldManager::IsVoxelRuntimeReady() const
{
    if (!IsValid(VoxelWorldActor))
    {
        return false;
    }

    return VoxelWorldActor->IsCreated();
}

bool UVoxelWorldManager::HasPendingTerrainRequest() const
{
    return PendingWorldTiles.Num() > 0 && PendingGridWidth > 0 && PendingGridHeight > 0;
}

// ── FIX: Use the actual VoxelWorld actor's VoxelSize when available.
// The Voxel Plugin passes coordinates to GetValueImpl in voxel-space where
// each unit = one voxel, and the voxel size is set on the AVoxelWorld actor.
// If ChunkSizeVoxels is computed from a different VoxelSize than the actor's,
// the generator's tile lookups will be at the wrong scale, causing the 3D
// terrain shape to diverge from the 2D overworld map.
int32 UVoxelWorldManager::GetChunkSizeInVoxels() const
{
    const float ActualVoxelSize = GetRenderVoxelSize();
    return FMath::Max(1, FMath::RoundToInt(ChunkWorldSize / ActualVoxelSize));
}

int32 UVoxelWorldManager::ComputeChunkBaseDepthVoxels(const FVoxelBiomeProfile& Profile) const
{
    const float SafeVoxelSize = (VoxelSize > KINDA_SMALL_NUMBER) ? VoxelSize : 100.0f;
    const int32 DerivedDepth = FMath::RoundToInt((Profile.NoiseAmplitude * 2.50f) / SafeVoxelSize);
    return FMath::Max(24, DerivedDepth);
}

int32 UVoxelWorldManager::GetEffectiveBlockHeightStep() const
{
    return FMath::Max(1, CurrentBlockHeightStep);
}

float UVoxelWorldManager::GetTerrainCellWorldSize() const
{
    return ChunkWorldSize;
}

// ─── FIX 2: Read actual VoxelWorld.VoxelSize when available; fall back to the
// configured VoxelSize setting (not the hard-coded 1.0 that was here before).
// A 1.0 fallback caused every world-space height to be wildly mis-scaled when
// the actor hadn't been found yet.
float UVoxelWorldManager::GetRenderVoxelSize() const
{
    if (const AVoxelWorld* VW = Cast<AVoxelWorld>(VoxelWorldActor))
    {
        return FMath::Max(KINDA_SMALL_NUMBER, VW->VoxelSize);
    }

    return FMath::Max(KINDA_SMALL_NUMBER, VoxelSize);
}

bool UVoxelWorldManager::ValidateSmoothSDF(const FVoxelBiomeProfile& Profile) const
{
    return Profile.bSmoothSDF;
}

void UVoxelWorldManager::BuildTerrainSnapshot(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight)
{
    ClearTerrainSnapshot();

    TerrainSnapshot.GridWidth = GridWidth;
    TerrainSnapshot.GridHeight = GridHeight;
    TerrainSnapshot.ChunkSizeVoxels = GetChunkSizeInVoxels();
    // ── FIX: Store the ACTUAL VoxelWorld actor's VoxelSize so the generator's
    // coordinate conversion matches the Voxel Plugin's coordinate space.
    TerrainSnapshot.VoxelSize = GetRenderVoxelSize();

    TerrainSnapshot.TerrainSurfaceZOffset = TerrainSurfaceZOffset;

    TerrainSnapshot.BaseHeight = BaseHeight;
    TerrainSnapshot.HeightMultiplier = HeightMultiplier;
    TerrainSnapshot.ContinentScale = ContinentScale;

    TerrainSnapshot.DetailScale = DetailScale;
    TerrainSnapshot.DetailAmplitude = DetailAmplitude;
    TerrainSnapshot.DetailOctaves = DetailOctaves;

    TerrainSnapshot.bEnableCaves = bEnableCaves;
    TerrainSnapshot.CaveFrequency = CaveFrequency;
    TerrainSnapshot.CaveThreshold = CaveThreshold;
    TerrainSnapshot.CaveSecondOctaveMultiplier = CaveSecondOctaveMultiplier;
    TerrainSnapshot.CavePrimaryWeight = CavePrimaryWeight;
    TerrainSnapshot.CaveSecondaryWeight = CaveSecondaryWeight;

    // v4.0 — 3D SDF worm caves
    TerrainSnapshot.CaveWormFrequency = CaveWormFrequency;
    TerrainSnapshot.CaveWormThreshold = CaveWormThreshold;
    TerrainSnapshot.CaveCarveStrength = CaveCarveStrength;
    TerrainSnapshot.CaveDepthFadeStart = CaveDepthFadeStart;
    TerrainSnapshot.CaveDepthFadeEnd = CaveDepthFadeEnd;

    // v4.0 — 3D SDF canyons
    TerrainSnapshot.CanyonRidgeFrequency = CanyonRidgeFrequency;
    TerrainSnapshot.CanyonDomainWarpStrength = CanyonDomainWarpStrength;
    TerrainSnapshot.CanyonDomainWarpFrequency = CanyonDomainWarpFrequency;
    TerrainSnapshot.CanyonMaxDepthVoxels = CanyonMaxDepthVoxels;

    // v4.0 — Lava tubes
    TerrainSnapshot.bEnableLavaTubes = bEnableLavaTubes;
    TerrainSnapshot.LavaTubeZCompress = LavaTubeZCompress;

    TerrainSnapshot.SeaLevelVoxels = SeaLevelVoxels;

    // v2 terrain height fields
    TerrainSnapshot.WaterThreshold = WaterThreshold;
    TerrainSnapshot.OceanDepthVoxels = OceanDepthVoxels;
    TerrainSnapshot.MaxLandHeightVoxels = MaxLandHeightVoxels;
    TerrainSnapshot.LandElevationExponent = LandElevationExponent;

    TerrainSnapshot.bAddGlobalFoundation = bAddGlobalFoundation;
    TerrainSnapshot.GlobalFoundationDepthWorld = GlobalFoundationDepthWorld;

    TerrainSnapshot.ContinentFrequency = ContinentFrequency;
    TerrainSnapshot.ContinentHeightRangeVoxels = ContinentHeightRangeVoxels;
    TerrainSnapshot.ContinentExponent = ContinentExponent;

    TerrainSnapshot.BasinFrequency = BasinFrequency;
    TerrainSnapshot.BasinThreshold = BasinThreshold;
    TerrainSnapshot.LakeBasinDepthVoxels = LakeBasinDepthVoxels;

    TerrainSnapshot.RiverFrequency = RiverFrequency;
    TerrainSnapshot.RiverThreshold = RiverThreshold;
    TerrainSnapshot.RiverCarveDepthVoxels = RiverCarveDepthVoxels;
    TerrainSnapshot.RiverSharpness = RiverSharpness;

    TerrainSnapshot.MountainRangeCount = MountainRangeCount;
    TerrainSnapshot.MountainMinLengthTiles = MountainMinLengthTiles;
    TerrainSnapshot.MountainMaxLengthTiles = MountainMaxLengthTiles;
    TerrainSnapshot.MountainMinWidthVoxels = MountainMinWidthVoxels;
    TerrainSnapshot.MountainMaxWidthVoxels = MountainMaxWidthVoxels;
    TerrainSnapshot.MountainMinHeightVoxels = MountainMinHeightVoxels;
    TerrainSnapshot.MountainMaxHeightVoxels = MountainMaxHeightVoxels;
    TerrainSnapshot.MountainFalloffPower = MountainFalloffPower;

    // v2.1 mountain ridge detail
    TerrainSnapshot.MountainRidgeHeightScale = MountainRidgeHeightScale;
    TerrainSnapshot.MountainRidgeWidthScale = MountainRidgeWidthScale;
    TerrainSnapshot.MountainCragFrequency = MountainCragFrequency;
    TerrainSnapshot.MountainCragAmplitude = MountainCragAmplitude;

    TerrainSnapshot.SandMaxElevationNormalized = SandMaxElevationNormalized;
    TerrainSnapshot.DirtMinElevationNormalized = DirtMinElevationNormalized;
    TerrainSnapshot.RockMinElevationNormalized = RockMinElevationNormalized;
    TerrainSnapshot.HighRockMinElevationNormalized = HighRockMinElevationNormalized;
    TerrainSnapshot.SnowMinElevationNormalized = SnowMinElevationNormalized;

    TerrainSnapshot.CoastBandVoxels = CoastBandVoxels;
    TerrainSnapshot.DirtSlopeThresholdDegrees = DirtSlopeThresholdDegrees;
    TerrainSnapshot.RockSlopeThresholdDegrees = RockSlopeThresholdDegrees;
    TerrainSnapshot.CliffSlopeThresholdDegrees = CliffSlopeThresholdDegrees;
    TerrainSnapshot.NormalizedElevationMaxExtraVoxels = NormalizedElevationMaxExtraVoxels;

    TerrainSnapshot.bUseFastTerrainSampling = bUseFastTerrainSampling;

    // ── FIX (Issue 1): Pipe Step03 mountain ranges into the snapshot
    // so the voxel generator uses real tectonic data.
    TerrainSnapshot.PipelineMountainRanges = CachedMountainRanges;
    TerrainSnapshot.PipelineVolcanoes = CachedVolcanoes;

    TerrainSnapshot.WorldTiles = WorldTiles;
    TerrainSnapshot.CachedTileElevations = CachedTileElevations;
    TerrainSnapshot.CachedVoxelProfiles = CachedVoxelProfiles;
    TerrainSnapshot.CachedBiomeTags = CachedBiomeTags;
    TerrainSnapshot.CachedSurfaceHeightsVoxels = CachedSurfaceHeightsVoxels;

    // ── v5.0: Initialize the modification map with grid parameters
    // so it can convert voxel coords → chunk keys.  This does NOT clear
    // existing brushes — modifications persist across snapshot rebuilds.
    TerrainSnapshot.InitializeModifications();

    if (bLogTerrainGeneration)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Built terrain snapshot. Grid=%dx%d Tiles=%d ChunkSizeVoxels=%d VoxelSize=%.2f"),
            TerrainSnapshot.GridWidth,
            TerrainSnapshot.GridHeight,
            TerrainSnapshot.WorldTiles.Num(),
            TerrainSnapshot.ChunkSizeVoxels,
            TerrainSnapshot.VoxelSize
        );
    }
}

void UVoxelWorldManager::ClearTerrainSnapshot()
{
    TerrainSnapshot = FWorldForgeTerrainSnapshot();
}

// ── FIX: Single helper that builds a complete local snapshot from member
// variables.  Every function that previously constructed its own partial
// FWorldForgeTerrainSnapshot should call this instead, eliminating the
// class of bugs where GridWidth, GridHeight, MountainMaxHeightVoxels, or
// other fields were accidentally left at zero-default.
FWorldForgeTerrainSnapshot UVoxelWorldManager::MakeLocalTerrainSnapshot() const
{
    FWorldForgeTerrainSnapshot S;

    // Grid dimensions — CRITICAL: these were missing from all local snapshots,
    // causing SampleMountainHeight to squeeze all ranges into a 1×1 tile area.
    S.GridWidth = CachedTerrainGridWidth;
    S.GridHeight = CachedTerrainGridHeight;
    S.ChunkSizeVoxels = GetChunkSizeInVoxels();
    // ── FIX: Use actual actor VoxelSize so classification agrees with generator.
    S.VoxelSize = GetRenderVoxelSize();

    // Sea / continent
    S.SeaLevelVoxels = SeaLevelVoxels;

    // v2 terrain height fields
    S.WaterThreshold = WaterThreshold;
    S.OceanDepthVoxels = OceanDepthVoxels;
    S.MaxLandHeightVoxels = MaxLandHeightVoxels;
    S.LandElevationExponent = LandElevationExponent;

    S.ContinentHeightRangeVoxels = ContinentHeightRangeVoxels;
    S.ContinentExponent = ContinentExponent;
    S.ContinentFrequency = ContinentFrequency;

    // Basins
    S.BasinFrequency = BasinFrequency;
    S.BasinThreshold = BasinThreshold;
    S.LakeBasinDepthVoxels = LakeBasinDepthVoxels;

    // Rivers
    S.RiverFrequency = RiverFrequency;
    S.RiverThreshold = RiverThreshold;
    S.RiverCarveDepthVoxels = RiverCarveDepthVoxels;
    S.RiverSharpness = RiverSharpness;

    // Mountains — CRITICAL: MountainMaxHeightVoxels was missing from the
    // normalization snapshot, shrinking the denominator and causing the cache
    // to disagree with the generator's classification.
    S.MountainRangeCount = MountainRangeCount;
    S.MountainMinLengthTiles = MountainMinLengthTiles;
    S.MountainMaxLengthTiles = MountainMaxLengthTiles;
    S.MountainMinWidthVoxels = MountainMinWidthVoxels;
    S.MountainMaxWidthVoxels = MountainMaxWidthVoxels;
    S.MountainMinHeightVoxels = MountainMinHeightVoxels;
    S.MountainMaxHeightVoxels = MountainMaxHeightVoxels;
    S.MountainFalloffPower = MountainFalloffPower;

    // v2.1 mountain ridge detail
    S.MountainRidgeHeightScale = MountainRidgeHeightScale;
    S.MountainRidgeWidthScale = MountainRidgeWidthScale;
    S.MountainCragFrequency = MountainCragFrequency;
    S.MountainCragAmplitude = MountainCragAmplitude;

    // Classification thresholds
    S.SandMaxElevationNormalized = SandMaxElevationNormalized;
    S.DirtMinElevationNormalized = DirtMinElevationNormalized;
    S.RockMinElevationNormalized = RockMinElevationNormalized;
    S.HighRockMinElevationNormalized = HighRockMinElevationNormalized;
    S.SnowMinElevationNormalized = SnowMinElevationNormalized;

    S.CoastBandVoxels = CoastBandVoxels;
    S.DirtSlopeThresholdDegrees = DirtSlopeThresholdDegrees;
    S.RockSlopeThresholdDegrees = RockSlopeThresholdDegrees;
    S.CliffSlopeThresholdDegrees = CliffSlopeThresholdDegrees;
    S.NormalizedElevationMaxExtraVoxels = NormalizedElevationMaxExtraVoxels;

    // Detail / caves (needed if snapshot is passed to the generator)
    S.DetailScale = DetailScale;
    S.DetailAmplitude = DetailAmplitude;
    S.DetailOctaves = DetailOctaves;

    S.bEnableCaves = bEnableCaves;
    S.CaveFrequency = CaveFrequency;
    S.CaveThreshold = CaveThreshold;
    S.CaveSecondOctaveMultiplier = CaveSecondOctaveMultiplier;
    S.CavePrimaryWeight = CavePrimaryWeight;
    S.CaveSecondaryWeight = CaveSecondaryWeight;

    // v4.0 — 3D SDF worm caves
    S.CaveWormFrequency = CaveWormFrequency;
    S.CaveWormThreshold = CaveWormThreshold;
    S.CaveCarveStrength = CaveCarveStrength;
    S.CaveDepthFadeStart = CaveDepthFadeStart;
    S.CaveDepthFadeEnd = CaveDepthFadeEnd;

    // v4.0 — 3D SDF canyons
    S.CanyonRidgeFrequency = CanyonRidgeFrequency;
    S.CanyonDomainWarpStrength = CanyonDomainWarpStrength;
    S.CanyonDomainWarpFrequency = CanyonDomainWarpFrequency;
    S.CanyonMaxDepthVoxels = CanyonMaxDepthVoxels;

    // v4.0 — Lava tubes
    S.bEnableLavaTubes = bEnableLavaTubes;
    S.LavaTubeZCompress = LavaTubeZCompress;

    // Misc
    S.BaseHeight = BaseHeight;
    S.HeightMultiplier = HeightMultiplier;
    S.ContinentScale = ContinentScale;
    S.bAddGlobalFoundation = bAddGlobalFoundation;
    S.GlobalFoundationDepthWorld = GlobalFoundationDepthWorld;
    S.TerrainSurfaceZOffset = TerrainSurfaceZOffset;
    S.bUseFastTerrainSampling = bUseFastTerrainSampling;

    // ── FIX (Issue 1): Include pipeline mountain ranges in local snapshots
    // so cache/classification functions agree with the generator.
    S.PipelineMountainRanges = CachedMountainRanges;
    S.PipelineVolcanoes = CachedVolcanoes;

    // ── v5.0: Copy the modification map from the main snapshot.
    // Single source of truth for all terrain edits.
    S.Modifications = TerrainSnapshot.Modifications;

    return S;
}

bool UVoxelWorldManager::HasTerrainSnapshot() const
{
    return TerrainSnapshot.IsValid();
}

void UVoxelWorldManager::ApplyBiomeProfileToChunk(FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile)
{
    if (!ValidateSmoothSDF(Profile))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk rejected: biome profile is not Smooth SDF.")
        );
        return;
    }

    if (!IsReady())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk failed: VoxelWorld still not available.")
        );
        return;
    }

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk failed: cached actor is not AVoxelWorld.")
        );
        return;
    }

    if (!VoxelWorld->IsCreated())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk skipped: VoxelWorld runtime is not created yet.")
        );
        return;
    }

    const int32 Index = TerrainIndex(ClampTerrainCellToBounds(ChunkCoord));
    const float TileElevation = CachedTileElevations.IsValidIndex(Index) ? CachedTileElevations[Index] : 0.5f;

    const bool bNativeApplied = ApplyBiomeChunkNative(VoxelWorld, ChunkCoord, Profile, TileElevation);
    if (!bNativeApplied)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk failed during native application.")
        );
        return;
    }

    OnBiomeChunkRequested(ChunkCoord, Profile);
}

void UVoxelWorldManager::NotifyVoxelDig(FVector WorldPosition, int32 AgentID)
{
    UE_LOG(
        LogTemp,
        Verbose,
        TEXT("[VoxelWorldManager] NotifyVoxelDig WorldPosition=(%.1f, %.1f, %.1f) AgentID=%d"),
        WorldPosition.X,
        WorldPosition.Y,
        WorldPosition.Z,
        AgentID
    );

    OnVoxelDigged(WorldPosition);
}

void UVoxelWorldManager::SetPipelineMountainRanges(const TArray<FMountainRange>& InRanges)
{
    CachedMountainRanges = InRanges;

    if (bLogTerrainGeneration)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[VoxelWorldManager] Cached %d pipeline mountain ranges for terrain generation."),
            CachedMountainRanges.Num());
    }
}

void UVoxelWorldManager::SetPipelineVolcanoes(const TArray<FVolcanoData>& InVolcanoes)
{
    CachedVolcanoes = InVolcanoes;

    if (bLogTerrainGeneration)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[VoxelWorldManager] Cached %d pipeline volcanoes for terrain generation."),
            CachedVolcanoes.Num());
    }
}

// ── FIX (Resource Discovery): Patch resource data into pending/cached tiles.
// Step05 copies tiles into PendingWorldTiles BEFORE Step08 writes VeinConfig.
// This method lets the pipeline push the updated resource fields after Step08.
void UVoxelWorldManager::SetPipelineResourceData(const TArray<FWorldTileData>& UpdatedTiles)
{
    const int32 TileCount = UpdatedTiles.Num();

    // Patch PendingWorldTiles (used if terrain gen is still deferred via retry)
    if (PendingWorldTiles.Num() == TileCount)
    {
        for (int32 i = 0; i < TileCount; ++i)
        {
            PendingWorldTiles[i].ResourceTags = UpdatedTiles[i].ResourceTags;
            PendingWorldTiles[i].ResourceSurvey = UpdatedTiles[i].ResourceSurvey;
            PendingWorldTiles[i].VeinConfig = UpdatedTiles[i].VeinConfig;
        }

        UE_LOG(LogTemp, Log,
            TEXT("[VoxelWorldManager] Patched resource data into %d PendingWorldTiles."),
            TileCount);
    }

    // Patch TerrainSnapshot.WorldTiles (used if terrain already built)
    if (TerrainSnapshot.WorldTiles.Num() == TileCount)
    {
        for (int32 i = 0; i < TileCount; ++i)
        {
            TerrainSnapshot.WorldTiles[i].ResourceTags = UpdatedTiles[i].ResourceTags;
            TerrainSnapshot.WorldTiles[i].ResourceSurvey = UpdatedTiles[i].ResourceSurvey;
            TerrainSnapshot.WorldTiles[i].VeinConfig = UpdatedTiles[i].VeinConfig;
        }

        UE_LOG(LogTemp, Log,
            TEXT("[VoxelWorldManager] Patched resource data into %d TerrainSnapshot tiles."),
            TileCount);
    }

    if (PendingWorldTiles.Num() != TileCount && TerrainSnapshot.WorldTiles.Num() != TileCount)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[VoxelWorldManager] SetPipelineResourceData: tile count mismatch. "
                "Updated=%d Pending=%d Snapshot=%d — resource data NOT patched."),
            TileCount, PendingWorldTiles.Num(), TerrainSnapshot.WorldTiles.Num());
    }
}

// ─── FIX 1: Read the VoxelWorld actor's actual world location so all
// coordinate conversions (GridToWorld, TerrainCellToWorld, WorldToGrid3D, etc.)
// are correct when the actor is not placed at the scene origin.
FVector UVoxelWorldManager::GetVoxelWorldOrigin() const
{
    if (IsValid(VoxelWorldActor))
    {
        return VoxelWorldActor->GetActorLocation();
    }

    return FVector::ZeroVector;
}

// ─── [COORDINATE UTILITIES] ──────────────────────────────────

FVector UVoxelWorldManager::GridToWorld(FIntPoint GridCell, int32 ZLevel) const
{
    const FVector Origin = GetVoxelWorldOrigin();
    const float RVS = GetRenderVoxelSize();

    const FVector Result(
        Origin.X + (GridCell.X * RVS),
        Origin.Y + (GridCell.Y * RVS),
        Origin.Z + (ZLevel * RVS)
    );

    if (bLogGridConversions)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] GridToWorld Grid=(%d,%d,%d) Origin=(%.0f, %.0f, %.0f) -> World=(%.0f, %.0f, %.0f)"),
            GridCell.X,
            GridCell.Y,
            ZLevel,
            Origin.X,
            Origin.Y,
            Origin.Z,
            Result.X,
            Result.Y,
            Result.Z
        );
    }

    return Result;
}

FVector UVoxelWorldManager::TerrainCellToWorld(
    FIntPoint GridCell,
    float WorldZ,
    bool bCenterInCell) const
{
    const float CellSize = GetTerrainCellWorldSize();
    const FVector Origin = GetVoxelWorldOrigin();

    const float HalfGridWidth = static_cast<float>(CachedTerrainGridWidth) * 0.5f;
    const float HalfGridHeight = static_cast<float>(CachedTerrainGridHeight) * 0.5f;

    float X = (static_cast<float>(GridCell.X) - HalfGridWidth) * CellSize;
    float Y = (static_cast<float>(GridCell.Y) - HalfGridHeight) * CellSize;

    if (bCenterInCell)
    {
        X += CellSize * 0.5f;
        Y += CellSize * 0.5f;
    }

    return FVector(
        Origin.X + X,
        Origin.Y + Y,
        WorldZ
    );
}

FIntVector UVoxelWorldManager::WorldToGrid3D(FVector WorldPosition) const
{
    const float RVS = FMath::Max(KINDA_SMALL_NUMBER, GetRenderVoxelSize());
    const FVector Origin = GetVoxelWorldOrigin();
    const FVector LocalPosition = WorldPosition - Origin;

    const int32 GridX = FMath::FloorToInt((LocalPosition.X + (RVS * 0.5f)) / RVS);
    const int32 GridY = FMath::FloorToInt((LocalPosition.Y + (RVS * 0.5f)) / RVS);
    const int32 GridZ = FMath::FloorToInt((LocalPosition.Z + (RVS * 0.5f)) / RVS);

    const FIntVector Result(GridX, GridY, GridZ);

    if (bLogGridConversions)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] WorldToGrid3D World=(%.1f, %.1f, %.1f) -> Grid=(%d,%d,%d)"),
            WorldPosition.X,
            WorldPosition.Y,
            WorldPosition.Z,
            Result.X,
            Result.Y,
            Result.Z
        );
    }

    return Result;
}

FIntPoint UVoxelWorldManager::WorldToGrid(FVector WorldPosition) const
{
    const FIntVector Grid3D = WorldToGrid3D(WorldPosition);
    return FIntPoint(Grid3D.X, Grid3D.Y);
}

FIntPoint UVoxelWorldManager::WorldToTerrainCell(FVector WorldPosition) const
{
    const FVector Origin = GetVoxelWorldOrigin();
    const float CellSize = GetTerrainCellWorldSize();
    const FVector LocalPosition = WorldPosition - Origin;

    const float HalfGridWidth = static_cast<float>(CachedTerrainGridWidth) * 0.5f;
    const float HalfGridHeight = static_cast<float>(CachedTerrainGridHeight) * 0.5f;

    const int32 CellX = FMath::FloorToInt(LocalPosition.X / CellSize + HalfGridWidth);
    const int32 CellY = FMath::FloorToInt(LocalPosition.Y / CellSize + HalfGridHeight);

    const FIntPoint Result(CellX, CellY);

    if (bLogGridConversions)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] WorldToTerrainCell World=(%.1f, %.1f, %.1f) CellSize=%.1f HalfGrid=(%.1f, %.1f) -> Cell=(%d,%d)"),
            WorldPosition.X,
            WorldPosition.Y,
            WorldPosition.Z,
            CellSize,
            HalfGridWidth,
            HalfGridHeight,
            Result.X,
            Result.Y
        );
    }

    return Result;
}

FIntPoint UVoxelWorldManager::VoxelCellToChunkCell(FIntPoint VoxelCell) const
{
    const int32 ChunkSizeVoxels = GetChunkSizeInVoxels();
    if (ChunkSizeVoxels <= 0)
    {
        return VoxelCell;
    }

    const float HalfGridX = static_cast<float>(CachedTerrainGridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(CachedTerrainGridHeight) * 0.5f;

    const int32 ChunkX = FMath::FloorToInt(
        (static_cast<float>(VoxelCell.X) / static_cast<float>(ChunkSizeVoxels)) + HalfGridX
    );

    const int32 ChunkY = FMath::FloorToInt(
        (static_cast<float>(VoxelCell.Y) / static_cast<float>(ChunkSizeVoxels)) + HalfGridY
    );

    return FIntPoint(ChunkX, ChunkY);
}

// ─── [NOISE MATH] ────────────────────────────────────────────

const int32 UVoxelWorldManager::PermutationTable[512] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

float UVoxelWorldManager::Fade(float T)
{
    return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
}

float UVoxelWorldManager::Lerp(float T, float A, float B)
{
    return A + T * (B - A);
}

float UVoxelWorldManager::Grad(int32 Hash, float X, float Y)
{
    const int32 H = Hash & 3;
    const float U = (H < 2) ? X : -X;
    const float V = (H == 0 || H == 3) ? Y : -Y;
    return U + V;
}

float UVoxelWorldManager::PerlinNoise2D(float X, float Y)
{
    const int32 Xi = static_cast<int32>(FMath::FloorToFloat(X)) & 255;
    const int32 Yi = static_cast<int32>(FMath::FloorToFloat(Y)) & 255;

    const float Xf = X - FMath::FloorToFloat(X);
    const float Yf = Y - FMath::FloorToFloat(Y);

    const float U = Fade(Xf);
    const float V = Fade(Yf);

    const int32 AA = PermutationTable[PermutationTable[Xi] + Yi];
    const int32 AB = PermutationTable[PermutationTable[Xi] + Yi + 1];
    const int32 BA = PermutationTable[PermutationTable[Xi + 1] + Yi];
    const int32 BB = PermutationTable[PermutationTable[Xi + 1] + Yi + 1];

    const float X1 = Lerp(U, Grad(AA, Xf, Yf), Grad(BA, Xf - 1.0f, Yf));
    const float X2 = Lerp(U, Grad(AB, Xf, Yf - 1.0f), Grad(BB, Xf - 1.0f, Yf - 1.0f));

    return Lerp(V, X1, X2);
}

float UVoxelWorldManager::FractalNoise2D(float X, float Y, const FPerlinOctaveSettings& Settings)
{
    const int32 ClampedOctaves = FMath::Clamp(Settings.OctaveCount, 1, 8);
    const float SafeFreq = FMath::Max(0.0001f, Settings.BaseFrequency);
    const float SafeAmp = FMath::Max(0.1f, Settings.BaseAmplitude);

    float Total = 0.0f;
    float Frequency = SafeFreq;
    float Amplitude = 1.0f;
    float MaxAmplitude = 0.0f;

    for (int32 i = 0; i < ClampedOctaves; ++i)
    {
        const float OffsetX = static_cast<float>(Settings.SeedOffset * 17 + i * 31);
        const float OffsetY = static_cast<float>(Settings.SeedOffset * 13 + i * 47);

        Total += PerlinNoise2D(
            (X + OffsetX) * Frequency,
            (Y + OffsetY) * Frequency
        ) * Amplitude;

        MaxAmplitude += Amplitude;
        Frequency *= Settings.Lacunarity;
        Amplitude *= Settings.Persistence;
    }

    const float Normalized = Total / FMath::Max(0.001f, MaxAmplitude);
    return Normalized * SafeAmp;
}

FPerlinOctaveSettings UVoxelWorldManager::GetOctaveSettingsForProfile(const FVoxelBiomeProfile& Profile) const
{
    FPerlinOctaveSettings Settings = DefaultPerlinSettings;

    if (Profile.NoiseFrequency > KINDA_SMALL_NUMBER)
    {
        Settings.BaseFrequency = Profile.NoiseFrequency;
    }

    if (Profile.NoiseAmplitude > KINDA_SMALL_NUMBER)
    {
        const float SafeVoxelSize = (VoxelSize > KINDA_SMALL_NUMBER) ? VoxelSize : 100.0f;
        Settings.BaseAmplitude = FMath::Max(0.5f, Profile.NoiseAmplitude / SafeVoxelSize);
    }

    if (Profile.OctaveCount > 0)
    {
        Settings.OctaveCount = FMath::Clamp(Profile.OctaveCount, 1, 8);
    }

    return Settings;
}

int32 UVoxelWorldManager::ComputeSmoothSurfaceHeightAtVoxel(
    int32 VoxelX,
    int32 VoxelY,
    const FVoxelBiomeProfile& Profile
) const
{
    const FPerlinOctaveSettings Settings = GetOctaveSettingsForProfile(Profile);

    const float Height = FractalNoise2D(
        static_cast<float>(VoxelX),
        static_cast<float>(VoxelY),
        Settings
    );

    const float FinalHeight = FMath::Clamp(Height, -96.0f, 96.0f);
    return FMath::RoundToInt(FinalHeight);
}

float UVoxelWorldManager::ComputeDetailNoiseHeightBilinear(
    int32 MinX,
    int32 MinY,
    int32 MaxX,
    int32 MaxY,
    int32 X,
    int32 Y,
    const FVoxelBiomeProfile& Profile
) const
{
    const float FX = static_cast<float>(X - MinX) / static_cast<float>(FMath::Max(1, MaxX - MinX));
    const float FY = static_cast<float>(Y - MinY) / static_cast<float>(FMath::Max(1, MaxY - MinY));

    const float H00 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(MinX, MinY, Profile));
    const float H10 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(MaxX, MinY, Profile));
    const float H01 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(MinX, MaxY, Profile));
    const float H11 = static_cast<float>(ComputeSmoothSurfaceHeightAtVoxel(MaxX, MaxY, Profile));

    const float X0 = FMath::Lerp(H00, H10, FX);
    const float X1 = FMath::Lerp(H01, H11, FX);
    return FMath::Lerp(X0, X1, FY);
}

// ─── [TERRAIN CACHE] ─────────────────────────────────────────

void UVoxelWorldManager::CacheTerrainMetadata(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight)
{
    CachedTerrainGridWidth = GridWidth;
    CachedTerrainGridHeight = GridHeight;

    CachedSurfaceHeightsVoxels.SetNumZeroed(GridWidth * GridHeight);
    CachedVoxelProfiles.SetNum(GridWidth * GridHeight);
    CachedBiomeTags.SetNum(GridWidth * GridHeight);
    CachedTileElevations.SetNumZeroed(GridWidth * GridHeight);

    // Pass 1: cache source data
    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Index = (Y * GridWidth) + X;
            const FWorldTileData& Tile = WorldTiles[Index];

            CachedTileElevations[Index] = FMath::Clamp(Tile.Elevation, 0.0f, 1.0f);
            CachedVoxelProfiles[Index] = Tile.VoxelProfile;
            CachedBiomeTags[Index] = Tile.BiomeType;
        }
    }

    // Pass 2: compute heights using the v2.1 formula.
    // Macro height from Tile.Elevation + ridge displacement from Step03 ranges.
    const FWorldForgeTerrainSnapshot CacheSnapshot = MakeLocalTerrainSnapshot();
    const float ChunkSizeF = static_cast<float>(FMath::Max(1, GetChunkSizeInVoxels()));
    const float HalfGridXF = static_cast<float>(GridWidth) * 0.5f;
    const float HalfGridYF = static_cast<float>(GridHeight) * 0.5f;
    const float CacheSeaLevel = static_cast<float>(SeaLevelVoxels);

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Index = (Y * GridWidth) + X;

            // Macro height from tile elevation
            float SurfaceHeight =
                FWorldForgeTerrainMath::ComputeSurfaceHeightFromTileElevation(
                    CachedTileElevations[Index],
                    CacheSnapshot
                );

            // Ridge displacement (land only) — sample at tile center voxel position
            if (SurfaceHeight > CacheSeaLevel && CacheSnapshot.PipelineMountainRanges.Num() > 0)
            {
                const float VoxelX = (static_cast<float>(X) + 0.5f - HalfGridXF) * ChunkSizeF;
                const float VoxelY = (static_cast<float>(Y) + 0.5f - HalfGridYF) * ChunkSizeF;
                SurfaceHeight += FWorldForgeTerrainMath::SampleMountainRidgeHeight(
                    VoxelX, VoxelY, CacheSnapshot);
            }

            CachedSurfaceHeightsVoxels[Index] = SurfaceHeight;
        }
    }

    if (bLogTerrainGeneration)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Cached terrain metadata. Cells=%d Grid=%dx%d"),
            CachedSurfaceHeightsVoxels.Num(),
            CachedTerrainGridWidth,
            CachedTerrainGridHeight
        );
    }
}

void UVoxelWorldManager::ClearTerrainCache()
{
    CachedSurfaceHeightsVoxels.Reset();
    CachedVoxelProfiles.Reset();
    CachedBiomeTags.Reset();
    CachedTileElevations.Reset();
    // v5.0: Clear terrain modifications on full cache reset
    TerrainSnapshot.ClearAllModifications();

    // NOTE: CachedMountainRanges and CachedVolcanoes are NOT cleared here.
    // They are pipeline SOURCE data set by SetPipelineMountainRanges() and
    // SetPipelineVolcanoes() BEFORE GenerateTerrainFromTiles() is called.
    // GenerateTerrainFromTiles() calls ClearTerrainCache() before the deferred
    // retry path, which previously wiped this data before BuildTerrainSnapshot
    // could read it — causing Volcanoes=0 in the generator.
    // Pipeline data is cleared in Deinitialize() and SetPipeline*() instead.

    CachedTerrainGridWidth = 0;
    CachedTerrainGridHeight = 0;

    bTerrainReadyBroadcasted = false;
}

bool UVoxelWorldManager::IsValidTerrainCell(FIntPoint GridCell) const
{
    return
        GridCell.X >= 0 &&
        GridCell.Y >= 0 &&
        GridCell.X < CachedTerrainGridWidth &&
        GridCell.Y < CachedTerrainGridHeight;
}

FIntPoint UVoxelWorldManager::ClampTerrainCellToBounds(FIntPoint GridCell) const
{
    if (CachedTerrainGridWidth <= 0 || CachedTerrainGridHeight <= 0)
    {
        return GridCell;
    }

    return FIntPoint(
        FMath::Clamp(GridCell.X, 0, CachedTerrainGridWidth - 1),
        FMath::Clamp(GridCell.Y, 0, CachedTerrainGridHeight - 1)
    );
}

int32 UVoxelWorldManager::TerrainIndex(FIntPoint GridCell) const
{
    return (GridCell.Y * CachedTerrainGridWidth) + GridCell.X;
}

bool UVoxelWorldManager::HasCachedTerrainData() const
{
    return
        CachedTerrainGridWidth > 0 &&
        CachedTerrainGridHeight > 0 &&
        CachedSurfaceHeightsVoxels.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight) &&
        CachedVoxelProfiles.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight) &&
        CachedBiomeTags.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight) &&
        CachedTileElevations.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight);
}

bool UVoxelWorldManager::IsSpawnableTerrainCell(FIntPoint GridCell) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    const FIntPoint ClampedCell = ClampTerrainCellToBounds(GridCell);
    const int32 Index = TerrainIndex(ClampedCell);

    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index) ||
        !CachedBiomeTags.IsValidIndex(Index))
    {
        return false;
    }

    const FGameplayTag& Biome = CachedBiomeTags[Index];

    const FGameplayTag OceanTag = GetBiomeOceanTag();
    const FGameplayTag SeaTag = GetBiomeSeaTag();
    const FGameplayTag LakeTag = GetBiomeLakeTag();
    const FGameplayTag WaterTag = GetBiomeWaterTag();
    const FGameplayTag DeepOceanTag = GetBiomeDeepOceanTag();
    const FGameplayTag LavaTag = GetBiomeLavaTag();

    // Hard reject only true water / lava biomes.
    if (Biome.MatchesTag(OceanTag) ||
        Biome.MatchesTag(SeaTag) ||
        Biome.MatchesTag(LakeTag) ||
        Biome.MatchesTag(WaterTag) ||
        Biome.MatchesTag(DeepOceanTag) ||
        Biome.MatchesTag(LavaTag))
    {
        return false;
    }

    EWorldForgeTerrainType TerrainType = EWorldForgeTerrainType::Grass;
    if (!GetTerrainTypeAtCell(ClampedCell, TerrainType))
    {
        return false;
    }

    // Reject only actual water terrain.
    if (TerrainType == EWorldForgeTerrainType::Water)
    {
        return false;
    }

    const float SurfaceHeightVoxels = CachedSurfaceHeightsVoxels[Index];

    // Must be above sea level, but dry sand/coast is allowed.
    if (SurfaceHeightVoxels <= static_cast<float>(SeaLevelVoxels))
    {
        return false;
    }

    return true;
}

bool UVoxelWorldManager::FindNearestSpawnableTerrainCell(
    FIntPoint StartCell,
    FIntPoint& OutCell,
    int32 MaxSearchRadius) const
{
    if (!HasCachedTerrainData())
    {
        return false;
    }

    StartCell = ClampTerrainCellToBounds(StartCell);

    if (IsSpawnableTerrainCell(StartCell))
    {
        OutCell = StartCell;
        return true;
    }

    const int32 RadiusMax = FMath::Max(1, MaxSearchRadius);

    for (int32 Radius = 1; Radius <= RadiusMax; ++Radius)
    {
        const int32 MinY = FMath::Max(0, StartCell.Y - Radius);
        const int32 MaxY = FMath::Min(CachedTerrainGridHeight - 1, StartCell.Y + Radius);
        const int32 MinX = FMath::Max(0, StartCell.X - Radius);
        const int32 MaxX = FMath::Min(CachedTerrainGridWidth - 1, StartCell.X + Radius);

        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 X = MinX; X <= MaxX; ++X)
            {
                const bool bOnRing =
                    (X == MinX) ||
                    (X == MaxX) ||
                    (Y == MinY) ||
                    (Y == MaxY);

                if (!bOnRing)
                {
                    continue;
                }

                const FIntPoint Candidate(X, Y);
                if (IsSpawnableTerrainCell(Candidate))
                {
                    OutCell = Candidate;
                    return true;
                }
            }
        }
    }

    return false;
}

// ─── [TERRAIN HEIGHTS] ───────────────────────────────────────

int32 UVoxelWorldManager::ComputeRiverCarveDepthVoxels(FIntPoint ChunkCoord) const
{
    const float SampleX = static_cast<float>(ChunkCoord.X);
    const float SampleY = static_cast<float>(ChunkCoord.Y);

    const float RiverNoise = PerlinNoise2D(
        SampleX * RiverFrequency + 1703.0f,
        SampleY * RiverFrequency + 811.0f
    );

    const float RiverMask = 1.0f - FMath::Abs(RiverNoise);

    if (RiverMask < RiverThreshold)
    {
        return 0;
    }

    const float RiverAlpha = (RiverMask - RiverThreshold) / FMath::Max(0.001f, 1.0f - RiverThreshold);
    const float ShapedRiver = FMath::Pow(
        FMath::Clamp(RiverAlpha, 0.0f, 1.0f),
        FMath::Max(0.1f, RiverSharpness)
    );

    return FMath::RoundToInt(ShapedRiver * static_cast<float>(RiverCarveDepthVoxels));
}

// ── v2.1: Single-path height from tile elevation + ridge displacement.

int32 UVoxelWorldManager::ComputeChunkSurfaceHeightVoxels(
    FIntPoint ChunkCoord,
    const FVoxelBiomeProfile& Profile,
    float TileElevation
) const
{
    const FWorldForgeTerrainSnapshot Snapshot = MakeLocalTerrainSnapshot();

    float SurfaceHeightF =
        FWorldForgeTerrainMath::ComputeSurfaceHeightFromTileElevation(
            TileElevation,
            Snapshot
        );

    // Add ridge displacement (land only)
    const float LocalSeaLevel = static_cast<float>(SeaLevelVoxels);
    if (SurfaceHeightF > LocalSeaLevel && Snapshot.PipelineMountainRanges.Num() > 0)
    {
        const int32 ChunkSizeV = GetChunkSizeInVoxels();
        const float HalfGX = static_cast<float>(CachedTerrainGridWidth) * 0.5f;
        const float HalfGY = static_cast<float>(CachedTerrainGridHeight) * 0.5f;
        const float VoxelX = (static_cast<float>(ChunkCoord.X) + 0.5f - HalfGX) * static_cast<float>(ChunkSizeV);
        const float VoxelY = (static_cast<float>(ChunkCoord.Y) + 0.5f - HalfGY) * static_cast<float>(ChunkSizeV);
        SurfaceHeightF += FWorldForgeTerrainMath::SampleMountainRidgeHeight(VoxelX, VoxelY, Snapshot);
    }

    return FMath::RoundToInt(SurfaceHeightF);
}

// ─── FIX 6: Rewritten to use the same tile-elevation formula as ComputeChunkSurfaceHeightVoxels
// (and by extension the voxel generator). The old version duplicated the independent continent
// noise logic from the pre-fix generator, creating a third inconsistent terrain brain.
// This version bilinearly interpolates tile elevation at the precise voxel XY coordinate,
// producing sub-chunk resolution that is still consistent with the generator output.

bool UVoxelWorldManager::TryGetTerrainSurfaceVoxelZ(FIntPoint VoxelXY, int32& OutVoxelZ) const
{
    OutVoxelZ = 0;

    if (!HasCachedTerrainData())
    {
        return false;
    }

    const int32 ChunkSize = GetChunkSizeInVoxels();

    const float HalfGridX = static_cast<float>(CachedTerrainGridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(CachedTerrainGridHeight) * 0.5f;

    const float CellX = (static_cast<float>(VoxelXY.X) / static_cast<float>(ChunkSize)) + HalfGridX;
    const float CellY = (static_cast<float>(VoxelXY.Y) / static_cast<float>(ChunkSize)) + HalfGridY;

    const int32 X0 = FMath::FloorToInt(CellX);
    const int32 Y0 = FMath::FloorToInt(CellY);
    const int32 X1 = X0 + 1;
    const int32 Y1 = Y0 + 1;

    const float FX = CellX - static_cast<float>(X0);
    const float FY = CellY - static_cast<float>(Y0);

    const FIntPoint C00 = ClampTerrainCellToBounds(FIntPoint(X0, Y0));
    const FIntPoint C10 = ClampTerrainCellToBounds(FIntPoint(X1, Y0));
    const FIntPoint C01 = ClampTerrainCellToBounds(FIntPoint(X0, Y1));
    const FIntPoint C11 = ClampTerrainCellToBounds(FIntPoint(X1, Y1));

    const int32 I00 = TerrainIndex(C00);
    const int32 I10 = TerrainIndex(C10);
    const int32 I01 = TerrainIndex(C01);
    const int32 I11 = TerrainIndex(C11);

    const float E00 = CachedTileElevations.IsValidIndex(I00) ? CachedTileElevations[I00] : 0.0f;
    const float E10 = CachedTileElevations.IsValidIndex(I10) ? CachedTileElevations[I10] : E00;
    const float E01 = CachedTileElevations.IsValidIndex(I01) ? CachedTileElevations[I01] : E00;
    const float E11 = CachedTileElevations.IsValidIndex(I11) ? CachedTileElevations[I11] : E00;

    const float TileElevation01 = FMath::Lerp(
        FMath::Lerp(E00, E10, FX),
        FMath::Lerp(E01, E11, FX),
        FY
    );

    // ── v2.1: Macro height + ridge displacement.
    const FWorldForgeTerrainSnapshot Snapshot = MakeLocalTerrainSnapshot();

    float SurfaceHeightF =
        FWorldForgeTerrainMath::ComputeSurfaceHeightFromTileElevation(
            TileElevation01,
            Snapshot
        );

    // Add ridge displacement (land only)
    const float LocalSeaLevel = static_cast<float>(SeaLevelVoxels);
    if (SurfaceHeightF > LocalSeaLevel && Snapshot.PipelineMountainRanges.Num() > 0)
    {
        SurfaceHeightF += FWorldForgeTerrainMath::SampleMountainRidgeHeight(
            static_cast<float>(VoxelXY.X), static_cast<float>(VoxelXY.Y), Snapshot);
    }

    OutVoxelZ = FMath::RoundToInt(SurfaceHeightF);
    return true;
}

// ─── [TERRAIN ANALYSIS] ──────────────────────────────────────

// ─── FIX 7: MaxElevation now correctly reflects the actual range that
// ComputeChunkSurfaceHeightVoxels can produce after Fix 5. The old formula
// added 54 extra voxels (24+18+12) that were artefacts of the old local noise
// height contributions — those no longer exist in the cache. Using a ceiling
// that is too high compresses all the terrain type thresholds downward, causing
// rock and snow to never appear on the map even for high-elevation tiles.
float UVoxelWorldManager::GetNormalizedElevationForTerrainCell(FIntPoint GridCell) const
{
    if (!IsValidTerrainCell(GridCell))
    {
        return 0.0f;
    }

    const int32 Index = TerrainIndex(GridCell);
    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return 0.0f;
    }

    // ── v2: Use the single-path normalization.
    // NormLandElev = (height - SeaLevel) / MaxLandHeightVoxels, clamped 0–1.
    const FWorldForgeTerrainSnapshot Snapshot = MakeLocalTerrainSnapshot();

    return FWorldForgeTerrainMath::ComputeNormalizedLandElevation(
        CachedSurfaceHeightsVoxels[Index],
        Snapshot
    );
}

float UVoxelWorldManager::ComputeTerrainSlopeDegreesInternal(FIntPoint GridCell) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return 0.0f;
    }

    const FIntPoint Center = ClampTerrainCellToBounds(GridCell);
    const FIntPoint North = ClampTerrainCellToBounds(FIntPoint(Center.X, Center.Y - 1));
    const FIntPoint South = ClampTerrainCellToBounds(FIntPoint(Center.X, Center.Y + 1));
    const FIntPoint West = ClampTerrainCellToBounds(FIntPoint(Center.X - 1, Center.Y));
    const FIntPoint East = ClampTerrainCellToBounds(FIntPoint(Center.X + 1, Center.Y));

    const int32 NorthIndex = TerrainIndex(North);
    const int32 SouthIndex = TerrainIndex(South);
    const int32 WestIndex = TerrainIndex(West);
    const int32 EastIndex = TerrainIndex(East);

    if (!CachedSurfaceHeightsVoxels.IsValidIndex(NorthIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(SouthIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(WestIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(EastIndex))
    {
        return 0.0f;
    }

    const float RVS = GetRenderVoxelSize();
    const float NorthHeightWorld = CachedSurfaceHeightsVoxels[NorthIndex] * RVS;
    const float SouthHeightWorld = CachedSurfaceHeightsVoxels[SouthIndex] * RVS;
    const float WestHeightWorld = CachedSurfaceHeightsVoxels[WestIndex] * RVS;
    const float EastHeightWorld = CachedSurfaceHeightsVoxels[EastIndex] * RVS;

    const float CellRunWorld = GetTerrainCellWorldSize();
    if (CellRunWorld <= KINDA_SMALL_NUMBER)
    {
        return 0.0f;
    }

    const float GradientX = (EastHeightWorld - WestHeightWorld) / (2.0f * CellRunWorld);
    const float GradientY = (SouthHeightWorld - NorthHeightWorld) / (2.0f * CellRunWorld);

    const float GradientMagnitude = FMath::Sqrt((GradientX * GradientX) + (GradientY * GradientY));
    return FMath::RadiansToDegrees(FMath::Atan(GradientMagnitude));
}

EWorldForgeTerrainType UVoxelWorldManager::ClassifyTerrainCellInternal(FIntPoint GridCell) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return EWorldForgeTerrainType::Grass;
    }

    const FIntPoint ClampedCell = ClampTerrainCellToBounds(GridCell);
    const int32 Index = TerrainIndex(ClampedCell);

    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return EWorldForgeTerrainType::Grass;
    }

    const float SurfaceHeightVoxels = CachedSurfaceHeightsVoxels[Index];
    const float NormalizedElevation = GetNormalizedElevationForTerrainCell(ClampedCell);
    const float SlopeDegrees = ComputeTerrainSlopeDegreesInternal(ClampedCell);

    const float CoastBandLocal = FMath::Max(0.0f, CoastBandVoxels);
    const float SandMaxElevNormLocal = FMath::Clamp(SandMaxElevationNormalized, 0.0f, 1.0f);
    const float DirtMinElevNormLocal = FMath::Clamp(DirtMinElevationNormalized, 0.0f, 1.0f);
    const float RockMinElevNormLocal = FMath::Clamp(RockMinElevationNormalized, 0.0f, 1.0f);
    const float HighRockMinElevNormLocal = FMath::Clamp(HighRockMinElevationNormalized, 0.0f, 1.0f);
    const float SnowMinElevNormLocal = FMath::Clamp(SnowMinElevationNormalized, 0.0f, 1.0f);

    const float DirtSlopeDegLocal = FMath::Max(0.0f, DirtSlopeThresholdDegrees);
    const float RockSlopeDegLocal = FMath::Max(0.0f, RockSlopeThresholdDegrees);
    const float CliffSlopeDegLocal = FMath::Max(0.0f, CliffSlopeThresholdDegrees);

    const FGameplayTag& BiomeTag = CachedBiomeTags.IsValidIndex(Index)
        ? CachedBiomeTags[Index]
        : FGameplayTag::EmptyTag;

    const bool bTaggedAsWater =
        BiomeTag.MatchesTag(GetBiomeOceanTag()) ||
        BiomeTag.MatchesTag(GetBiomeDeepOceanTag()) ||
        BiomeTag.MatchesTag(GetBiomeSeaTag()) ||
        BiomeTag.MatchesTag(GetBiomeLakeTag()) ||
        BiomeTag.MatchesTag(GetBiomeWaterTag());

    // ── Lava check (before water — lava biomes override elevation logic)
    if (BiomeTag.MatchesTag(GetBiomeLavaTag()))
    {
        return EWorldForgeTerrainType::Lava;
    }

    const bool bBelowSeaLevel = SurfaceHeightVoxels <= static_cast<float>(SeaLevelVoxels);

    if (bTaggedAsWater || bBelowSeaLevel)
    {
        return EWorldForgeTerrainType::Water;
    }

    const bool bNearSeaLevel =
        SurfaceHeightVoxels <= (static_cast<float>(SeaLevelVoxels) + CoastBandLocal);

    if (bNearSeaLevel || NormalizedElevation <= SandMaxElevNormLocal)
    {
        return EWorldForgeTerrainType::Sand;
    }

    if (NormalizedElevation >= SnowMinElevNormLocal)
    {
        return EWorldForgeTerrainType::Snow;
    }

    if (SlopeDegrees >= CliffSlopeDegLocal)
    {
        return EWorldForgeTerrainType::Rock;
    }

    if (NormalizedElevation >= HighRockMinElevNormLocal)
    {
        return EWorldForgeTerrainType::Rock;
    }

    if (NormalizedElevation >= RockMinElevNormLocal && SlopeDegrees >= RockSlopeDegLocal)
    {
        return EWorldForgeTerrainType::Rock;
    }

    if (NormalizedElevation >= DirtMinElevNormLocal || SlopeDegrees >= DirtSlopeDegLocal)
    {
        return EWorldForgeTerrainType::Dirt;
    }

    return EWorldForgeTerrainType::Grass;
}

bool UVoxelWorldManager::GetTerrainTypeAtCell(FIntPoint GridCell, EWorldForgeTerrainType& OutTerrainType) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    OutTerrainType = ClassifyTerrainCellInternal(GridCell);
    return true;
}

bool UVoxelWorldManager::GetTerrainTypeAtVoxel(FIntPoint VoxelXY, EWorldForgeTerrainType& OutTerrainType) const
{
    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelXY);
    return GetTerrainTypeAtCell(ChunkCell, OutTerrainType);
}

bool UVoxelWorldManager::GetTerrainSlopeAtCell(FIntPoint GridCell, float& OutSlopeDegrees) const
{
    OutSlopeDegrees = 0.0f;

    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    OutSlopeDegrees = ComputeTerrainSlopeDegreesInternal(GridCell);
    return true;
}

bool UVoxelWorldManager::GetTerrainSlopeAtVoxel(FIntPoint VoxelXY, float& OutSlopeDegrees) const
{
    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelXY);
    return GetTerrainSlopeAtCell(ChunkCell, OutSlopeDegrees);
}

// ─── FIX 8: MaxElev now matches the actual cache range from Fix 5/7.
// The old value added 54 extra voxels (24+18+12) from the removed local
// noise contributions, compressing all the terrain-type thresholds and
// preventing rock/snow from appearing correctly on the map.
bool UVoxelWorldManager::GetTerrainTypeAtVoxelSample(
    FIntPoint VoxelXY,
    EWorldForgeTerrainType& OutTerrainType) const
{
    if (!HasCachedTerrainData())
    {
        return false;
    }

    const int32 ChunkSize = GetChunkSizeInVoxels();
    if (ChunkSize <= 0)
    {
        return GetTerrainTypeAtVoxel(VoxelXY, OutTerrainType);
    }

    const float HalfGridX = static_cast<float>(CachedTerrainGridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(CachedTerrainGridHeight) * 0.5f;

    const float CellXF =
        (static_cast<float>(VoxelXY.X) / static_cast<float>(ChunkSize)) + HalfGridX;
    const float CellYF =
        (static_cast<float>(VoxelXY.Y) / static_cast<float>(ChunkSize)) + HalfGridY;

    const int32 X0 = FMath::FloorToInt(CellXF);
    const int32 Y0 = FMath::FloorToInt(CellYF);
    const float FracX = CellXF - static_cast<float>(X0);
    const float FracY = CellYF - static_cast<float>(Y0);

    const FIntPoint Cell00(X0, Y0);
    const FIntPoint Cell10(X0 + 1, Y0);
    const FIntPoint Cell01(X0, Y0 + 1);
    const FIntPoint Cell11(X0 + 1, Y0 + 1);

    auto SafeHeight = [&](FIntPoint Cell) -> float
        {
            const FIntPoint C = ClampTerrainCellToBounds(Cell);
            const int32 Idx = TerrainIndex(C);
            return CachedSurfaceHeightsVoxels.IsValidIndex(Idx)
                ? CachedSurfaceHeightsVoxels[Idx]
                : static_cast<float>(SeaLevelVoxels);
        };

    const float H00 = SafeHeight(Cell00);
    const float H10 = SafeHeight(Cell10);
    const float H01 = SafeHeight(Cell01);
    const float H11 = SafeHeight(Cell11);

    const float InterpH = FMath::Lerp(
        FMath::Lerp(H00, H10, FracX),
        FMath::Lerp(H01, H11, FracX),
        FracY);

    auto SafeSlope = [&](FIntPoint Cell) -> float
        {
            return IsValidTerrainCell(Cell) ? ComputeTerrainSlopeDegreesInternal(Cell) : 0.0f;
        };

    const float InterpSlope = FMath::Lerp(
        FMath::Lerp(SafeSlope(Cell00), SafeSlope(Cell10), FracX),
        FMath::Lerp(SafeSlope(Cell01), SafeSlope(Cell11), FracX),
        FracY);

    {
        const FIntPoint CenterCell = ClampTerrainCellToBounds(Cell00);
        const int32 CenterIdx = TerrainIndex(CenterCell);

        const FGameplayTag& BiomeTag = CachedBiomeTags.IsValidIndex(CenterIdx)
            ? CachedBiomeTags[CenterIdx]
            : FGameplayTag::EmptyTag;

        const bool bTaggedWater =
            BiomeTag.MatchesTag(GetBiomeOceanTag()) ||
            BiomeTag.MatchesTag(GetBiomeDeepOceanTag()) ||
            BiomeTag.MatchesTag(GetBiomeSeaTag()) ||
            BiomeTag.MatchesTag(GetBiomeLakeTag()) ||
            BiomeTag.MatchesTag(GetBiomeWaterTag());

        // ── Lava check (before water — lava overrides elevation)
        if (BiomeTag.MatchesTag(GetBiomeLavaTag()))
        {
            OutTerrainType = EWorldForgeTerrainType::Lava;
            return true;
        }

        if (bTaggedWater || InterpH <= static_cast<float>(SeaLevelVoxels))
        {
            OutTerrainType = EWorldForgeTerrainType::Water;
            return true;
        }
    }

    // ── v2: Use the single-path normalization.
    const FWorldForgeTerrainSnapshot NormSnapshot = MakeLocalTerrainSnapshot();
    const float NormElev = FWorldForgeTerrainMath::ComputeNormalizedLandElevation(
        InterpH, NormSnapshot);

    const float CoastBandLocal = FMath::Max(0.0f, CoastBandVoxels);
    const float SandMaxElevNorm = FMath::Clamp(SandMaxElevationNormalized, 0.0f, 1.0f);
    const float DirtMinElevNorm = FMath::Clamp(DirtMinElevationNormalized, 0.0f, 1.0f);
    const float RockMinElevNorm = FMath::Clamp(RockMinElevationNormalized, 0.0f, 1.0f);
    const float HighRockMinElevNorm = FMath::Clamp(HighRockMinElevationNormalized, 0.0f, 1.0f);
    const float SnowMinElevNorm = FMath::Clamp(SnowMinElevationNormalized, 0.0f, 1.0f);

    const float DirtSlopeDeg = FMath::Max(0.0f, DirtSlopeThresholdDegrees);
    const float RockSlopeDeg = FMath::Max(0.0f, RockSlopeThresholdDegrees);
    const float CliffSlopeDeg = FMath::Max(0.0f, CliffSlopeThresholdDegrees);

    const float SeaLevel = static_cast<float>(SeaLevelVoxels);

    if (InterpH <= SeaLevel)
    {
        OutTerrainType = EWorldForgeTerrainType::Water;
        return true;
    }

    if (InterpH <= (SeaLevel + CoastBandLocal) || NormElev <= SandMaxElevNorm)
    {
        OutTerrainType = EWorldForgeTerrainType::Sand;
        return true;
    }

    if (NormElev >= SnowMinElevNorm)
    {
        OutTerrainType = EWorldForgeTerrainType::Snow;
        return true;
    }

    if (InterpSlope >= CliffSlopeDeg)
    {
        OutTerrainType = EWorldForgeTerrainType::Rock;
        return true;
    }

    if (NormElev >= HighRockMinElevNorm)
    {
        OutTerrainType = EWorldForgeTerrainType::Rock;
        return true;
    }

    if (NormElev >= RockMinElevNorm && InterpSlope >= RockSlopeDeg)
    {
        OutTerrainType = EWorldForgeTerrainType::Rock;
        return true;
    }

    if (NormElev >= DirtMinElevNorm || InterpSlope >= DirtSlopeDeg)
    {
        OutTerrainType = EWorldForgeTerrainType::Dirt;
        return true;
    }

    OutTerrainType = EWorldForgeTerrainType::Grass;
    return true;
}

// ─── [TERRAIN QUERIES] ───────────────────────────────────────

// ─── FIX 3: Initialise OutWorldLocation to zero (was uninitialized on early
// returns), and add the voxel world origin's Z so that the returned world
// location is correct when the AVoxelWorld actor is not at Z=0.
bool UVoxelWorldManager::TryGetSurfaceWorldLocation(
    FIntPoint GridCell,
    FVector& OutWorldLocation,
    float ZOffset) const
{
    OutWorldLocation = FVector::ZeroVector;

    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(GridCell);
    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return false;
    }

    const float SurfaceHeightWorld =
        GetVoxelWorldOrigin().Z +
        (CachedSurfaceHeightsVoxels[Index] * GetRenderVoxelSize()) +
        ZOffset;

    OutWorldLocation = TerrainCellToWorld(GridCell, SurfaceHeightWorld, true);
    return true;
}

bool UVoxelWorldManager::TryGetNearestSpawnLocationForTerrainCell(
    FIntPoint DesiredCell,
    FVector& OutWorldLocation,
    FIntPoint& OutResolvedCell,
    int32 MaxSearchRadius,
    float ZOffset) const
{
    OutWorldLocation = FVector::ZeroVector;
    OutResolvedCell = DesiredCell;

    if (!HasCachedTerrainData())
    {
        return false;
    }

    FIntPoint ResolvedCell = DesiredCell;
    if (!FindNearestSpawnableTerrainCell(DesiredCell, ResolvedCell, MaxSearchRadius))
    {
        return false;
    }

    FVector SurfaceLocation = FVector::ZeroVector;
    if (!TryGetSurfaceWorldLocation(ResolvedCell, SurfaceLocation, ZOffset))
    {
        return false;
    }

    OutResolvedCell = ResolvedCell;
    OutWorldLocation = SurfaceLocation;
    return true;
}

// ─── FIX 4: Add the voxel world origin's Z so the returned world-space height
// is correct when the AVoxelWorld actor is not placed at Z=0 in the level.
bool UVoxelWorldManager::TryGetCachedSurfaceHeightWorldForVoxelCell(
    FIntPoint VoxelCell,
    float& OutSurfaceHeightWorld) const
{
    OutSurfaceHeightWorld = 0.0f;

    if (!HasCachedTerrainData())
    {
        return false;
    }

    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelCell);
    if (!IsValidTerrainCell(ChunkCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(ChunkCell);
    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return false;
    }

    OutSurfaceHeightWorld =
        GetVoxelWorldOrigin().Z +
        (CachedSurfaceHeightsVoxels[Index] * GetRenderVoxelSize());

    return true;
}

// ─── [GENERATION ORCHESTRATOR] ───────────────────────────────

bool UVoxelWorldManager::GenerateTerrainFromTiles(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight,
    ETerrainMode TerrainMode,
    int32 BlockHeightStep)
{
    ClearTerrainSnapshot();

    if (GridWidth <= 0 || GridHeight <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles failed: invalid grid size %d x %d."),
            GridWidth,
            GridHeight
        );
        return false;
    }

    const int32 ExpectedTileCount = GridWidth * GridHeight;
    if (WorldTiles.Num() != ExpectedTileCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles failed: tile count mismatch. Expected=%d Actual=%d"),
            ExpectedTileCount,
            WorldTiles.Num()
        );
        return false;
    }

    CurrentTerrainMode = ETerrainMode::Smooth;
    CurrentBlockHeightStep = 1;

    if (TerrainMode == ETerrainMode::Blocky)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] Blocky terrain mode requested, but WorldForge is forcing Smooth mode to match GDD terrain direction.")
        );
    }

    ClearTerrainCache();

    PendingWorldTiles = WorldTiles;
    PendingGridWidth = GridWidth;
    PendingGridHeight = GridHeight;
    TerrainRetryCount = 0;

    if (!IsReady() || !IsVoxelRuntimeReady())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles deferred: VoxelWorld runtime is not ready yet.")
        );

        ScheduleTerrainRetry();
        return true;
    }

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles failed: cached actor is not AVoxelWorld.")
        );

        ClearTerrainSnapshot();
        return false;
    }

    const bool bSuccess = ApplyTerrainFromTilesInternal(
        VoxelWorld,
        WorldTiles,
        GridWidth,
        GridHeight
    );

    if (bSuccess)
    {
        ClearPendingTerrainRequest();
        BroadcastTerrainReadyIfNeeded();
    }
    else
    {
        ScheduleTerrainRetry();
    }

    return true;
}

bool UVoxelWorldManager::ApplyTerrainFromTilesInternal(
    AVoxelWorld* VoxelWorld,
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight)
{
    if (!VoxelWorld)
    {
        UE_LOG(LogTemp, Verbose, TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: VoxelWorld is null."));
        ClearTerrainSnapshot();
        return false;
    }

    if (GridWidth <= 0 || GridHeight <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: invalid grid size %d x %d."),
            GridWidth,
            GridHeight
        );
        ClearTerrainSnapshot();
        return false;
    }

    const int32 ExpectedTileCount = GridWidth * GridHeight;
    if (WorldTiles.Num() != ExpectedTileCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: tile count mismatch. Expected=%d Actual=%d"),
            ExpectedTileCount,
            WorldTiles.Num()
        );
        ClearTerrainSnapshot();
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelWorldManager] Terrain generation start (Density Generator). Tiles=%d Grid=%dx%d ChunkSizeVoxels=%d Mode=%s Step=%d"),
        WorldTiles.Num(),
        GridWidth,
        GridHeight,
        GetChunkSizeInVoxels(),
        CurrentTerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
        GetEffectiveBlockHeightStep()
    );

    CacheTerrainMetadata(WorldTiles, GridWidth, GridHeight);
    BuildTerrainSnapshot(WorldTiles, GridWidth, GridHeight);

    // ── Diagnostic: detect VoxelSize mismatch between settings and actor
    {
        const float SettingsVS = VoxelSize;
        const float ActorVS = GetRenderVoxelSize();
        if (FMath::Abs(SettingsVS - ActorVS) > 0.1f)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[VoxelWorldManager] VOXELSIZE MISMATCH: Settings=%.1f Actor=%.1f ChunkSizeVoxels=%d. "
                    "Snapshot uses Actor value. If terrain shape is wrong, check VoxelWorld actor's VoxelSize property."),
                SettingsVS, ActorVS, TerrainSnapshot.ChunkSizeVoxels);
        }
        else
        {
            UE_LOG(LogTemp, Log,
                TEXT("[VoxelWorldManager] VoxelSize confirmed: Settings=%.1f Actor=%.1f ChunkSizeVoxels=%d"),
                SettingsVS, ActorVS, TerrainSnapshot.ChunkSizeVoxels);
        }
    }

    if (!HasTerrainSnapshot())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: terrain snapshot build produced invalid state.")
        );
        return false;
    }

#if WITH_VOXEL_PLUGIN
    UWorldForgeTerrainGenerator* Generator = NewObject<UWorldForgeTerrainGenerator>(VoxelWorld);
    if (!Generator)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: could not allocate WorldForge terrain generator.")
        );
        return false;
    }

    Generator->SetTerrainSnapshot(TerrainSnapshot);
    Generator->SetSeed(1337);

    VoxelWorld->SetGeneratorObject(Generator);

    if (VoxelWorld->IsCreated())
    {
        VoxelWorld->DestroyWorld();
    }

    VoxelWorld->CreateWorld(FVoxelWorldCreateInfo());
#else
    UE_LOG(
        LogTemp,
        Error,
        TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: WITH_VOXEL_PLUGIN is disabled.")
    );
    return false;
#endif

    const bool bWorldCreated = VoxelWorld->IsCreated();
    if (!bWorldCreated)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: VoxelWorld did not report created after generator assignment.")
        );
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelWorldManager] Terrain generation complete (Density Generator). Applied=%d / %d SnapshotValid=%s"),
        ExpectedTileCount,
        ExpectedTileCount,
        HasTerrainSnapshot() ? TEXT("true") : TEXT("false")
    );

    BroadcastTerrainReadyIfNeeded();
    return true;
}

void UVoxelWorldManager::ScheduleTerrainRetry()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ScheduleTerrainRetry failed: GetWorld() returned null."));
        return;
    }

    if (bTerrainRetryScheduled)
    {
        return;
    }

    bTerrainRetryScheduled = true;

    World->GetTimerManager().SetTimer(
        TerrainRetryTimerHandle,
        this,
        &UVoxelWorldManager::RetryPendingTerrainGeneration,
        TerrainRetryIntervalSeconds,
        false
    );

    if (bLogTerrainRetry)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Scheduled terrain retry in %.2f seconds."),
            TerrainRetryIntervalSeconds
        );
    }
}

void UVoxelWorldManager::RetryPendingTerrainGeneration()
{
    bTerrainRetryScheduled = false;

    if (PendingWorldTiles.Num() == 0 || PendingGridWidth <= 0 || PendingGridHeight <= 0)
    {
        return;
    }

    if (!IsReady())
    {
        ++TerrainRetryCount;

        if (TerrainRetryCount > MaxTerrainRetryCount)
        {
            UE_LOG(LogTemp, Error, TEXT("[VoxelWorldManager] Terrain retry failed: VoxelWorld actor never became ready."));
            ClearPendingTerrainRequest();
            ClearTerrainSnapshot();
            return;
        }

        if (bLogTerrainRetry)
        {
            UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] Terrain retry %d: VoxelWorld actor still not ready."), TerrainRetryCount);
        }

        ScheduleTerrainRetry();
        return;
    }

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld)
    {
        ++TerrainRetryCount;

        if (TerrainRetryCount > MaxTerrainRetryCount)
        {
            UE_LOG(LogTemp, Error, TEXT("[VoxelWorldManager] Terrain retry failed: cached actor is not AVoxelWorld."));
            ClearPendingTerrainRequest();
            ClearTerrainSnapshot();
            return;
        }

        ScheduleTerrainRetry();
        return;
    }

    if (ApplyTerrainFromTilesInternal(VoxelWorld, PendingWorldTiles, PendingGridWidth, PendingGridHeight))
    {
        UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Pending terrain applied successfully on retry %d."), TerrainRetryCount);
        ClearPendingTerrainRequest();
        BroadcastTerrainReadyIfNeeded();
        return;
    }

    ++TerrainRetryCount;

    if (TerrainRetryCount > MaxTerrainRetryCount)
    {
        UE_LOG(LogTemp, Error, TEXT("[VoxelWorldManager] Terrain retry failed: generator terrain application never succeeded."));
        ClearPendingTerrainRequest();
        ClearTerrainSnapshot();
        return;
    }

    ScheduleTerrainRetry();
}

void UVoxelWorldManager::ClearPendingTerrainRequest()
{
    PendingWorldTiles.Reset();
    PendingGridWidth = 0;
    PendingGridHeight = 0;
    TerrainRetryCount = 0;
    bTerrainRetryScheduled = false;
}

void UVoxelWorldManager::BroadcastTerrainReadyIfNeeded()
{
    if (bTerrainReadyBroadcasted)
    {
        return;
    }

    if (!IsTerrainRuntimeStable())
    {
        return;
    }

    bTerrainReadyBroadcasted = true;

    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Terrain is ready for gameplay."));

    if (HasCachedTerrainData())
    {
        const FIntPoint CenterCell(
            FMath::Max(0, CachedTerrainGridWidth / 2),
            FMath::Max(0, CachedTerrainGridHeight / 2)
        );

        const FIntPoint TopLeft(0, 0);
        const FIntPoint TopRight(FMath::Max(0, CachedTerrainGridWidth - 1), 0);
        const FIntPoint BottomLeft(0, FMath::Max(0, CachedTerrainGridHeight - 1));
        const FIntPoint BottomRight(
            FMath::Max(0, CachedTerrainGridWidth - 1),
            FMath::Max(0, CachedTerrainGridHeight - 1)
        );

        FVector CenterWorld = FVector::ZeroVector;
        FVector TopLeftWorld = FVector::ZeroVector;
        FVector TopRightWorld = FVector::ZeroVector;
        FVector BottomLeftWorld = FVector::ZeroVector;
        FVector BottomRightWorld = FVector::ZeroVector;

        const bool bHasCenter = TryGetSurfaceWorldLocation(CenterCell, CenterWorld, TerrainSurfaceZOffset);
        const bool bHasTL = TryGetSurfaceWorldLocation(TopLeft, TopLeftWorld, TerrainSurfaceZOffset);
        const bool bHasTR = TryGetSurfaceWorldLocation(TopRight, TopRightWorld, TerrainSurfaceZOffset);
        const bool bHasBL = TryGetSurfaceWorldLocation(BottomLeft, BottomLeftWorld, TerrainSurfaceZOffset);
        const bool bHasBR = TryGetSurfaceWorldLocation(BottomRight, BottomRightWorld, TerrainSurfaceZOffset);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Terrain samples: CenterCell=(%d,%d) Center=(%.1f,%.1f,%.1f) TL=(%.1f,%.1f,%.1f) TR=(%.1f,%.1f,%.1f) BL=(%.1f,%.1f,%.1f) BR=(%.1f,%.1f,%.1f)"),
            CenterCell.X, CenterCell.Y,
            bHasCenter ? CenterWorld.X : -999999.0f, bHasCenter ? CenterWorld.Y : -999999.0f, bHasCenter ? CenterWorld.Z : -999999.0f,
            bHasTL ? TopLeftWorld.X : -999999.0f, bHasTL ? TopLeftWorld.Y : -999999.0f, bHasTL ? TopLeftWorld.Z : -999999.0f,
            bHasTR ? TopRightWorld.X : -999999.0f, bHasTR ? TopRightWorld.Y : -999999.0f, bHasTR ? TopRightWorld.Z : -999999.0f,
            bHasBL ? BottomLeftWorld.X : -999999.0f, bHasBL ? BottomLeftWorld.Y : -999999.0f, bHasBL ? BottomLeftWorld.Z : -999999.0f,
            bHasBR ? BottomRightWorld.X : -999999.0f, bHasBR ? BottomRightWorld.Y : -999999.0f, bHasBR ? BottomRightWorld.Z : -999999.0f
        );

        if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
        {
            FVector ViewLocation = FVector::ZeroVector;
            FRotator ViewRotation = FRotator::ZeroRotator;
            PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

            const double DistanceToCenter = bHasCenter ? FVector::Dist(ViewLocation, CenterWorld) : -1.0;

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[VoxelWorldManager] Player view at terrain-ready: View=(%.1f,%.1f,%.1f) Rot=(%.1f,%.1f,%.1f) DistToCenter=%.1f"),
                ViewLocation.X, ViewLocation.Y, ViewLocation.Z,
                ViewRotation.Pitch, ViewRotation.Yaw, ViewRotation.Roll,
                DistanceToCenter
            );
        }
    }

    if (HasCachedTerrainData())
    {
        int32 WaterCount = 0;
        int32 SandCount = 0;
        int32 GrassCount = 0;
        int32 DirtCount = 0;
        int32 RockCount = 0;
        int32 SnowCount = 0;
        int32 LavaCount = 0;

        for (int32 Y = 0; Y < CachedTerrainGridHeight; ++Y)
        {
            for (int32 X = 0; X < CachedTerrainGridWidth; ++X)
            {
                EWorldForgeTerrainType TerrainType = EWorldForgeTerrainType::Grass;
                if (GetTerrainTypeAtCell(FIntPoint(X, Y), TerrainType))
                {
                    switch (TerrainType)
                    {
                    case EWorldForgeTerrainType::Water: ++WaterCount; break;
                    case EWorldForgeTerrainType::Sand:  ++SandCount;  break;
                    case EWorldForgeTerrainType::Grass: ++GrassCount; break;
                    case EWorldForgeTerrainType::Dirt:  ++DirtCount;  break;
                    case EWorldForgeTerrainType::Rock:  ++RockCount;  break;
                    case EWorldForgeTerrainType::Snow:  ++SnowCount;  break;
                    case EWorldForgeTerrainType::Lava:  ++LavaCount;  break;
                    default: break;
                    }
                }
            }
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Terrain classification counts | Water=%d Sand=%d Grass=%d Dirt=%d Rock=%d Snow=%d Lava=%d"),
            WaterCount,
            SandCount,
            GrassCount,
            DirtCount,
            RockCount,
            SnowCount,
            LavaCount
        );
    }

    OnTerrainReady.Broadcast();

    // ── Resource Discovery: notify ResourceSpawnSubsystem that all terrain
    // tiles are ready for resource node spawning.
    NotifyResourceSpawnSystemTerrainReady();
}

// ── Resource Discovery: public accessors ──

const TArray<FWorldTileData>& UVoxelWorldManager::GetCachedWorldTiles() const
{
    // Return the snapshot's tile array (populated during BuildTerrainSnapshot).
    // If terrain hasn't been generated yet, this returns an empty array.
    return TerrainSnapshot.WorldTiles;
}

float UVoxelWorldManager::GetSurfaceHeightWorldAtTerrainCell(FIntPoint Cell) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(Cell))
    {
        return 0.0f;
    }

    const FIntPoint ClampedCell = ClampTerrainCellToBounds(Cell);
    const int32 Index = TerrainIndex(ClampedCell);

    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return 0.0f;
    }

    const float SurfaceVoxelZ = CachedSurfaceHeightsVoxels[Index];
    const float RVS = GetRenderVoxelSize();
    const FVector Origin = GetVoxelWorldOrigin();

    return Origin.Z + (SurfaceVoxelZ * RVS) + TerrainSurfaceZOffset;
}

// ── Resource Discovery: terrain-ready notification ──

void UVoxelWorldManager::NotifyResourceSpawnSystemTerrainReady()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    UResourceSpawnSubsystem* ResourceSpawn = World->GetSubsystem<UResourceSpawnSubsystem>();
    if (!ResourceSpawn)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[VoxelWorldManager] ResourceSpawnSubsystem not found — skipping resource node spawn. "
                "(Normal if resource system is not yet enabled.)"));
        return;
    }

    // ── Auto-configure Shatter Stone assets from TerrainSettings ────
    // This replaces the Blueprint-side SetOreDataTable / SetNodeBlueprintClass
    // calls.  The user picks the assets once in Project Settings → WorldForge
    // → Resources → ShatterStone, and this code wires them in automatically
    // before the first OnChunkActivated fires.
    {
        const UWorldForgeTerrainSettings* Settings = GetDefault<UWorldForgeTerrainSettings>();
        if (Settings)
        {
            // Load and register data table for all categories.
            if (!Settings->ShatterStoneDataTable.IsNull())
            {
                UDataTable* DT = Settings->ShatterStoneDataTable.LoadSynchronous();
                if (DT)
                {
                    for (int32 i = 0; i < static_cast<int32>(EResourceCategory::COUNT); ++i)
                    {
                        ResourceSpawn->SetOreDataTable(
                            static_cast<EResourceCategory>(i), DT);
                    }

                    UE_LOG(LogTemp, Log,
                        TEXT("[VoxelWorldManager] Auto-configured ShatterStone DataTable: %s"),
                        *DT->GetPathName());
                }
                else
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[VoxelWorldManager] ShatterStoneDataTable configured but failed to load. "
                            "Check Project Settings → WorldForge → Resources → ShatterStone."));
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[VoxelWorldManager] ShatterStoneDataTable not set. "
                        "No ore nodes will spawn. Set it in Project Settings → WorldForge → Resources → ShatterStone."));
            }

            // Load and register node Blueprint class.
            if (!Settings->ShatterStoneNodeClass.IsNull())
            {
                UClass* NodeClass = Settings->ShatterStoneNodeClass.LoadSynchronous();
                if (NodeClass)
                {
                    ResourceSpawn->SetNodeBlueprintClass(NodeClass);

                    UE_LOG(LogTemp, Log,
                        TEXT("[VoxelWorldManager] Auto-configured ShatterStone NodeClass: %s"),
                        *NodeClass->GetPathName());
                }
                else
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[VoxelWorldManager] ShatterStoneNodeClass configured but failed to load. "
                            "Check Project Settings → WorldForge → Resources → ShatterStone."));
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[VoxelWorldManager] ShatterStoneNodeClass not set. "
                        "No ore nodes will spawn. Set it in Project Settings → WorldForge → Resources → ShatterStone."));
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    // Wild Harvest Integration — Auto-configure WildHarvestAdapter
    //
    // Same pattern as Shatter Stone above.  The user picks the assets
    // once in Project Settings → WorldForge → Farming → WildHarvest,
    // and this code wires them into WildHarvestAdapter automatically.
    // ════════════════════════════════════════════════════════════════
    {
        UWildHarvestAdapter* WildHarvestAdapter = World->GetSubsystem<UWildHarvestAdapter>();
        if (WildHarvestAdapter)
        {
            const UWorldForgeTerrainSettings* WHSettings = GetDefault<UWorldForgeTerrainSettings>();
            if (WHSettings)
            {
                // Load and register DT_LowPoly data table.
                if (!WHSettings->WildHarvest_DT_LowPoly.IsNull())
                {
                    UDataTable* CropDT = WHSettings->WildHarvest_DT_LowPoly.LoadSynchronous();
                    if (CropDT)
                    {
                        WildHarvestAdapter->SetCropDataTable(CropDT);

                        UE_LOG(LogTemp, Log,
                            TEXT("[VoxelWorldManager] Auto-configured WildHarvest DataTable: %s"),
                            *CropDT->GetPathName());
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning,
                            TEXT("[VoxelWorldManager] WildHarvest_DT_LowPoly configured but failed to load. "
                                "Check Project Settings → WorldForge → Farming → WildHarvest."));
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Log,
                        TEXT("[VoxelWorldManager] WildHarvest_DT_LowPoly not set. "
                            "Farming will not function. Set it in Project Settings → WorldForge → Farming → WildHarvest."));
                }

                // Load and register crop patch Blueprint class.
                // Prefer the LowPoly sub-pack class; fall back to the master class.
                bool bClassSet = false;

                if (!WHSettings->WildHarvestCropPatchLowPolyClass.IsNull())
                {
                    UClass* PatchClass = WHSettings->WildHarvestCropPatchLowPolyClass.LoadSynchronous();
                    if (PatchClass)
                    {
                        WildHarvestAdapter->SetCropPatchClass(PatchClass);
                        bClassSet = true;

                        UE_LOG(LogTemp, Log,
                            TEXT("[VoxelWorldManager] Auto-configured WildHarvest CropPatchClass (LowPoly): %s"),
                            *PatchClass->GetPathName());
                    }
                }

                if (!bClassSet && !WHSettings->WildHarvestCropPatchMasterClass.IsNull())
                {
                    UClass* MasterClass = WHSettings->WildHarvestCropPatchMasterClass.LoadSynchronous();
                    if (MasterClass)
                    {
                        WildHarvestAdapter->SetCropPatchClass(MasterClass);
                        bClassSet = true;

                        UE_LOG(LogTemp, Log,
                            TEXT("[VoxelWorldManager] Auto-configured WildHarvest CropPatchClass (Master): %s"),
                            *MasterClass->GetPathName());
                    }
                }

                if (!bClassSet)
                {
                    UE_LOG(LogTemp, Log,
                        TEXT("[VoxelWorldManager] WildHarvest CropPatchClass not set. "
                            "Farming will not function. Set it in Project Settings → WorldForge → Farming → WildHarvest."));
                }
            }
        }
        else
        {
            UE_LOG(LogTemp, Log,
                TEXT("[VoxelWorldManager] WildHarvestAdapter not found — skipping farming auto-config."));
        }
    }

    const TArray<FWorldTileData>& Tiles = TerrainSnapshot.WorldTiles;
    const int32 GW = CachedTerrainGridWidth;
    const int32 GH = CachedTerrainGridHeight;

    if (Tiles.Num() != GW * GH || GW <= 0 || GH <= 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[VoxelWorldManager] NotifyResourceSpawnSystemTerrainReady — tile data invalid. "
                "Tiles=%d Grid=%dx%d"),
            Tiles.Num(), GW, GH);
        return;
    }

    int32 SnapshotHasVeinBoolCount = 0;
    int32 SnapshotHasPresentCategoriesCount = 0;
    int32 SnapshotHasSpawnSeedsCount = 0;
    int32 SnapshotHasDepthRangesCount = 0;
    int32 SnapshotHasAnyVeinDataCount = 0;

    for (const FWorldTileData& Tile : Tiles)
    {
        const bool bHasBool = Tile.VeinConfig.bHasVeins;
        const bool bHasPresentCategories = Tile.VeinConfig.PresentCategories.Num() > 0;
        const bool bHasSpawnSeeds = Tile.VeinConfig.SpawnSeeds.Num() > 0;
        const bool bHasDepthRanges = Tile.VeinConfig.DepthRanges.Num() > 0;

        if (bHasBool)
        {
            ++SnapshotHasVeinBoolCount;
        }

        if (bHasPresentCategories)
        {
            ++SnapshotHasPresentCategoriesCount;
        }

        if (bHasSpawnSeeds)
        {
            ++SnapshotHasSpawnSeedsCount;
        }

        if (bHasDepthRanges)
        {
            ++SnapshotHasDepthRangesCount;
        }

        if (bHasBool || bHasPresentCategories || bHasSpawnSeeds || bHasDepthRanges)
        {
            ++SnapshotHasAnyVeinDataCount;
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[VoxelWorldManager] Resource snapshot diagnostic: bHasVeins=%d PresentCategories=%d SpawnSeeds=%d DepthRanges=%d AnyVeinData=%d / %d"),
        SnapshotHasVeinBoolCount,
        SnapshotHasPresentCategoriesCount,
        SnapshotHasSpawnSeedsCount,
        SnapshotHasDepthRangesCount,
        SnapshotHasAnyVeinDataCount,
        Tiles.Num());

    int32 ActivatedCount = 0;
    int32 SkippedDueToActivationCap = 0;

    // TEMPORARY SAFETY CAP:
    // Do NOT spawn physical Shatter Stone actors for the entire world at terrain-ready.
    // On medium/large maps this can create tens of thousands of actors and freeze Unreal.
    // Long-term: spawn resource nodes only around active gameplay/settlement chunks.
    const int32 MaxResourceTilesToActivateAtTerrainReady = 128;

    for (int32 Y = 0; Y < GH; ++Y)
    {
        for (int32 X = 0; X < GW; ++X)
        {
            const int32 Index = (Y * GW) + X;
            const FWorldTileData& Tile = Tiles[Index];

            // Only activate tiles that have resource vein data seeded by Step08.
            // Do not rely only on bHasVeins because older pipeline code may populate
            // PresentCategories/SpawnSeeds/DepthRanges without setting the boolean.
            const bool bHasVeinData =
                Tile.VeinConfig.bHasVeins ||
                Tile.VeinConfig.PresentCategories.Num() > 0 ||
                Tile.VeinConfig.SpawnSeeds.Num() > 0 ||
                Tile.VeinConfig.VeinRichness.Num() > 0 ||
                Tile.VeinConfig.DepthRanges.Num() > 0;

            if (!bHasVeinData)
            {
                continue;
            }

            if (ActivatedCount >= MaxResourceTilesToActivateAtTerrainReady)
            {
                ++SkippedDueToActivationCap;
                continue;
            }

            const FIntPoint TileCell(X, Y);
            const float SurfaceHeightWorld = GetSurfaceHeightWorldAtTerrainCell(TileCell);

            ResourceSpawn->OnChunkActivated(
                TileCell,
                Tile,
                ChunkWorldSize,
                SurfaceHeightWorld
            );

            ++ActivatedCount;
        }
    }

    if (SkippedDueToActivationCap > 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[VoxelWorldManager] Resource spawn activation capped. Activated=%d Skipped=%d. "
                "This prevents editor freezes; long-term fix is settlement/chunk-based resource streaming."),
            ActivatedCount,
            SkippedDueToActivationCap);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[VoxelWorldManager] Resource spawn: activated %d / %d tiles with vein data."),
        ActivatedCount, Tiles.Num());
}

void UVoxelWorldManager::RequestBatchedTerrainRebuild()
{
    if (!bRebuildWorldAfterDigEdits)
    {
        // Phase 1.1: snapshot is updated for persistence, but we do NOT
        // destroy/recreate the whole voxel world per dig.
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    bBatchedTerrainRebuildPending = true;

    World->GetTimerManager().ClearTimer(BatchedTerrainRebuildTimerHandle);

    World->GetTimerManager().SetTimer(
        BatchedTerrainRebuildTimerHandle,
        this,
        &UVoxelWorldManager::ExecuteBatchedTerrainRebuild,
        DigFullRebuildDebounceSeconds,
        false
    );
}

void UVoxelWorldManager::ExecuteBatchedTerrainRebuild()
{
    if (!bBatchedTerrainRebuildPending)
    {
        return;
    }

    bBatchedTerrainRebuildPending = false;

    UE_LOG(LogTemp, Warning,
        TEXT("[VoxelWorldManager] Batched terrain rebuild. DirtyChunks=%d"),
        TerrainSnapshot.NumDirtyChunks());

    if (!RebuildVoxelWorldFromCurrentSnapshot())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[VoxelWorldManager] Batched terrain rebuild failed."));
    }
}

bool UVoxelWorldManager::RebuildVoxelWorldFromCurrentSnapshot()
{
#if WITH_VOXEL_PLUGIN
    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld || !HasTerrainSnapshot())
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] RebuildVoxelWorldFromCurrentSnapshot failed: missing VoxelWorld or terrain snapshot."));
        return false;
    }

    UWorldForgeTerrainGenerator* Generator = NewObject<UWorldForgeTerrainGenerator>(VoxelWorld);
    if (!Generator)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] RebuildVoxelWorldFromCurrentSnapshot failed: could not allocate generator."));
        return false;
    }

    Generator->SetTerrainSnapshot(TerrainSnapshot);
    Generator->SetSeed(1337);

    VoxelWorld->SetGeneratorObject(Generator);

    if (VoxelWorld->IsCreated())
    {
        VoxelWorld->DestroyWorld();
    }

    VoxelWorld->CreateWorld(FVoxelWorldCreateInfo());

    return VoxelWorld->IsCreated();
#else
    UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] RebuildVoxelWorldFromCurrentSnapshot failed: WITH_VOXEL_PLUGIN disabled."));
    return false;
#endif
}

// ─── [TERRAIN APPLICATION] ───────────────────────────────────
// These functions are intentionally thin — terrain density is
// now fully owned by UWorldForgeTerrainGenerator::GetValueImpl().

bool UVoxelWorldManager::ApplyBiomeChunkNative(
    AVoxelWorld* VoxelWorld,
    FIntPoint ChunkCoord,
    const FVoxelBiomeProfile& Profile,
    float TileElevation)
{
    if (!VoxelWorld)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyBiomeChunkNative skipped: VoxelWorld is null."));
        return false;
    }

    if (!VoxelWorld->IsCreated())
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyBiomeChunkNative skipped: VoxelWorld runtime is not created yet."));
        return false;
    }

    if (!HasTerrainSnapshot())
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyBiomeChunkNative skipped: no valid terrain snapshot is available."));
        return false;
    }

    if (!ValidateSmoothSDF(Profile))
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyBiomeChunkNative skipped: biome profile is not Smooth SDF."));
        return false;
    }

    if (bLogTerrainGeneration && ((ChunkCoord.X + ChunkCoord.Y) % 64 == 0))
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[VoxelWorldManager] ApplyBiomeChunkNative is now generator-driven. Chunk=(%d,%d) Elev=%.3f"),
            ChunkCoord.X,
            ChunkCoord.Y,
            TileElevation
        );
    }

    // Density generation is handled by UWorldForgeTerrainGenerator through the voxel world generator path.
    return true;
}

bool UVoxelWorldManager::ApplyGlobalFoundationNative(AVoxelWorld* VoxelWorld, int32 GridWidth, int32 GridHeight)
{
    if (!VoxelWorld)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyGlobalFoundationNative skipped: VoxelWorld is null."));
        return false;
    }

    if (!VoxelWorld->IsCreated())
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyGlobalFoundationNative skipped: VoxelWorld runtime is not created yet."));
        return false;
    }

    if (!HasTerrainSnapshot())
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyGlobalFoundationNative skipped: no valid terrain snapshot is available."));
        return false;
    }

    if (bLogTerrainGeneration)
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[VoxelWorldManager] ApplyGlobalFoundationNative is now generator-driven. Grid=%dx%d"),
            GridWidth,
            GridHeight
        );
    }

    // Foundation is part of the density field in UWorldForgeTerrainGenerator::GetValueImpl().
    return true;
}



// ─── [PHASE 1: DIG API] ─────────────────────────────────────

bool UVoxelWorldManager::IsValidGridPosition(const FIntVector& GridPos) const
{
    if (!HasCachedTerrainData())
    {
        return false;
    }

    const FIntPoint VoxelXY(GridPos.X, GridPos.Y);
    const FIntPoint TerrainCell = VoxelCellToChunkCell(VoxelXY);

    if (!IsValidTerrainCell(TerrainCell))
    {
        return false;
    }

    int32 SurfaceZ = 0;
    if (!TryGetTerrainSurfaceVoxelZ(VoxelXY, SurfaceZ))
    {
        return false;
    }

    const int32 MinZ = FMath::FloorToInt(
        -FMath::Max(0.0f, GlobalFoundationDepthWorld) / GetRenderVoxelSize()
    );

    return GridPos.Z >= MinZ && GridPos.Z <= SurfaceZ;
}

bool UVoxelWorldManager::TryFindTopSolidBlockInColumn(
    const FIntPoint& GridXY,
    FIntVector& OutBlock,
    int32 MaxSearchDown) const
{
    int32 SurfaceZ = 0;
    if (!TryGetTerrainSurfaceVoxelZ(GridXY, SurfaceZ))
    {
        return false;
    }

    for (int32 Z = SurfaceZ; Z >= SurfaceZ - MaxSearchDown; --Z)
    {
        const FIntVector Pos(GridXY.X, GridXY.Y, Z);
        const FGameplayTag Type = GetBlockTypeAt(Pos);

        if (Type.IsValid() && Type != TAG_Terrain_Bedrock)
        {
            OutBlock = Pos;
            return true;
        }
    }

    return false;
}

bool UVoxelWorldManager::TryFindSurfaceBlockNearZ(
    const FIntPoint& GridXY,
    int32 TargetZ,
    int32 VerticalSearchRadius,
    FIntVector& OutBlock) const
{
    if (!HasCachedTerrainData())
    {
        return false;
    }

    const int32 SearchRadius = FMath::Max(VerticalSearchRadius, 0);

    const int32 MinZ = FMath::FloorToInt(
        -FMath::Max(0.0f, GlobalFoundationDepthWorld) / GetRenderVoxelSize()
    );

    int32 ColumnSurfaceZ = 0;
    if (!TryGetTerrainSurfaceVoxelZ(GridXY, ColumnSurfaceZ))
    {
        return false;
    }

    const int32 StartZ = FMath::Min(TargetZ + SearchRadius, ColumnSurfaceZ);
    const int32 EndZ = FMath::Max(TargetZ - SearchRadius, MinZ);

    for (int32 Z = StartZ; Z >= EndZ; --Z)
    {
        const FIntVector Pos(GridXY.X, GridXY.Y, Z);
        const FGameplayTag Type = GetBlockTypeAt(Pos);

        if (!Type.IsValid() || Type == TAG_Terrain_Bedrock)
        {
            continue;
        }

        const FIntVector AbovePos(GridXY.X, GridXY.Y, Z + 1);
        const FGameplayTag AboveType = GetBlockTypeAt(AbovePos);

        if (!AboveType.IsValid())
        {
            OutBlock = Pos;
            return true;
        }
    }

    return false;
}

FGameplayTag UVoxelWorldManager::GetBlockTypeAt(const FIntVector& GridPos) const
{
    if (!IsValidGridPosition(GridPos))
    {
        return FGameplayTag();
    }

    // Exact voxel cube mining removes this one cell with no SDF falloff.
    if (RuntimeRemovedVoxels.Contains(GridPos))
    {
        return FGameplayTag();
    }

    // v5.0: Check if this position has been carved by a modification brush
    const float ModDensity = TerrainSnapshot.SampleModificationDensity(
        static_cast<float>(GridPos.X),
        static_cast<float>(GridPos.Y),
        static_cast<float>(GridPos.Z));
    if (ModDensity > 0.5f)
    {
        return FGameplayTag();  // carved to air
    }

    const FIntPoint VoxelXY(GridPos.X, GridPos.Y);

    int32 SurfaceZ = 0;
    if (!TryGetTerrainSurfaceVoxelZ(VoxelXY, SurfaceZ))
    {
        return FGameplayTag();
    }

    if (GridPos.Z > SurfaceZ)
    {
        return FGameplayTag();
    }

    const int32 MinZ = FMath::FloorToInt(
        -FMath::Max(0.0f, GlobalFoundationDepthWorld) / GetRenderVoxelSize()
    );

    if (GridPos.Z <= MinZ)
    {
        return TAG_Terrain_Bedrock;
    }

    EWorldForgeTerrainType TerrainType = EWorldForgeTerrainType::Grass;
    if (!GetTerrainTypeAtVoxelSample(VoxelXY, TerrainType))
    {
        return TAG_Terrain_Stone;
    }

    switch (TerrainType)
    {
    case EWorldForgeTerrainType::Dirt:
    case EWorldForgeTerrainType::Grass:
        return TAG_Terrain_Dirt;

    case EWorldForgeTerrainType::Rock:
    case EWorldForgeTerrainType::Snow:
    case EWorldForgeTerrainType::Lava:
        return TAG_Terrain_Stone;

    case EWorldForgeTerrainType::Sand:
    case EWorldForgeTerrainType::Water:
        return TAG_Terrain_Sand;

    default:
        return TAG_Terrain_Stone;
    }
}


bool UVoxelWorldManager::GetTerrainMaterialLayerAtVoxel(
    const FIntVector& GridPos,
    int32& OutMaterialLayer,
    FGameplayTag& OutBlockType) const
{
    // Keep these numeric indices in sync with WorldForgeTerrainGenerator.h
    constexpr int32 TerrainLayer_Sand = 0;
    constexpr int32 TerrainLayer_Grass = 1;
    constexpr int32 TerrainLayer_Dirt = 2;
    constexpr int32 TerrainLayer_Rock = 3;
    constexpr int32 TerrainLayer_Snow = 4;
    constexpr int32 TerrainLayer_Water = 5;
    constexpr int32 TerrainLayer_Lava = 6;
    constexpr int32 TerrainLayer_Basalt = 7;
    constexpr int32 TerrainLayer_DeepRock = 8;

    OutMaterialLayer = TerrainLayer_Dirt;
    OutBlockType = FGameplayTag();

    if (!IsValidGridPosition(GridPos))
    {
        return false;
    }

    OutBlockType = GetBlockTypeAt(GridPos);
    if (!OutBlockType.IsValid())
    {
        return false;
    }

    if (OutBlockType == TAG_Terrain_Bedrock)
    {
        OutMaterialLayer = TerrainLayer_DeepRock;
        return true;
    }

    const FIntPoint VoxelXY(GridPos.X, GridPos.Y);

    int32 SurfaceZ = 0;
    if (!TryGetTerrainSurfaceVoxelZ(VoxelXY, SurfaceZ))
    {
        return false;
    }

    const int32 DepthBelowSurface = FMath::Max(0, SurfaceZ - GridPos.Z);

    // Match the terrain generator's underground special biome handling.
    // This allows the overlay/slice helper to visually line up with MC_WorldForge_Terrain.
    if (HasCachedTerrainData())
    {
        const FIntPoint ChunkCell = ClampTerrainCellToBounds(VoxelCellToChunkCell(VoxelXY));
        const int32 ChunkIndex = TerrainIndex(ChunkCell);
        if (CachedBiomeTags.IsValidIndex(ChunkIndex))
        {
            const FGameplayTag& BiomeTag = CachedBiomeTags[ChunkIndex];
            const FGameplayTag LavaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);
            const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);

            if (LavaTag.IsValid() && BiomeTag.MatchesTag(LavaTag))
            {
                OutMaterialLayer = TerrainLayer_Lava;
                return true;
            }

            if (VolcanicTag.IsValid() && BiomeTag.MatchesTag(VolcanicTag))
            {
                OutMaterialLayer = TerrainLayer_Basalt;
                return true;
            }
        }
    }

    EWorldForgeTerrainType TerrainType = EWorldForgeTerrainType::Grass;
    if (!GetTerrainTypeAtVoxelSample(VoxelXY, TerrainType))
    {
        TerrainType = EWorldForgeTerrainType::Rock;
    }

    // These depth bands mirror FWorldForgeTerrainGeneratorInstance::ClassifyUndergroundMaterial.
    if (DepthBelowSurface <= 4)
    {
        switch (TerrainType)
        {
        case EWorldForgeTerrainType::Water:
            OutMaterialLayer = TerrainLayer_Water;
            break;
        case EWorldForgeTerrainType::Sand:
            OutMaterialLayer = TerrainLayer_Sand;
            break;
        case EWorldForgeTerrainType::Snow:
            OutMaterialLayer = TerrainLayer_Dirt;
            break;
        case EWorldForgeTerrainType::Rock:
            OutMaterialLayer = TerrainLayer_Rock;
            break;
        case EWorldForgeTerrainType::Lava:
            OutMaterialLayer = TerrainLayer_Lava;
            break;
        case EWorldForgeTerrainType::Dirt:
            OutMaterialLayer = TerrainLayer_Dirt;
            break;
        case EWorldForgeTerrainType::Grass:
        default:
            OutMaterialLayer = TerrainLayer_Dirt;
            break;
        }
        return true;
    }

    if (DepthBelowSurface <= 20)
    {
        OutMaterialLayer = TerrainLayer_Rock;
        return true;
    }

    OutMaterialLayer = TerrainLayer_DeepRock;
    return true;
}

bool UVoxelWorldManager::RemoveBlockAt(const FIntVector& GridPos)
{
    UE_LOG(LogTemp, Warning,
        TEXT("[VoxelWorldManager] RemoveBlockAt called Grid=%s"),
        *GridPos.ToString());

    if (!IsValidGridPosition(GridPos))
    {
        return false;
    }

    const FGameplayTag BlockType = GetBlockTypeAt(GridPos);
    if (!BlockType.IsValid() || BlockType == TAG_Terrain_Bedrock)
    {
        return false;
    }

    // ── v5.0: Create SDF brush and add to the modification map ──
    FSDFBrush DigBrush = MakeDigBrush(GridPos);

    // Add to the snapshot's modification map (single source of truth).
    // The brush is stored in all chunks its AABB overlaps.
    const int64 BrushID = TerrainSnapshot.AddModificationBrush(DigBrush);

    // Apply the same brush geometry to the live Voxel Plugin data
    // for immediate visual feedback (no generator rebuild needed).
    DigBrush.BrushID = BrushID;
    ApplyRuntimeBrush(DigBrush);

    RemoveRuntimeDigHoleActor(GridPos);

    const FVector WorldPosition = GridToWorld(FIntPoint(GridPos.X, GridPos.Y), GridPos.Z);
    OnVoxelDigged(WorldPosition);

    UE_LOG(LogTemp, Warning,
        TEXT("[DigTrace][VoxelWorldManager] RemoveBlockAt SUCCESS Grid=%s Type=%s BrushID=%lld DirtyChunks=%d"),
        *GridPos.ToString(),
        *BlockType.ToString(),
        BrushID,
        TerrainSnapshot.NumDirtyChunks());

    return true;
}



bool UVoxelWorldManager::RemoveSingleVoxelAt(const FIntVector& GridPos)
{
    UE_LOG(LogTemp, Warning,
        TEXT("[VoxelWorldManager] RemoveSingleVoxelAt called Grid=%s"),
        *GridPos.ToString());

    if (!IsValidGridPosition(GridPos))
    {
        return false;
    }

    const FGameplayTag BlockType = GetBlockTypeAt(GridPos);
    if (!BlockType.IsValid() || BlockType == TAG_Terrain_Bedrock)
    {
        return false;
    }

    RuntimeRemovedVoxels.Add(GridPos);

    const bool bApplied = ApplyRuntimeVoxelRemoval(GridPos);

    RemoveRuntimeDigHoleActor(GridPos);

    const FVector WorldPosition = GridToWorld(FIntPoint(GridPos.X, GridPos.Y), GridPos.Z);
    OnVoxelDigged(WorldPosition);

    UE_LOG(LogTemp, Warning,
        TEXT("[VoxelWorldManager] RemoveSingleVoxelAt SUCCESS Grid=%s Type=%s RuntimeApplied=%s RemovedVoxels=%d"),
        *GridPos.ToString(),
        *BlockType.ToString(),
        bApplied ? TEXT("true") : TEXT("false"),
        RuntimeRemovedVoxels.Num());

    return true;
}

bool UVoxelWorldManager::RemoveVoxelByMode(const FIntVector& GridPos, EMiningRemovalMode RemovalMode)
{
    switch (RemovalMode)
    {
    case EMiningRemovalMode::VoxelCube:
        return RemoveSingleVoxelAt(GridPos);

    case EMiningRemovalMode::SDFBrush:
        return RemoveBlockAt(GridPos);

    default:
        return RemoveSingleVoxelAt(GridPos);
    }
}


bool UVoxelWorldManager::RestoreBlockAt(const FIntVector& GridPos, const FGameplayTag& BlockType)
{
    if (!BlockType.IsValid())
    {
        return false;
    }

    const bool bHadExactVoxelRemoval = RuntimeRemovedVoxels.Remove(GridPos) > 0;
    if (bHadExactVoxelRemoval)
    {
        RefreshEditedVoxel(GridPos);

        UE_LOG(LogTemp, Log,
            TEXT("[VoxelWorldManager] RestoreBlockAt exact voxel SUCCESS Grid=%s Type=%s RemovedVoxels=%d"),
            *GridPos.ToString(),
            *BlockType.ToString(),
            RuntimeRemovedVoxels.Num());

        return true;
    }

    // ── v5.0: Remove all brushes near this grid position ──
    const FVector VoxelPos(
        static_cast<float>(GridPos.X),
        static_cast<float>(GridPos.Y),
        static_cast<float>(GridPos.Z)
    );

    const int32 NumRemoved = TerrainSnapshot.RemoveModificationsNear(VoxelPos, 3.0f);

    if (NumRemoved == 0)
    {
        return false;
    }

    RefreshEditedVoxel(GridPos);

    UE_LOG(LogTemp, Log,
        TEXT("[VoxelWorldManager] RestoreBlockAt SUCCESS Grid=%s Type=%s RemovedBrushes=%d DirtyChunks=%d"),
        *GridPos.ToString(),
        *BlockType.ToString(),
        NumRemoved,
        TerrainSnapshot.NumDirtyChunks());

    return true;
}

bool UVoxelWorldManager::ApplyRuntimeVoxelRemoval(const FIntVector& GridPos)
{
#if WITH_VOXEL_PLUGIN
    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld || !VoxelWorld->IsCreated())
    {
        return false;
    }

    FVoxelData& Data = VoxelWorld->GetData();

    const FVoxelIntBox Bounds(
        GridPos - FIntVector(1, 1, 1),
        GridPos + FIntVector(2, 2, 2)
    );

    FVoxelWriteScopeLock Lock(Data, Bounds, FUNCTION_FNAME);

    // Exact mining path: remove only this single voxel cell.
    Data.SetValue(GridPos.X, GridPos.Y, GridPos.Z, FVoxelValue::Empty());

    VoxelWorld->GetLODManager().UpdateBounds(Bounds);

    return true;
#else
    return false;
#endif
}

bool UVoxelWorldManager::ApplyRuntimeVoxelRestore(const FIntVector& GridPos)
{
#if WITH_VOXEL_PLUGIN
    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld || !VoxelWorld->IsCreated())
    {
        return false;
    }

    FVoxelData& Data = VoxelWorld->GetData();

    const FVoxelIntBox Bounds(
        GridPos - FIntVector(2, 2, 2),
        GridPos + FIntVector(2, 2, 2)
    );

    FVoxelWriteScopeLock Lock(Data, Bounds, FUNCTION_FNAME);

    // Restore = set back to "full" density
    Data.SetValue(GridPos.X, GridPos.Y, GridPos.Z, FVoxelValue::Full());

    VoxelWorld->GetLODManager().UpdateBounds(Bounds);

    return true;
#else
    return false;
#endif
}

// ── v5.0: Apply SDF brush to live Voxel Plugin data ──
// Brush geometry matches what GetValueImpl will compute on regeneration,
// so runtime edits and generator output agree exactly.
bool UVoxelWorldManager::ApplyRuntimeBrush(const FSDFBrush& Brush)
{
#if WITH_VOXEL_PLUGIN
    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld || !VoxelWorld->IsCreated())
    {
        return false;
    }

    FVoxelData& Data = VoxelWorld->GetData();

    const FBox BrushBounds = Brush.GetBounds();

    const FVoxelIntBox Bounds(
        FIntVector(
            FMath::FloorToInt(BrushBounds.Min.X) - 1,
            FMath::FloorToInt(BrushBounds.Min.Y) - 1,
            FMath::FloorToInt(BrushBounds.Min.Z) - 1
        ),
        FIntVector(
            FMath::CeilToInt(BrushBounds.Max.X) + 2,
            FMath::CeilToInt(BrushBounds.Max.Y) + 2,
            FMath::CeilToInt(BrushBounds.Max.Z) + 2
        )
    );

    FVoxelWriteScopeLock Lock(Data, Bounds, FUNCTION_FNAME);

    for (int32 X = Bounds.Min.X; X < Bounds.Max.X; ++X)
    {
        for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; ++Y)
        {
            for (int32 Z = Bounds.Min.Z; Z < Bounds.Max.Z; ++Z)
            {
                const float BrushValue = Brush.Evaluate(
                    static_cast<float>(X),
                    static_cast<float>(Y),
                    static_cast<float>(Z)
                );

                if (FMath::Abs(BrushValue) > KINDA_SMALL_NUMBER)
                {
                    const FVoxelValue Current = Data.GetValue(X, Y, Z, 0);
                    const float CurrentF = Current.ToFloat();
                    const float NewF = FMath::Clamp(CurrentF + BrushValue, -1.0f, 1.0f);
                    Data.SetValue(X, Y, Z, FVoxelValue(NewF));
                }
            }
        }
    }

    VoxelWorld->GetLODManager().UpdateBounds(Bounds);
    return true;

#else
    return false;
#endif
}

int32 UVoxelWorldManager::GetSurfaceZAt(const FIntPoint& GridXY) const
{
    int32 SurfaceZ = 0;

    if (!TryGetTerrainSurfaceVoxelZ(GridXY, SurfaceZ))
    {
        return 0;
    }

    return SurfaceZ;
}

int32 UVoxelWorldManager::GetTerrainDepthAt(const FIntPoint& GridXY) const
{
    const int32 SurfaceZ = GetSurfaceZAt(GridXY);

    const int32 MinZ = FMath::FloorToInt(
        -FMath::Max(0.0f, GlobalFoundationDepthWorld) / GetRenderVoxelSize()
    );

    return FMath::Max(0, SurfaceZ - MinZ + 1);
}

// ── v5.0: Create an SDF brush for a standard dig operation ──
FSDFBrush UVoxelWorldManager::MakeDigBrush(const FIntVector& GridPos) const
{
    FSDFBrush Brush;

    // Default mining must remove one selected 100uu voxel at a time.
    // The previous sphere brush used Radius=2.0 / HalfHeight=2.5, which could
    // carve a large crater around a single designation. Keep the SDF/runtime
    // modification path, but clamp the normal mining brush to a tight single
    // voxel footprint. Larger organic/metaball excavation should be added later
    // as an explicit brush mode, not the default Mine action.
    Brush.Shape = ESDFBrushShape::Box;
    Brush.Mode = ESDFBrushMode::Carve;
    Brush.Center = FVector(
        static_cast<float>(GridPos.X),
        static_cast<float>(GridPos.Y),
        static_cast<float>(GridPos.Z)
    );
    Brush.Radius = 0.51f;
    Brush.HalfHeight = 0.51f;
    Brush.Strength = 1.0f;
    Brush.FalloffVoxels = 0.0f;
    return Brush;
}

void UVoxelWorldManager::RefreshEditedVoxel(const FIntVector& GridPos)
{
    // v5.0: On restore, write Full() density back and let the next
    // LOD update regenerate from the generator (which no longer has
    // the removed brush in its snapshot).
    ApplyRuntimeVoxelRestore(GridPos);
    RemoveRuntimeDigHoleActor(GridPos);
}

AStaticMeshActor* UVoxelWorldManager::SpawnRuntimeDigHoleActor(const FIntVector& GridPos)
{
    return nullptr;
}

void UVoxelWorldManager::RemoveRuntimeDigHoleActor(const FIntVector& GridPos)
{
    TObjectPtr<AStaticMeshActor>* FoundActor = RuntimeDigHoleActors.Find(GridPos);
    if (!FoundActor)
    {
        return;
    }

    if (IsValid(*FoundActor))
    {
        (*FoundActor)->Destroy();
    }

    RuntimeDigHoleActors.Remove(GridPos);
}

// ─── [PLACED BLOCK VISUALS] ──────────────────────────────────

void UVoxelWorldManager::PlaceVoxelBlock(FVector WorldPosition, FGameplayTag MaterialTag)
{
    const FIntVector GridCell3D = WorldToGrid3D(WorldPosition);
    const FVector VisualWorldPosition = ResolvePlacedBlockVisualWorldPosition(WorldPosition);

    if (bUseFallbackPlacedBlockVisuals)
    {
        if (TObjectPtr<AStaticMeshActor>* ExistingActor = FallbackPlacedBlockActors.Find(GridCell3D))
        {
            if (IsValid(*ExistingActor))
            {
                (*ExistingActor)->SetActorLocation(VisualWorldPosition);
            }
            else
            {
                FallbackPlacedBlockActors.Remove(GridCell3D);
                if (AStaticMeshActor* NewActor = SpawnFallbackPlacedBlockActor(GridCell3D, VisualWorldPosition, MaterialTag))
                {
                    FallbackPlacedBlockActors.Add(GridCell3D, NewActor);
                }
            }
        }
        else
        {
            if (AStaticMeshActor* NewActor = SpawnFallbackPlacedBlockActor(GridCell3D, VisualWorldPosition, MaterialTag))
            {
                FallbackPlacedBlockActors.Add(GridCell3D, NewActor);
            }
        }
    }

    if (bLogPlacedBlockVisuals)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] PlaceVoxelBlock Grid=(%d,%d,%d) Requested=(%.1f,%.1f,%.1f) Visual=(%.1f,%.1f,%.1f) Material=%s"),
            GridCell3D.X, GridCell3D.Y, GridCell3D.Z,
            WorldPosition.X, WorldPosition.Y, WorldPosition.Z,
            VisualWorldPosition.X, VisualWorldPosition.Y, VisualWorldPosition.Z,
            *MaterialTag.ToString()
        );
    }

    OnVoxelBlockPlaced(VisualWorldPosition, MaterialTag);
}

FVector UVoxelWorldManager::ResolvePlacedBlockVisualWorldPosition(const FVector& RequestedWorldPosition) const
{
    FVector Result = RequestedWorldPosition;
    Result.Z += FallbackPlacedBlockVerticalNudge;
    return Result;
}

AStaticMeshActor* UVoxelWorldManager::SpawnFallbackPlacedBlockActor(
    const FIntVector& GridCell3D,
    const FVector& SpawnWorldPosition,
    FGameplayTag MaterialTag)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    static UStaticMesh* CubeMesh = nullptr;
    if (!CubeMesh)
    {
        CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    }

    if (!CubeMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] Failed to load fallback cube mesh for placed block visuals."));
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Name = NAME_None;

    AStaticMeshActor* BlockActor = World->SpawnActor<AStaticMeshActor>(
        SpawnWorldPosition,
        FRotator::ZeroRotator,
        SpawnParams
    );

    if (!BlockActor)
    {
        return nullptr;
    }

    UStaticMeshComponent* MeshComp = BlockActor->GetStaticMeshComponent();
    if (!MeshComp)
    {
        BlockActor->Destroy();
        return nullptr;
    }

    MeshComp->SetMobility(EComponentMobility::Movable);
    MeshComp->SetStaticMesh(CubeMesh);
    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
    MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    MeshComp->SetGenerateOverlapEvents(false);

    const float RVS = GetRenderVoxelSize();
    MeshComp->SetWorldScale3D(FVector(RVS / 100.0f, RVS / 100.0f, RVS / 100.0f));
    MeshComp->SetCastShadow(true);

    BlockActor->SetActorEnableCollision(true);
    BlockActor->Tags.Add(FName(TEXT("WorldForgePlacedBlock")));
    BlockActor->Tags.Add(FName(*MaterialTag.ToString()));

    return BlockActor;
}

void UVoxelWorldManager::ClearPlacedBlockVisuals()
{
    for (TPair<FIntVector, TObjectPtr<AStaticMeshActor>>& Pair : FallbackPlacedBlockActors)
    {
        if (IsValid(Pair.Value))
        {
            Pair.Value->Destroy();
        }
    }

    for (TPair<FIntVector, TObjectPtr<AStaticMeshActor>>& Pair : RuntimeDigHoleActors)
    {
        if (IsValid(Pair.Value))
        {
            Pair.Value->Destroy();
        }
    }

    RuntimeDigHoleActors.Reset();

    FallbackPlacedBlockActors.Reset();
}