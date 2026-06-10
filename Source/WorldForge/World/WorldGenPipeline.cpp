// WorldGenPipeline.cpp — v3.2
// ════════════════════════════════════════════════════════════════════
// CHANGELOG v3.1:
//   Step02: Thresholded noise + channel carving (no circular seeds)
//   Step03: Stronger mountain stamp, river generation, lake placement,
//           canyon carving, volcano placement (kept from v3.0)
//   Step04: River/Lake/Canyon/Lava biome tags, improved Whittaker diagram
// ════════════════════════════════════════════════════════════════════

#include "WorldGenPipeline.h"
#include "../Core/WorldForgeRNG.h"
#include "GameplayTagContainer.h"
#include "../Civilization/CivilizationGenerator.h"
#include "../Civilization/HistoricalSimulation.h"
#include "../Civilization/WorldHistory/WorldHistoryGenerator.h"
#include "../Civilization/WorldHistory/WorldHistoryContext.h"
#include "../Economy/WorldEconomySimulation.h"
#include "../Cosmic/CosmicSimulationSubsystem.h"
#include "WorldMapSubsystem.h"
#include "VoxelWorldManager.h"
#include "../Lore/WorldLoreTypes.h"
#include "../Lore/LoreGenerationContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    static FString CleanNameArtifacts(const FString& Input)
    {
        FString Result = Input;
        for (int32 i = 0; i < Result.Len() - 2; ++i)
        {
            if (Result[i] == Result[i + 1] && Result[i] == Result[i + 2])
            {
                Result.RemoveAt(i + 2);
                --i;
            }
        }
        for (int32 i = 0; i < Result.Len() - 3; ++i)
        {
            if (Result[i] == Result[i + 2] && Result[i + 1] == Result[i + 3])
            {
                Result.RemoveAt(i + 2, 2);
                --i;
            }
        }
        Result.ReplaceInline(TEXT("ririr"), TEXT("rir"));
        Result.ReplaceInline(TEXT("lalal"), TEXT("lal"));
        return Result;
    }

    static FVector2D Rotate2D(const FVector2D& Vec, float Degrees)
    {
        const float Rad = FMath::DegreesToRadians(Degrees);
        const float C = FMath::Cos(Rad);
        const float S = FMath::Sin(Rad);
        return FVector2D(Vec.X * C - Vec.Y * S, Vec.X * S + Vec.Y * C);
    }

    static float DistanceToSegment2D(const FVector2D& P, const FVector2D& A, const FVector2D& B)
    {
        const FVector2D AB = B - A;
        const float Denom = FVector2D::DotProduct(AB, AB);
        if (Denom <= KINDA_SMALL_NUMBER) return FVector2D::Distance(P, A);
        const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / Denom, 0.f, 1.f);
        return FVector2D::Distance(P, A + AB * T);
    }

    static float GaussianLikeFalloff(float NormalizedDistance, float Sharpness)
    {
        return FMath::Exp(-NormalizedDistance * NormalizedDistance * Sharpness);
    }

    static float CalculateMountainContributionAtTile(
        const FVector2D& Tile,
        const TArray<FMountainRange>& Ranges,
        float FalloffSharpness)
    {
        float Total = 0.f;
        for (const FMountainRange& Range : Ranges)
        {
            const FVector2D End = Range.Start + Range.Direction * Range.Length;
            const float Distance = DistanceToSegment2D(Tile, Range.Start, End);
            const float RidgeNorm = Distance / FMath::Max(Range.Width, 1.f);
            const float Ridge = GaussianLikeFalloff(RidgeNorm, FalloffSharpness) * Range.HeightMultiplier;
            float Basin = 0.f;
            if (Range.BasinWidth > KINDA_SMALL_NUMBER && Range.BasinStrength > KINDA_SMALL_NUMBER)
            {
                const float BasinDistance = FMath::Abs(Distance - Range.BasinOffset);
                const float BasinNorm = BasinDistance / Range.BasinWidth;
                Basin = GaussianLikeFalloff(BasinNorm, FalloffSharpness) * Range.BasinStrength;
            }
            Total += (Ridge - Basin);
        }
        return Total;
    }

    static float PerlinFBM2D(float X, float Y, int32 Octaves, float Persistence, float Lacunarity)
    {
        float Sum = 0.0f, Amplitude = 1.0f, Frequency = 1.0f, MaxVal = 0.0f;
        for (int32 Oct = 0; Oct < Octaves; ++Oct)
        {
            Sum += FMath::PerlinNoise2D(FVector2D(X * Frequency, Y * Frequency)) * Amplitude;
            MaxVal += Amplitude;
            Amplitude *= Persistence;
            Frequency *= Lacunarity;
        }
        return (MaxVal > KINDA_SMALL_NUMBER) ? (Sum / MaxVal) : 0.0f;
    }

    constexpr float Step03SeaThreshold = 0.48f;
    constexpr float Step03CoastalThreshold = 0.515f;
    constexpr float Step03RiverLakeDepthThreshold = 0.15f;
    constexpr float Step03RiverMinDepth = 0.05f;
    constexpr float Step03RiverMaxDepth = 0.12f;
    constexpr float Step03RiverChannelDepression = 0.008f;
    constexpr float Step03RiverMinorUphillAllowance = 0.035f;

    static const FIntPoint GRiverNeighborOffsets[] =
    {
        FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(0, 1),
        FIntPoint(-1, -1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(1, 1)
    };

    struct FRiverSourceCandidate
    {
        FIntPoint Tile = FIntPoint::ZeroValue;
        float Score = 0.0f;
    };

    struct FRiverSourceDiagnostics
    {
        int32 SourceCandidates = 0;
        int32 Accepted = 0;
        int32 RejectedNoDrainage = 0;
        int32 RejectedWater = 0;
        int32 RejectedBiome = 0;
    };

    struct FDrainageSearchResult
    {
        bool bFound = false;
        FIntPoint Target = FIntPoint(-1, -1);
        FString Reason = TEXT("None");
    };

    enum class ERiverOutletKind : uint8
    {
        None,
        Coast,
        Sea,
        Lake,
        RiverMerge,
        LakeEndpoint,
        Loop,
        MaxLength
    };

    static bool IsInGrid(const FIntPoint& Point, int32 GridWidth, int32 GridHeight)
    {
        return Point.X >= 0 && Point.X < GridWidth && Point.Y >= 0 && Point.Y < GridHeight;
    }

    static bool IsVolcanicRiverBlocked(const FIntPoint& Point, const TArray<FVolcanoData>& Volcanoes)
    {
        const FVector2D TilePos(static_cast<float>(Point.X), static_cast<float>(Point.Y));
        for (const FVolcanoData& Volcano : Volcanoes)
        {
            const float ExclusionRadius = Volcano.bActive
                ? FMath::Max(Volcano.ConeRadius, Volcano.LavaFlowRadius)
                : Volcano.ConeRadius;
            if (FVector2D::Distance(TilePos, Volcano.Center) <= ExclusionRadius)
            {
                return true;
            }
        }
        return false;
    }

    static bool IsCoastalRiverOutlet(
        const FIntPoint& Point,
        const TArray<FWorldTileData>& WorldTiles,
        int32 GridWidth,
        int32 GridHeight)
    {
        if (!IsInGrid(Point, GridWidth, GridHeight))
        {
            return false;
        }

        const FWorldTileData& Tile = WorldTiles[Point.Y * GridWidth + Point.X];
        if (Tile.Elevation < Step03SeaThreshold || Tile.Elevation > Step03CoastalThreshold)
        {
            return false;
        }

        for (const FIntPoint& Offset : GRiverNeighborOffsets)
        {
            const FIntPoint Neighbor(Point.X + Offset.X, Point.Y + Offset.Y);
            if (!IsInGrid(Neighbor, GridWidth, GridHeight))
            {
                continue;
            }

            if (WorldTiles[Neighbor.Y * GridWidth + Neighbor.X].Elevation < Step03SeaThreshold)
            {
                return true;
            }
        }

        return false;
    }

    static int32 FindAdjacentSeaTileIndex(
        const FIntPoint& Point,
        const TArray<FWorldTileData>& WorldTiles,
        int32 GridWidth,
        int32 GridHeight)
    {
        for (const FIntPoint& Offset : GRiverNeighborOffsets)
        {
            const FIntPoint Neighbor(Point.X + Offset.X, Point.Y + Offset.Y);
            if (!IsInGrid(Neighbor, GridWidth, GridHeight))
            {
                continue;
            }

            if (WorldTiles[Neighbor.Y * GridWidth + Neighbor.X].Elevation < Step03SeaThreshold)
            {
                return Neighbor.Y * GridWidth + Neighbor.X;
            }
        }

        return INDEX_NONE;
    }

    static float ComputeRiverWaterDepth(float FlowUnits, int32 DownstreamDistance)
    {
        const float FlowBonus = FMath::Min(0.04f, FMath::Max(0.0f, FlowUnits - 1.0f) * 0.015f);
        const float DistanceBonus = FMath::Min(0.03f, static_cast<float>(DownstreamDistance) * 0.0015f);
        return FMath::Clamp(
            Step03RiverMinDepth + FlowBonus + DistanceBonus,
            Step03RiverMinDepth,
            Step03RiverMaxDepth);
    }

    static const TCHAR* RiverOutletKindToString(ERiverOutletKind Kind)
    {
        switch (Kind)
        {
        case ERiverOutletKind::Coast:        return TEXT("Coast");
        case ERiverOutletKind::Sea:          return TEXT("Sea");
        case ERiverOutletKind::Lake:         return TEXT("Lake");
        case ERiverOutletKind::RiverMerge:   return TEXT("River");
        case ERiverOutletKind::LakeEndpoint: return TEXT("LakeEndpoint");
        case ERiverOutletKind::Loop:         return TEXT("Loop");
        case ERiverOutletKind::MaxLength:    return TEXT("MaxLength");
        case ERiverOutletKind::None:
        default:
            return TEXT("None");
        }
    }

    struct FWaterBalanceStats
    {
        int32 WaterCount = 0;
        int32 LandCount = 0;
        float WaterPct = 0.0f;
        float LandPct = 0.0f;
    };

    struct FHeightDistributionStats
    {
        float Mean = 0.0f;
        float P10 = 0.0f;
        float P25 = 0.0f;
        float P50 = 0.0f;
        float P75 = 0.0f;
        float P90 = 0.0f;
        float WaterPct = 0.0f;
    };

    static float SampleSortedPercentile(const TArray<float>& SortedValues, float Percentile01)
    {
        if (SortedValues.Num() <= 0)
        {
            return 0.0f;
        }

        const float ClampedPercentile = FMath::Clamp(Percentile01, 0.0f, 1.0f);
        const float IndexFloat = ClampedPercentile * static_cast<float>(SortedValues.Num() - 1);
        const int32 IndexA = FMath::Clamp(FMath::FloorToInt(IndexFloat), 0, SortedValues.Num() - 1);
        const int32 IndexB = FMath::Clamp(IndexA + 1, 0, SortedValues.Num() - 1);
        const float Alpha = IndexFloat - static_cast<float>(IndexA);

        return FMath::Lerp(SortedValues[IndexA], SortedValues[IndexB], Alpha);
    }

    static FHeightDistributionStats CalculateHeightDistributionStats(
        const TArray<float>& Heights,
        float SeaThreshold)
    {
        FHeightDistributionStats Stats;
        if (Heights.Num() <= 0)
        {
            return Stats;
        }

        TArray<float> SortedHeights = Heights;
        SortedHeights.Sort();

        double Sum = 0.0;
        int32 WaterCount = 0;
        for (const float Height : Heights)
        {
            Sum += Height;
            if (Height < SeaThreshold)
            {
                ++WaterCount;
            }
        }

        Stats.Mean = static_cast<float>(Sum / static_cast<double>(Heights.Num()));
        Stats.P10 = SampleSortedPercentile(SortedHeights, 0.10f);
        Stats.P25 = SampleSortedPercentile(SortedHeights, 0.25f);
        Stats.P50 = SampleSortedPercentile(SortedHeights, 0.50f);
        Stats.P75 = SampleSortedPercentile(SortedHeights, 0.75f);
        Stats.P90 = SampleSortedPercentile(SortedHeights, 0.90f);
        Stats.WaterPct = static_cast<float>(WaterCount) * (100.0f / static_cast<float>(Heights.Num()));

        return Stats;
    }

    static FWaterBalanceStats CalculateWaterBalanceStats(
        const TArray<FWorldTileData>& Tiles,
        float SeaThreshold)
    {
        FWaterBalanceStats Stats;
        const int32 TotalTiles = Tiles.Num();

        for (const FWorldTileData& Tile : Tiles)
        {
            if (Tile.Elevation < SeaThreshold)
            {
                ++Stats.WaterCount;
            }
        }

        Stats.LandCount = FMath::Max(0, TotalTiles - Stats.WaterCount);

        if (TotalTiles > 0)
        {
            const float InvTotalPct = 100.0f / static_cast<float>(TotalTiles);
            Stats.WaterPct = static_cast<float>(Stats.WaterCount) * InvTotalPct;
            Stats.LandPct = static_cast<float>(Stats.LandCount) * InvTotalPct;
        }

        return Stats;
    }
}

void UWorldGenPipeline::GenerateWorld(
    int64 InWorldSeed,
    FWorldGenConfig Config,
    UWorldForgeRNG* RNG)
{
    bComplete = false;
    CurrentConfig = Config;
    WorldSeed = InWorldSeed;
    RNGRef = RNG;
    WorldRef = GetOuter() ? GetOuter()->GetWorld() : nullptr;
    GridWidth = GetDimension(Config.WorldSize);
    GridHeight = GridWidth;

    if (!RNGRef)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGen] GenerateWorld: RNG is null! Aborting."));
        return;
    }

    Civilizations.Reset();
    HistoryLog.Reset();
    Currencies.Reset();
    WorldTraits.Reset();
    MountainRanges.Reset();
    MountainProvinces.Reset();
    VolcanicHotspots.Reset();
    Volcanoes.Reset();
    LakeBasins.Reset(); // LAKE-FIX-01A

    WorldTiles.SetNum(GridWidth * GridHeight);
    for (int32 i = 0; i < WorldTiles.Num(); i++)
    {
        WorldTiles[i].TileID = i;
        WorldTiles[i].KnowledgeState.TileID = i;
        WorldTiles[i].KnowledgeState.KnowledgeLevel = EKnowledgeLevel::Unexplored;
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Start Seed=%lld Grid=%dx%d"), WorldSeed, GridWidth, GridHeight);

    Step01_InitializeSeed();

    Step02_GenerateHeightmap();
    PushPartialMapUpdate();                      // land/water appears

    Step03_RunTectonics();
    PushPartialMapUpdate();                      // mountains/rivers/canyons

    Step04_AssignBiomes();
    PushPartialMapUpdate();                      // terrain colors improve

    // LAKE-FIX-01A: Flood-fill connected lake tiles into basin groups.
    // Must run after Step04 (biome/WaterDepth set) and before the bootstrap-owned
    // Step05 voxel handoff compatibility path.
    BuildLakeBasins();

    // CRITICAL HANDOFF FIX:
    // Resource veins must be seeded before voxel terrain initialization.
    // Step05 hands WorldTiles to VoxelWorldManager, which may immediately cache/snapshot
    // tile metadata even if actual terrain generation is deferred.
    // If resources are seeded after Step05, VoxelWorldManager sees 0 vein tiles.
    Step08_SeedResources();
    PushPartialMapUpdate();                      // resource overlay/data now exists before terrain snapshot
    // WorldGenPipeline now stops at generated data. Bootstrap owns when the
    // runtime voxel terrain handoff is requested after map subsystem init.
    Step06_PCGDecorationPass();
    Step07_ApplyWorldDriver();

    Step09_PlaceCivilizations();
    PushPartialMapUpdate();                      // civ territory appears

    Step10_GenerateCulturalDNA();
    Step11_GenerateEconomyLayer();
    Step12_GenerateCosmicRoster();

    Step13_RunPreHistory();
    PushPartialMapUpdate();                      // history/civ changes

    Step14_AssignWorldTraits();
    Step15_RecordFinalState();

    Step16_FinalizeWorldMap();
    PushPartialMapUpdate();                      // final locked map
    UE_LOG(LogTemp, Log, TEXT("[EmbarkFlow] Data worldgen complete; voxel terrain handoff deferred to bootstrap."));

    bComplete = true;
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Complete Tiles=%d Civs=%d Events=%d Currencies=%d Hotspots=%d Volcanoes=%d"),
        WorldTiles.Num(), Civilizations.Num(),
        HistoryLog.Num(), Currencies.Num(), VolcanicHotspots.Num(), Volcanoes.Num());
}

bool UWorldGenPipeline::DispatchVoxelTerrainHandoff(UObject* OptionalWorldContext)
{
    // WorldGenPipeline keeps the legacy Step05 implementation for now, but
    // AWorldGenBootstrapActor owns when playable voxel terrain actually starts.
    if (OptionalWorldContext)
    {
        if (UWorld* ContextWorld = OptionalWorldContext->GetWorld())
        {
            WorldRef = ContextWorld;
        }
    }

    return Step05_InitVoxelChunks();
}

void UWorldGenPipeline::HandlePCGDecorationPass_Implementation(const TArray<FWorldTileData>& InWorldTiles, int32 InGridWidth, int32 InGridHeight)
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] HandlePCGDecorationPass default. Tiles=%d Grid=%dx%d"),
        InWorldTiles.Num(), InGridWidth, InGridHeight);
}

// ────────────────────────────────────────────────────────────────────
// DF-style staged map update.
//
// Pushes partial world data to WorldMapSubsystem so the player sees
// the map "build itself" during generation.  Called after each major
// pipeline step that visually changes the map.
// ────────────────────────────────────────────────────────────────────
void UWorldGenPipeline::PushPartialMapUpdate()
{
    if (!WorldRef)
    {
        return;
    }

    UWorldMapSubsystem* MapSub = WorldRef->GetSubsystem<UWorldMapSubsystem>();
    if (!MapSub)
    {
        return;
    }

    MapSub->UpdateFromPartialPipeline(WorldTiles, Civilizations, GridWidth, GridHeight);

    // Force immediate texture rebuild so the player sees the update
    // before the next step begins.
    MapSub->RebuildMapTexture();
}

int32 UWorldGenPipeline::GetDimension(EWorldSize S) const
{
    switch (S)
    {
    case EWorldSize::Small:  return 128;
    case EWorldSize::Large:  return 512;
    case EWorldSize::OMG:    return 1024;
    default:                 return 256;
    }
}

void UWorldGenPipeline::Step01_InitializeSeed()
{
    RNGRef->InitializeWithSeed(WorldSeed);
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step01 done"));
}

// ════════════════════════════════════════════════════════════════════
// STEP 02 — v3.3: Multi-continent heightmap
//
// Produces 3–5 separated continents with ~25–35% ocean.
//
// Architecture:
//   1. Domain-warped FBM for organic continent shapes
//   2. Power curve to control land/ocean ratio
//   3. TWO channel carvers at different frequencies/orientations
//      to split the landmass into 3–5 pieces
//   4. Detail noise for interior variation
//   5. Edge fade for guaranteed ocean border
//   6. Ridge noise for volcanic hotspot candidates
//
// The dual-channel approach is the key to multi-continent separation:
//   Channel A (low freq)  → creates 1 broad ocean gap (2 halves)
//   Channel B (med freq)  → creates a second gap at a different angle
//   Combined              → 3–5 natural-looking landmasses
// ════════════════════════════════════════════════════════════════════

void UWorldGenPipeline::Step02_GenerateHeightmap()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step02: Multi-continent heightmap (v3.3)..."));

    constexpr float ReferenceGridSize = 512.0f;
    constexpr float TargetWaterRatio = 0.38f;
    constexpr float MinWaterRatio = 0.30f;
    constexpr float MaxWaterRatio = 0.45f;
    constexpr float CalibrationStrength = 0.35f;
    constexpr float DryMaxCalibrationOffset = 0.065f;  // enlarged cap for severe-dry seeds (PreCal < 25%)
    constexpr float WetMaxCalibrationOffset = 0.045f;  // standard cap for wet/mild-dry correction
    constexpr float SevereDryThreshold = 0.25f;        // below this ratio, use enlarged dry cap
    constexpr float SecondPassStrength = 0.25f;
    constexpr float MaxSecondPassOffset = 0.030f;
    constexpr float DryWarningWaterPct = 28.0f;
    constexpr float WetWarningWaterPct = 50.0f;
    constexpr float RawExtremeDryWarningWaterPct = 22.0f;
    constexpr float RawExtremeWetWarningWaterPct = 55.0f;
    constexpr float DrySeedMacroTriggerWaterPct = 25.0f;
    constexpr float MaxDrySeedOceanBoost = 0.080f;
    constexpr float DryBoostChannelMaskStart = 0.08f;
    constexpr float DryBoostChannelMaskEnd = 0.45f;
    constexpr float DryBoostLowlandMaskStart = 0.52f;
    constexpr float DryBoostLowlandMaskEnd = 0.72f;

    struct FStep02TileSample
    {
        float BaseElevation = 0.0f;
        float ChannelADepth = 0.0f;
        float ChannelBDepth = 0.0f;
        float RidgeVal = 0.0f;
        float EdgeFade = 0.0f;
    };

    // ── Deterministic offsets ──
    const float OffX = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.f, 10000.f);
    const float OffY = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.f, 10000.f);
    const float WarpOffX = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.f, 10000.f);
    const float WarpOffY = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.f, 10000.f);
    const float ChannelAOffX = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.f, 10000.f);
    const float ChannelAOffY = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.f, 10000.f);
    const float ChannelBOffX = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.f, 10000.f);
    const float ChannelBOffY = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.f, 10000.f);

    // ── Main continent noise ──
    const float ContinentScale = 0.0038f;
    const int32 ContinentOctaves = 6;
    const float ContinentPersist = 0.48f;
    const float ContinentLacun = 2.1f;

    // ── Domain warp for organic coastlines ──
    const float WarpScale = 0.0028f;
    const float WarpStrength = 25.0f;

    // ── Interior detail noise ──
    const float DetailScale = 0.020f;
    const float DetailWeight = 0.06f;

    // ── Channel carver A — primary ocean gap (low frequency = broad) ──
    const float ChannelAScale = 0.0025f;
    const float ChannelAThreshold = 0.43f;
    const float ChannelAStrength = 0.42f;

    // ── Channel carver B — secondary ocean gap (slightly higher freq) ──
    const float ChannelBScale = 0.0038f;
    const float ChannelBThreshold = 0.47f;
    const float ChannelBStrength = 0.34f;

    // ── Land shaping ──
    const float LandPower = 0.62f;      // higher = more ocean
    const float SeaThreshold = 0.48f;   // aligned with voxel terrain WaterThreshold
    const float LandBias = 0.10f;

    const float SafeGridWidth = FMath::Max(1.0f, static_cast<float>(GridWidth));
    const float WorldSizeScale = ReferenceGridSize / SafeGridWidth;
    const float ContinentScaleNormalized = ContinentScale * WorldSizeScale;
    const float ChannelAScaleNormalized = ChannelAScale * WorldSizeScale;
    const float ChannelBScaleNormalized = ChannelBScale * WorldSizeScale;

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] MacroScale Grid=%dx%d Reference=%.0f WorldScale=%.3f ContinentScale=%.6f ChannelA=%.6f ChannelB=%.6f"),
        GridWidth,
        GridHeight,
        ReferenceGridSize,
        WorldSizeScale,
        ContinentScaleNormalized,
        ChannelAScaleNormalized,
        ChannelBScaleNormalized);

    // ── Edge fade ──
    const FVector2D MapCenter((GridWidth - 1) * 0.5f, (GridHeight - 1) * 0.5f);
    const float MaxCenterDist = FVector2D::Distance(FVector2D::ZeroVector, MapCenter);

    VolcanicHotspots.Reset();

    TArray<FStep02TileSample> TileSamples;
    TileSamples.SetNum(WorldTiles.Num());

    TArray<float> InitialRawHeights;
    InitialRawHeights.SetNum(WorldTiles.Num());

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Index = TileIndex(X, Y);
            FStep02TileSample& Sample = TileSamples[Index];
            const float SX = static_cast<float>(X);
            const float SY = static_cast<float>(Y);

            // ── 1. Domain warp ──
            const float WarpDX = PerlinFBM2D(
                (SX + WarpOffX) * WarpScale, (SY + WarpOffY) * WarpScale,
                3, 0.5f, 2.0f) * WarpStrength;
            const float WarpDY = PerlinFBM2D(
                (SX + WarpOffX + 5000.f) * WarpScale, (SY + WarpOffY + 5000.f) * WarpScale,
                3, 0.5f, 2.0f) * WarpStrength;
            const float WarpedX = SX + WarpDX;
            const float WarpedY = SY + WarpDY;

            // ── 2. Base continent noise ──
            const float ContinentRaw = PerlinFBM2D(
                (WarpedX + OffX) * ContinentScaleNormalized,
                (WarpedY + OffY) * ContinentScaleNormalized,
                ContinentOctaves, ContinentPersist, ContinentLacun);
            float Continent01 = (ContinentRaw + 1.0f) * 0.5f;

            // ── 3. Power curve ──
            Continent01 = FMath::Pow(FMath::Clamp(Continent01, 0.0f, 1.0f), LandPower);

            // ── 4. Channel carver A — broad primary ocean gap ──
            {
                const float ChNoise = PerlinFBM2D(
                    (WarpedX + ChannelAOffX) * ChannelAScaleNormalized,
                    (WarpedY + ChannelAOffY) * ChannelAScaleNormalized,
                    3, 0.45f, 2.2f);
                const float Ch01 = (ChNoise + 1.0f) * 0.5f;

                if (Ch01 > ChannelAThreshold)
                {
                    Sample.ChannelADepth = (Ch01 - ChannelAThreshold) / (1.0f - ChannelAThreshold);
                    Continent01 -= Sample.ChannelADepth * ChannelAStrength;
                }
            }

            // ── 5. Channel carver B — secondary ocean gap ──
            {
                const float ChNoise = PerlinFBM2D(
                    (WarpedX + ChannelBOffX) * ChannelBScaleNormalized,
                    (WarpedY + ChannelBOffY) * ChannelBScaleNormalized,
                    3, 0.50f, 2.0f);
                const float Ch01 = (ChNoise + 1.0f) * 0.5f;

                if (Ch01 > ChannelBThreshold)
                {
                    Sample.ChannelBDepth = (Ch01 - ChannelBThreshold) / (1.0f - ChannelBThreshold);
                    Continent01 -= Sample.ChannelBDepth * ChannelBStrength;
                }
            }

            // ── 6. Detail noise ──
            const float DetailRaw = PerlinFBM2D(
                (SX + OffX + 3000.f) * DetailScale,
                (SY + OffY + 3000.f) * DetailScale,
                4, 0.5f, 2.0f);
            const float Detail01 = (DetailRaw + 1.0f) * 0.5f;

            // ── 7. Edge fade — guarantee ocean border ──
            const float DistToCenter = FVector2D::Distance(FVector2D(SX, SY), MapCenter);
            const float EdgeNorm = FMath::Clamp(DistToCenter / MaxCenterDist, 0.0f, 1.0f);
            Sample.EdgeFade = FMath::SmoothStep(0.65f, 0.92f, EdgeNorm);

            // ── 8. Combine ──
            Sample.BaseElevation = Continent01 * 0.85f + Detail01 * DetailWeight + LandBias;
            Sample.BaseElevation *= (1.0f - Sample.EdgeFade);
            Sample.BaseElevation = FMath::Clamp(Sample.BaseElevation, 0.0f, 1.0f);

            // ── 9. Ridge noise for volcanic hotspot candidates ──
            const float RidgeRaw = FMath::PerlinNoise2D(FVector2D(
                (WarpedX + OffX + 1000.f) * 0.012f,
                (WarpedY + OffY + 1000.f) * 0.012f));
            Sample.RidgeVal = 1.0f - FMath::Abs(RidgeRaw);
            Sample.RidgeVal = Sample.RidgeVal * Sample.RidgeVal;

            float InitialElevation = Sample.BaseElevation;
            if (InitialElevation > SeaThreshold)
            {
                const float CoastalFade = FMath::Clamp(
                    (InitialElevation - SeaThreshold) / 0.15f, 0.0f, 1.0f);
                InitialElevation += Sample.RidgeVal * 0.10f * CoastalFade;
            }

            InitialRawHeights[Index] = FMath::Clamp(InitialElevation, 0.0f, 1.0f);
        }
    }

    const FHeightDistributionStats InitialRawStats = CalculateHeightDistributionStats(InitialRawHeights, SeaThreshold);

    float DrySeedOceanBoost = 0.0f;
    if (InitialRawStats.WaterPct < DrySeedMacroTriggerWaterPct
        && InitialRawStats.Mean > SeaThreshold
        && InitialRawStats.P50 > SeaThreshold)
    {
        const float WaterDeficitAlpha = FMath::Clamp(
            (DrySeedMacroTriggerWaterPct - InitialRawStats.WaterPct) / 12.0f,
            0.0f,
            1.0f);
        const float MeanHighAlpha = FMath::Clamp(
            (InitialRawStats.Mean - SeaThreshold) / 0.14f,
            0.0f,
            1.0f);
        const float MedianHighAlpha = FMath::Clamp(
            (InitialRawStats.P50 - SeaThreshold) / 0.14f,
            0.0f,
            1.0f);

        DrySeedOceanBoost = FMath::Clamp(
            (WaterDeficitAlpha * 0.60f + MeanHighAlpha * 0.25f + MedianHighAlpha * 0.15f) * MaxDrySeedOceanBoost,
            0.0f,
            MaxDrySeedOceanBoost);
    }

    if (DrySeedOceanBoost > 0.0f)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[WorldGen] MacroBalance Step02 DrySeedOceanBoost=%.4f InitialWaterPct=%.1f InitialMean=%.3f InitialMedian=%.3f"),
            DrySeedOceanBoost,
            InitialRawStats.WaterPct,
            InitialRawStats.Mean,
            InitialRawStats.P50);
    }

    TArray<float> PreCalHeights;
    PreCalHeights.SetNum(WorldTiles.Num());

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Index = TileIndex(X, Y);
            const FStep02TileSample& Sample = TileSamples[Index];
            FWorldTileData& Tile = WorldTiles[Index];

            float BaseElevation = Sample.BaseElevation;
            if (DrySeedOceanBoost > 0.0f)
            {
                const float ChannelMask = FMath::SmoothStep(
                    DryBoostChannelMaskStart,
                    DryBoostChannelMaskEnd,
                    FMath::Max(Sample.ChannelADepth, Sample.ChannelBDepth));
                const float LowlandMask = 1.0f - FMath::SmoothStep(
                    DryBoostLowlandMaskStart,
                    DryBoostLowlandMaskEnd,
                    BaseElevation);
                const float InteriorMask = 1.0f - Sample.EdgeFade * 0.35f;
                const float OceanMask = FMath::Clamp(
                    ChannelMask * 0.70f + LowlandMask * 0.30f,
                    0.0f,
                    1.0f) * InteriorMask;

                BaseElevation = FMath::Max(0.0f, BaseElevation - DrySeedOceanBoost * OceanMask);
            }

            float Elevation = BaseElevation;
            if (Elevation > SeaThreshold)
            {
                const float CoastalFade = FMath::Clamp(
                    (Elevation - SeaThreshold) / 0.15f, 0.0f, 1.0f);
                Elevation += Sample.RidgeVal * 0.10f * CoastalFade;

                if (Sample.RidgeVal > 0.78f && CoastalFade > 0.3f)
                {
                    VolcanicHotspots.Add(FIntPoint(X, Y));
                }
            }

            Tile.Elevation = FMath::Clamp(Elevation, 0.0f, 1.0f);
            Tile.Temperature = 0.5f;
            Tile.Rainfall = 0.5f;
            PreCalHeights[Index] = Tile.Elevation;
        }
    }

    const FHeightDistributionStats RawHeightStats = CalculateHeightDistributionStats(PreCalHeights, SeaThreshold);
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] HeightDist Step02 Raw Mean=%.3f P10=%.3f P25=%.3f P50=%.3f P75=%.3f P90=%.3f Sea=%.3f WaterPct=%.1f"),
        RawHeightStats.Mean,
        RawHeightStats.P10,
        RawHeightStats.P25,
        RawHeightStats.P50,
        RawHeightStats.P75,
        RawHeightStats.P90,
        SeaThreshold,
        RawHeightStats.WaterPct);

    if (RawHeightStats.WaterPct < RawExtremeDryWarningWaterPct)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WorldGen] WARNING: Raw Step02 extremely dry before calibration. WaterPct=%.1f MeanElev=%.3f Median=%.3f"),
            RawHeightStats.WaterPct,
            RawHeightStats.Mean,
            RawHeightStats.P50);
    }

    if (RawHeightStats.WaterPct > RawExtremeWetWarningWaterPct)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WorldGen] WARNING: Raw Step02 extremely wet before calibration. WaterPct=%.1f MeanElev=%.3f Median=%.3f"),
            RawHeightStats.WaterPct,
            RawHeightStats.Mean,
            RawHeightStats.P50);
    }

    const FWaterBalanceStats PreCalStats = CalculateWaterBalanceStats(WorldTiles, SeaThreshold);
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] WaterBalance Step02 PreCal WaterPct=%.1f LandPct=%.1f"),
        PreCalStats.WaterPct,
        PreCalStats.LandPct);

    // ── Pass 1 ──
    // Good seeds (30–45% water) receive no offset.
    // Severe-dry seeds (<25%) get an enlarged dry cap to make a stronger first correction.
    float Pass1Offset = 0.0f;
    const TCHAR* Pass1Reason = TEXT("InRange");

    const bool bSevereDry = (PreCalStats.WaterPct * 0.01f) < SevereDryThreshold;
    const float EffectiveDryCap = bSevereDry ? DryMaxCalibrationOffset : WetMaxCalibrationOffset;

    if (PreCalStats.WaterPct < (MinWaterRatio * 100.0f))
    {
        const float Delta = MinWaterRatio - (PreCalStats.WaterPct * 0.01f);
        Pass1Offset = -FMath::Clamp(Delta * CalibrationStrength, 0.0f, EffectiveDryCap);
        Pass1Reason = bSevereDry ? TEXT("SevereDry") : TEXT("TooDry");
    }
    else if (PreCalStats.WaterPct > (MaxWaterRatio * 100.0f))
    {
        const float Delta = (PreCalStats.WaterPct * 0.01f) - MaxWaterRatio;
        Pass1Offset = FMath::Clamp(Delta * CalibrationStrength, 0.0f, WetMaxCalibrationOffset);
        Pass1Reason = TEXT("TooWet");
    }

    const bool bPass1Applied = !FMath::IsNearlyZero(Pass1Offset, KINDA_SMALL_NUMBER);
    if (bPass1Applied)
    {
        for (FWorldTileData& Tile : WorldTiles)
        {
            Tile.Elevation = FMath::Clamp(Tile.Elevation + Pass1Offset, 0.0f, 1.0f);
        }
    }

    const FWaterBalanceStats PostPass1Stats = CalculateWaterBalanceStats(WorldTiles, SeaThreshold);
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] WaterBalance Calibration Pass1 Applied=%s Offset=%.4f Reason=%s"),
        bPass1Applied ? TEXT("true") : TEXT("false"),
        Pass1Offset,
        Pass1Reason);
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] WaterBalance Step02 PostCal WaterPct=%.1f LandPct=%.1f"),
        PostPass1Stats.WaterPct,
        PostPass1Stats.LandPct);

    // ── Pass 2 — smaller nudge if still outside target after Pass 1 ──
    // Dry correction is full SecondPassStrength; wet correction is deliberately more conservative.
    float Pass2Offset = 0.0f;
    const TCHAR* Pass2Reason = TEXT("Skipped");

    if (PostPass1Stats.WaterPct < (MinWaterRatio * 100.0f))
    {
        const float RemainingDelta = MinWaterRatio - (PostPass1Stats.WaterPct * 0.01f);
        Pass2Offset = -FMath::Clamp(RemainingDelta * SecondPassStrength, 0.0f, MaxSecondPassOffset);
        Pass2Reason = TEXT("StillDry");
    }
    else if (PostPass1Stats.WaterPct > (MaxWaterRatio * 100.0f))
    {
        const float RemainingDelta = (PostPass1Stats.WaterPct * 0.01f) - MaxWaterRatio;
        Pass2Offset = FMath::Clamp(RemainingDelta * SecondPassStrength * 0.5f, 0.0f, MaxSecondPassOffset * 0.5f);
        Pass2Reason = TEXT("StillWet");
    }

    const bool bPass2Applied = !FMath::IsNearlyZero(Pass2Offset, KINDA_SMALL_NUMBER);
    if (bPass2Applied)
    {
        for (FWorldTileData& Tile : WorldTiles)
        {
            Tile.Elevation = FMath::Clamp(Tile.Elevation + Pass2Offset, 0.0f, 1.0f);
        }
    }

    const FWaterBalanceStats PostCalStats = CalculateWaterBalanceStats(WorldTiles, SeaThreshold);
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] WaterBalance Calibration Pass2 Applied=%s Offset=%.4f Reason=%s"),
        bPass2Applied ? TEXT("true") : TEXT("false"),
        Pass2Offset,
        Pass2Reason);

    const float TotalCalOffset = Pass1Offset + Pass2Offset;
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] WaterBalance Step02 FinalCal WaterPct=%.1f LandPct=%.1f TotalOffset=%.4f Target=%.1f"),
        PostCalStats.WaterPct,
        PostCalStats.LandPct,
        TotalCalOffset,
        TargetWaterRatio * 100.0f);

    if (PostCalStats.WaterPct < DryWarningWaterPct)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WorldGen] WARNING: World still too dry after calibration. WaterPct=%.1f"),
            PostCalStats.WaterPct);
    }

    if (PostCalStats.WaterPct > WetWarningWaterPct)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WorldGen] WARNING: World still too wet after calibration. WaterPct=%.1f"),
            PostCalStats.WaterPct);
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step02 done. Water=%d (%d%%) Land=%d (%d%%) VolcanicHotspots=%d"),
        PostCalStats.WaterCount, FMath::RoundToInt(PostCalStats.WaterPct),
        PostCalStats.LandCount, FMath::RoundToInt(PostCalStats.LandPct),
        VolcanicHotspots.Num());

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] WaterBalance Step02 SeaLevel=%.3f HeightBias=%.3f ContinentBias=%.3f WaterPct=%.1f LandPct=%.1f"),
        SeaThreshold,
        LandBias,
        LandPower,
        PostCalStats.WaterPct,
        PostCalStats.LandPct);
}

// ════════════════════════════════════════════════════════════════════
// STEP 03 — Tectonics + Mountains + Volcanoes + Rivers + Lakes +
//           Canyons (v3.1)
//
// Runs in phases:
//   A. Tectonic provinces → mountain range splines (kept from v2)
//   B. Apply mountain elevation to tiles (STRONGER than v3.0)
//   C. Volcano composite primitives (kept from v3.0)
//   D. River generation (NEW — flow from mountains to coast)
//   E. Lake placement (NEW — inland water at basins)
//   F. Canyon carving (NEW — deep cuts near mountains)
// ════════════════════════════════════════════════════════════════════

void UWorldGenPipeline::Step03_RunTectonics()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step03: Tectonics + terrain features..."));

    MountainRanges.Reset();
    MountainProvinces.Reset();
    const FMountainGenerationSettings& Settings = CurrentConfig.MountainSettings;

    // ── Phase A: Generate tectonic provinces ──
    const int32 ProvinceCount = FMath::Clamp(
        RNGRef->NextInt(UWorldForgeRNG::BRANCH_TERRAIN, Settings.MinProvinceCount, Settings.MaxProvinceCount), 1, 64);

    MountainProvinces.Reserve(ProvinceCount);
    for (int32 ProvinceIdx = 0; ProvinceIdx < ProvinceCount; ++ProvinceIdx)
    {
        FMountainProvince Province;
        Province.Center = FVector2D(
            RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) * float(GridWidth - 1),
            RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) * float(GridHeight - 1));
        const float AngleDeg = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) * 360.0f;
        Province.Direction = FVector2D(
            FMath::Cos(FMath::DegreesToRadians(AngleDeg)),
            FMath::Sin(FMath::DegreesToRadians(AngleDeg))).GetSafeNormal();
        Province.SpreadRadius = FMath::Lerp(20.0f, 70.0f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
        Province.Strength = FMath::Lerp(2.2f, 3.5f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
        Province.RangeCount = FMath::Clamp(
            RNGRef->NextInt(UWorldForgeRNG::BRANCH_TERRAIN, Settings.MinRangesPerProvince, Settings.MaxRangesPerProvince), 1, 32);
        MountainProvinces.Add(Province);
    }

    // ── Generate mountain ranges per province ──
    for (const FMountainProvince& Province : MountainProvinces)
    {
        for (int32 RangeIdx = 0; RangeIdx < Province.RangeCount; ++RangeIdx)
        {
            const float DirJitter = FMath::Lerp(
                -Settings.ProvinceDirectionJitterDegrees, Settings.ProvinceDirectionJitterDegrees,
                RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
            const FVector2D FinalDir = Rotate2D(Province.Direction, DirJitter).GetSafeNormal();

            const float OffsetAngleDeg = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) * 360.0f;
            const float OffsetRadius = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) * Settings.StartOffsetJitterRadius;
            const FVector2D Offset(
                FMath::Cos(FMath::DegreesToRadians(OffsetAngleDeg)) * OffsetRadius,
                FMath::Sin(FMath::DegreesToRadians(OffsetAngleDeg)) * OffsetRadius);

            FMountainRange Range;
            Range.Start = Province.Center + Offset;
            Range.Start.X = FMath::Clamp(Range.Start.X, 0.0f, float(GridWidth - 1));
            Range.Start.Y = FMath::Clamp(Range.Start.Y, 0.0f, float(GridHeight - 1));
            Range.Direction = FinalDir;
            Range.Length = FMath::Lerp(Settings.MinRangeLength, Settings.MaxRangeLength, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
            Range.Width = FMath::Lerp(Settings.MinRangeWidth, Settings.MaxRangeWidth, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
            Range.HeightMultiplier = FMath::Lerp(Settings.MinHeightMultiplier, Settings.MaxHeightMultiplier,
                RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN)) * Province.Strength;
            Range.BasinOffset = Range.Width * FMath::Lerp(1.0f, 1.8f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
            Range.BasinWidth = Range.Width * FMath::Lerp(0.7f, 1.3f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
            Range.BasinStrength = Range.HeightMultiplier * Settings.BasinStrengthScale;
            MountainRanges.Add(Range);
        }
    }

    // ── Phase B: Apply mountain contribution to tiles (STRONGER) ──
    for (int32 Y = 0; Y < GridHeight; Y++)
    {
        for (int32 X = 0; X < GridWidth; X++)
        {
            FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];
            if (Tile.Elevation < 0.48f) continue; // Skip ocean tiles

            float Elevation = Tile.Elevation;

            // Tectonic plate noise
            Elevation += FMath::PerlinNoise2D(FVector2D(X * 0.004f, Y * 0.004f)) * 0.03f;

            // Ridge noise for natural mountain shapes
            const float RidgeNoise = FMath::Abs(FMath::PerlinNoise2D(FVector2D(X * 0.0014f, Y * 0.0014f)));
            Elevation += RidgeNoise * 0.15f;

            // Valley noise
            const float ValleyNoise = FMath::PerlinNoise2D(FVector2D((X + 1000) * 0.0014f, (Y + 1000) * 0.0014f));
            Elevation -= FMath::Abs(ValleyNoise) * 0.08f;

            // Mountain range contribution — MUCH stronger than v3.0
            const float MountainContribution = CalculateMountainContributionAtTile(
                FVector2D(float(X), float(Y)), MountainRanges, Settings.FalloffSharpness);
            Elevation += MountainContribution * 1.5f; // v3.0 was 1.0

            // Keep tectonic valley/basin shaping from converting established land into open sea.
            Tile.Elevation = FMath::Clamp(Elevation, 0.48f, 1.0f);
        }
    }

    // ── Phase C: Volcano placement ──
    Volcanoes.Reset();
    const int32 MaxVolcanoes = FMath::Clamp(GridWidth / 64, 2, 8);
    const float MinVolcanoSpacing = GridWidth * 0.15f;

    TArray<FIntPoint> ShuffledHotspots = VolcanicHotspots;
    for (int32 i = ShuffledHotspots.Num() - 1; i > 0; --i)
    {
        const int32 j = RNGRef->NextInt(UWorldForgeRNG::BRANCH_VOLCANO, 0, i);
        ShuffledHotspots.Swap(i, j);
    }

    for (const FIntPoint& Hotspot : ShuffledHotspots)
    {
        if (Volcanoes.Num() >= MaxVolcanoes) break;
        const FVector2D Candidate(static_cast<float>(Hotspot.X), static_cast<float>(Hotspot.Y));
        bool bTooClose = false;
        for (const FVolcanoData& Existing : Volcanoes)
        {
            if (FVector2D::Distance(Candidate, Existing.Center) < MinVolcanoSpacing) { bTooClose = true; break; }
        }
        if (bTooClose) continue;
        const int32 Idx = TileIndex(Hotspot.X, Hotspot.Y);
        if (!WorldTiles.IsValidIndex(Idx) || WorldTiles[Idx].Elevation < 0.52f) continue;

        FVolcanoData V;
        V.Center = Candidate;
        V.ConeRadius = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_VOLCANO, 8.0f, 16.0f);
        V.ConeHeight = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_VOLCANO, 0.18f, 0.35f);
        V.CraterRadiusFraction = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_VOLCANO, 0.15f, 0.25f);
        V.CraterDepthFraction = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_VOLCANO, 0.30f, 0.50f);
        V.LavaFlowRadius = V.ConeRadius * RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_VOLCANO, 1.2f, 1.8f);
        V.ConeSteepness = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_VOLCANO, 1.5f, 2.5f);
        V.bActive = RNGRef->NextBool(UWorldForgeRNG::BRANCH_VOLCANO, 0.7f);
        Volcanoes.Add(V);
    }

    // Stamp volcano elevation
    for (const FVolcanoData& V : Volcanoes)
    {
        const int32 Radius = FMath::CeilToInt(V.ConeRadius) + 1;
        const int32 CX = FMath::RoundToInt(V.Center.X);
        const int32 CY = FMath::RoundToInt(V.Center.Y);
        for (int32 DY = -Radius; DY <= Radius; ++DY)
        {
            for (int32 DX = -Radius; DX <= Radius; ++DX)
            {
                const int32 TX = CX + DX, TY = CY + DY;
                if (TX < 0 || TX >= GridWidth || TY < 0 || TY >= GridHeight) continue;
                const float Dist = FMath::Sqrt(static_cast<float>(DX * DX + DY * DY));
                if (Dist >= V.ConeRadius) continue;
                FWorldTileData& Tile = WorldTiles[TileIndex(TX, TY)];
                const float NormDist = Dist / V.ConeRadius;
                const float ConeElev = FMath::Pow(1.0f - FMath::Clamp(NormDist, 0.0f, 1.0f), V.ConeSteepness) * V.ConeHeight;
                float CraterCarve = 0.0f;
                const float CraterRadius = V.ConeRadius * V.CraterRadiusFraction;
                if (Dist < CraterRadius)
                    CraterCarve = (1.0f - FMath::Square(Dist / FMath::Max(1.0f, CraterRadius))) * V.ConeHeight * V.CraterDepthFraction;
                Tile.Elevation = FMath::Clamp(Tile.Elevation + ConeElev - CraterCarve, 0.0f, 1.0f);
            }
        }
    }

    // ── Phase D: River generation ──
    // Build longer outlet-seeking rivers with tributary merges and simple flow accumulation.
    {
        const int32 MaxRiverSources = FMath::Clamp(GridWidth / 8, 8, 64);
        const int32 DrainageSearchRadius = FMath::Clamp(GridWidth / 64, 8, 16);
        const int32 MinRiverLengthTiles = (GridWidth >= 1024) ? 24 : ((GridWidth >= 512) ? 16 : ((GridWidth >= 256) ? 12 : 8));
        const int32 MaxRiverLengthTiles = FMath::Clamp(GridWidth * 2, 96, 4096);
        const float RiverSourceMinElev = (GridWidth >= 1024) ? 0.74f : ((GridWidth >= 512) ? 0.72f : 0.70f);
        const float RiverMinSpacing = GridWidth * 0.055f;

        TArray<float> RiverFlowUnits;
        RiverFlowUnits.Init(0.0f, WorldTiles.Num());

        TArray<int32> RiverLongestRun;
        RiverLongestRun.Init(0, WorldTiles.Num());

        TArray<int32> RiverDownstream;
        RiverDownstream.Init(INDEX_NONE, WorldTiles.Num());

        const auto EvaluateStepScore = [&](const FIntPoint& Current, const FIntPoint& Candidate, bool bUseTarget, const FIntPoint& Target) -> float
        {
            if (!IsInGrid(Candidate, GridWidth, GridHeight))
            {
                return -MAX_flt;
            }

            const int32 CurrentIdx = TileIndex(Current.X, Current.Y);
            const int32 CandidateIdx = TileIndex(Candidate.X, Candidate.Y);
            const FWorldTileData& CurrentTile = WorldTiles[CurrentIdx];
            const FWorldTileData& CandidateTile = WorldTiles[CandidateIdx];

            const bool bExistingRiver = RiverFlowUnits[CandidateIdx] > 0.0f;
            const bool bLakeOutlet = CandidateTile.WaterDepth >= Step03RiverLakeDepthThreshold;
            const bool bSeaOutlet = CandidateTile.Elevation < Step03SeaThreshold;
            const bool bCoastalOutlet = IsCoastalRiverOutlet(Candidate, WorldTiles, GridWidth, GridHeight);

            if (!bExistingRiver && !bLakeOutlet && !bSeaOutlet && !bCoastalOutlet
                && IsVolcanicRiverBlocked(Candidate, Volcanoes))
            {
                return -MAX_flt;
            }

            const float ElevationDelta = CurrentTile.Elevation - CandidateTile.Elevation;
            const float Uphill = FMath::Max(0.0f, CandidateTile.Elevation - CurrentTile.Elevation);
            if (Uphill > Step03RiverMinorUphillAllowance
                && !bExistingRiver && !bLakeOutlet && !bSeaOutlet && !bCoastalOutlet)
            {
                return -MAX_flt;
            }

            float Score = ElevationDelta * 22.0f;
            Score -= Uphill * 70.0f;
            Score -= (Candidate.X != Current.X && Candidate.Y != Current.Y) ? 0.15f : 0.0f;

            if (bExistingRiver)
            {
                Score += 12.0f;
            }
            if (bLakeOutlet)
            {
                Score += 8.0f;
            }
            if (bCoastalOutlet)
            {
                Score += 9.0f;
            }
            if (bSeaOutlet)
            {
                Score += 14.0f;
            }

            if (bUseTarget)
            {
                const FVector2D CurrentPos(static_cast<float>(Current.X), static_cast<float>(Current.Y));
                const FVector2D CandidatePos(static_cast<float>(Candidate.X), static_cast<float>(Candidate.Y));
                const FVector2D TargetPos(static_cast<float>(Target.X), static_cast<float>(Target.Y));
                const float Progress = FVector2D::Distance(CurrentPos, TargetPos) - FVector2D::Distance(CandidatePos, TargetPos);
                Score += Progress * 10.0f;
                if (Candidate == Target)
                {
                    Score += 8.0f;
                }
            }

            return Score;
        };

        const auto FindDrainageTarget = [&](const FIntPoint& Current, const TSet<int32>& Visited, int32 SearchRadius) -> FDrainageSearchResult
        {
            FDrainageSearchResult Result;
            const int32 CurrentIdx = TileIndex(Current.X, Current.Y);
            const float CurrentElevation = WorldTiles[CurrentIdx].Elevation;
            float BestScore = -MAX_flt;

            for (int32 DY = -SearchRadius; DY <= SearchRadius; ++DY)
            {
                for (int32 DX = -SearchRadius; DX <= SearchRadius; ++DX)
                {
                    if (DX == 0 && DY == 0)
                    {
                        continue;
                    }

                    if ((DX * DX + DY * DY) > (SearchRadius * SearchRadius))
                    {
                        continue;
                    }

                    const FIntPoint Candidate(Current.X + DX, Current.Y + DY);
                    if (!IsInGrid(Candidate, GridWidth, GridHeight))
                    {
                        continue;
                    }

                    const int32 CandidateIdx = TileIndex(Candidate.X, Candidate.Y);
                    if (Visited.Contains(CandidateIdx) && RiverFlowUnits[CandidateIdx] <= 0.0f)
                    {
                        continue;
                    }

                    const FWorldTileData& CandidateTile = WorldTiles[CandidateIdx];
                    const bool bExistingRiver = RiverFlowUnits[CandidateIdx] > 0.0f;
                    const bool bLakeOutlet = CandidateTile.WaterDepth >= Step03RiverLakeDepthThreshold;
                    const bool bSeaOutlet = CandidateTile.Elevation < Step03SeaThreshold;
                    const bool bCoastalOutlet = IsCoastalRiverOutlet(Candidate, WorldTiles, GridWidth, GridHeight);
                    const bool bLowerGround = CandidateTile.Elevation + 0.005f < CurrentElevation;

                    if (!bExistingRiver && !bLakeOutlet && !bSeaOutlet && !bCoastalOutlet && !bLowerGround)
                    {
                        continue;
                    }

                    if (!bExistingRiver && !bLakeOutlet && !bSeaOutlet && !bCoastalOutlet
                        && IsVolcanicRiverBlocked(Candidate, Volcanoes))
                    {
                        continue;
                    }

                    const float Uphill = FMath::Max(0.0f, CandidateTile.Elevation - CurrentElevation);
                    if (Uphill > Step03RiverMinorUphillAllowance * 1.5f
                        && !bExistingRiver && !bLakeOutlet && !bSeaOutlet && !bCoastalOutlet)
                    {
                        continue;
                    }

                    float Score = (CurrentElevation - CandidateTile.Elevation) * 12.0f;
                    Score -= Uphill * 40.0f;
                    Score -= FMath::Sqrt(static_cast<float>(DX * DX + DY * DY)) * 0.35f;

                    FString Reason = TEXT("LowerGround");
                    if (bExistingRiver)
                    {
                        Score += 12.0f;
                        Reason = TEXT("ExistingRiver");
                    }
                    else if (bLakeOutlet)
                    {
                        Score += 10.0f;
                        Reason = TEXT("Lake");
                    }
                    else if (bCoastalOutlet)
                    {
                        Score += 9.0f;
                        Reason = TEXT("Coast");
                    }
                    else if (bSeaOutlet)
                    {
                        Score += 8.0f;
                        Reason = TEXT("Sea");
                    }
                    else
                    {
                        Score += 3.0f;
                    }

                    if (Score > BestScore)
                    {
                        BestScore = Score;
                        Result.bFound = true;
                        Result.Target = Candidate;
                        Result.Reason = Reason;
                    }
                }
            }

            return Result;
        };

        FRiverSourceDiagnostics SourceDiagnostics;
        TArray<FRiverSourceCandidate> SourceCandidates;
        SourceCandidates.Reserve(GridWidth * GridHeight / 8);

        for (int32 Y = 2; Y < GridHeight - 2; ++Y)
        {
            for (int32 X = 2; X < GridWidth - 2; ++X)
            {
                const FIntPoint SourcePoint(X, Y);
                const FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];
                if (Tile.Elevation < RiverSourceMinElev)
                {
                    continue;
                }

                ++SourceDiagnostics.SourceCandidates;

                if (Tile.Elevation < Step03SeaThreshold || Tile.WaterDepth >= Step03RiverLakeDepthThreshold)
                {
                    ++SourceDiagnostics.RejectedWater;
                    continue;
                }

                if (IsVolcanicRiverBlocked(SourcePoint, Volcanoes))
                {
                    ++SourceDiagnostics.RejectedBiome;
                    continue;
                }

                int32 LowerNeighborCount = 0;
                int32 NeighborCount = 0;
                float NeighborElevationSum = 0.0f;
                float MinNeighborElevation = Tile.Elevation;
                float MaxNeighborElevation = Tile.Elevation;

                for (const FIntPoint& Offset : GRiverNeighborOffsets)
                {
                    const FIntPoint Neighbor(X + Offset.X, Y + Offset.Y);
                    if (!IsInGrid(Neighbor, GridWidth, GridHeight))
                    {
                        continue;
                    }

                    const float NeighborElevation = WorldTiles[TileIndex(Neighbor.X, Neighbor.Y)].Elevation;
                    ++NeighborCount;
                    NeighborElevationSum += NeighborElevation;
                    MinNeighborElevation = FMath::Min(MinNeighborElevation, NeighborElevation);
                    MaxNeighborElevation = FMath::Max(MaxNeighborElevation, NeighborElevation);

                    if (NeighborElevation < Tile.Elevation - 0.002f)
                    {
                        ++LowerNeighborCount;
                    }
                }

                const bool bHasDirectDrainage = LowerNeighborCount > 0;
                const TSet<int32> EmptyVisited;
                const FDrainageSearchResult SourceDrainage = bHasDirectDrainage
                    ? FDrainageSearchResult()
                    : FindDrainageTarget(SourcePoint, EmptyVisited, DrainageSearchRadius + 2);

                if (!bHasDirectDrainage && !SourceDrainage.bFound)
                {
                    ++SourceDiagnostics.RejectedNoDrainage;
                    continue;
                }

                const float MeanNeighborElevation = NeighborCount > 0
                    ? (NeighborElevationSum / static_cast<float>(NeighborCount))
                    : Tile.Elevation;
                const float LocalSlope = FMath::Max(0.0f, Tile.Elevation - MeanNeighborElevation);
                const float LocalRelief = MaxNeighborElevation - MinNeighborElevation;

                float SourceScore = Tile.Elevation * 0.30f;
                SourceScore += LocalSlope * 6.0f;
                SourceScore += LocalRelief * 4.0f;
                SourceScore += static_cast<float>(LowerNeighborCount) * 0.12f;

                if (Tile.Elevation > 0.92f && LowerNeighborCount <= 1)
                {
                    SourceScore -= 0.40f;
                }
                if (LocalRelief < 0.025f)
                {
                    SourceScore -= 0.30f;
                }

                FRiverSourceCandidate Candidate;
                Candidate.Tile = SourcePoint;
                Candidate.Score = SourceScore;
                SourceCandidates.Add(Candidate);
            }
        }

        SourceCandidates.Sort([](const FRiverSourceCandidate& A, const FRiverSourceCandidate& B)
        {
            return A.Score > B.Score;
        });

        TArray<FIntPoint> SelectedSources;
        for (const FRiverSourceCandidate& Candidate : SourceCandidates)
        {
            if (SelectedSources.Num() >= MaxRiverSources)
            {
                break;
            }

            bool bTooClose = false;
            for (const FIntPoint& Existing : SelectedSources)
            {
                const FVector2D CandidatePos(static_cast<float>(Candidate.Tile.X), static_cast<float>(Candidate.Tile.Y));
                const FVector2D ExistingPos(static_cast<float>(Existing.X), static_cast<float>(Existing.Y));
                if (FVector2D::Distance(CandidatePos, ExistingPos) < RiverMinSpacing)
                {
                    bTooClose = true;
                    break;
                }
            }

            if (!bTooClose)
            {
                SelectedSources.Add(Candidate.Tile);
            }
        }

        SourceDiagnostics.Accepted = SelectedSources.Num();
        UE_LOG(LogTemp, Log,
            TEXT("[RiverGen] Source candidates=%d Accepted=%d RejectedNoDrainage=%d RejectedWater=%d RejectedBiome=%d"),
            SourceDiagnostics.SourceCandidates,
            SourceDiagnostics.Accepted,
            SourceDiagnostics.RejectedNoDrainage,
            SourceDiagnostics.RejectedWater,
            SourceDiagnostics.RejectedBiome);

        int32 TotalRiverTiles = 0;

        for (const FIntPoint& Source : SelectedSources)
        {
            FIntPoint Current = Source;
            FIntPoint EndPoint = Source;
            TSet<int32> Visited;
            TArray<int32> LocalPath;
            LocalPath.Reserve(MinRiverLengthTiles * 2);

            ERiverOutletKind OutletKind = ERiverOutletKind::None;
            int32 TerminalIndex = INDEX_NONE;
            bool bMerged = false;
            bool bLakeCreated = false;

            for (int32 Step = 0; Step < MaxRiverLengthTiles; ++Step)
            {
                const int32 CurrentIdx = TileIndex(Current.X, Current.Y);
                EndPoint = Current;

                if (Visited.Contains(CurrentIdx))
                {
                    OutletKind = ERiverOutletKind::Loop;
                    break;
                }
                Visited.Add(CurrentIdx);

                const FWorldTileData& CurrentTile = WorldTiles[CurrentIdx];
                if (CurrentTile.Elevation < Step03SeaThreshold)
                {
                    OutletKind = ERiverOutletKind::Sea;
                    TerminalIndex = CurrentIdx;
                    break;
                }
                if (CurrentTile.WaterDepth >= Step03RiverLakeDepthThreshold)
                {
                    OutletKind = ERiverOutletKind::Lake;
                    TerminalIndex = CurrentIdx;
                    break;
                }
                if (RiverFlowUnits[CurrentIdx] > 0.0f)
                {
                    OutletKind = ERiverOutletKind::RiverMerge;
                    TerminalIndex = CurrentIdx;
                    bMerged = true;
                    break;
                }
                if (IsCoastalRiverOutlet(Current, WorldTiles, GridWidth, GridHeight))
                {
                    LocalPath.Add(CurrentIdx);
                    OutletKind = ERiverOutletKind::Coast;
                    TerminalIndex = FindAdjacentSeaTileIndex(Current, WorldTiles, GridWidth, GridHeight);
                    break;
                }

                LocalPath.Add(CurrentIdx);

                float BestNaturalScore = -MAX_flt;
                FIntPoint BestNaturalStep = Current;
                bool bFoundNaturalStep = false;

                for (const FIntPoint& Offset : GRiverNeighborOffsets)
                {
                    const FIntPoint Candidate(Current.X + Offset.X, Current.Y + Offset.Y);
                    if (!IsInGrid(Candidate, GridWidth, GridHeight))
                    {
                        continue;
                    }

                    const int32 CandidateIdx = TileIndex(Candidate.X, Candidate.Y);
                    if (Visited.Contains(CandidateIdx))
                    {
                        continue;
                    }

                    const FWorldTileData& CandidateTile = WorldTiles[CandidateIdx];
                    const bool bOutletNeighbor =
                        CandidateTile.Elevation < Step03SeaThreshold
                        || CandidateTile.WaterDepth >= Step03RiverLakeDepthThreshold
                        || RiverFlowUnits[CandidateIdx] > 0.0f
                        || IsCoastalRiverOutlet(Candidate, WorldTiles, GridWidth, GridHeight);

                    if (!bOutletNeighbor && CandidateTile.Elevation >= CurrentTile.Elevation - 0.001f)
                    {
                        continue;
                    }

                    const float Score = EvaluateStepScore(Current, Candidate, false, Source);
                    if (Score > BestNaturalScore)
                    {
                        BestNaturalScore = Score;
                        BestNaturalStep = Candidate;
                        bFoundNaturalStep = true;
                    }
                }

                if (bFoundNaturalStep)
                {
                    Current = BestNaturalStep;
                    continue;
                }

                const int32 SearchRadius = (LocalPath.Num() < MinRiverLengthTiles)
                    ? FMath::Min(DrainageSearchRadius + 4, 20)
                    : DrainageSearchRadius;
                const FDrainageSearchResult DrainageResult = FindDrainageTarget(Current, Visited, SearchRadius);

                UE_LOG(LogTemp, Log,
                    TEXT("[RiverGen] Drainage search Current=(%d,%d) Found=%s Target=(%d,%d) Reason=%s"),
                    Current.X,
                    Current.Y,
                    DrainageResult.bFound ? TEXT("Yes") : TEXT("No"),
                    DrainageResult.Target.X,
                    DrainageResult.Target.Y,
                    *DrainageResult.Reason);

                if (DrainageResult.bFound)
                {
                    float BestTargetedScore = -MAX_flt;
                    FIntPoint BestTargetedStep = Current;
                    bool bFoundTargetedStep = false;

                    for (const FIntPoint& Offset : GRiverNeighborOffsets)
                    {
                        const FIntPoint Candidate(Current.X + Offset.X, Current.Y + Offset.Y);
                        if (!IsInGrid(Candidate, GridWidth, GridHeight))
                        {
                            continue;
                        }

                        const int32 CandidateIdx = TileIndex(Candidate.X, Candidate.Y);
                        if (Visited.Contains(CandidateIdx))
                        {
                            continue;
                        }

                        const float Score = EvaluateStepScore(Current, Candidate, true, DrainageResult.Target);
                        if (Score > BestTargetedScore)
                        {
                            BestTargetedScore = Score;
                            BestTargetedStep = Candidate;
                            bFoundTargetedStep = true;
                        }
                    }

                    if (bFoundTargetedStep)
                    {
                        Current = BestTargetedStep;
                        continue;
                    }
                }

                const int32 LakeIndex = LocalPath.Pop(false);
                EndPoint = FIntPoint(LakeIndex % GridWidth, LakeIndex / GridWidth);
                TerminalIndex = LakeIndex;
                OutletKind = ERiverOutletKind::LakeEndpoint;
                bLakeCreated = true;
                break;
            }

            if ((OutletKind == ERiverOutletKind::None || OutletKind == ERiverOutletKind::Loop)
                && !LocalPath.IsEmpty())
            {
                const int32 LakeIndex = LocalPath.Pop(false);
                EndPoint = FIntPoint(LakeIndex % GridWidth, LakeIndex / GridWidth);
                TerminalIndex = LakeIndex;
                OutletKind = ERiverOutletKind::LakeEndpoint;
                bLakeCreated = true;
            }
            else if (OutletKind == ERiverOutletKind::None)
            {
                OutletKind = ERiverOutletKind::MaxLength;
            }

            if (LocalPath.Num() < 2 && !bMerged)
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[RiverGen] River source=(%d,%d) End=(%d,%d) Length=%d Outlet=%s Merged=%s"),
                    Source.X,
                    Source.Y,
                    EndPoint.X,
                    EndPoint.Y,
                    LocalPath.Num(),
                    RiverOutletKindToString(OutletKind),
                    bMerged ? TEXT("Yes") : TEXT("No"));
                continue;
            }

            for (int32 PathStep = 0; PathStep < LocalPath.Num(); ++PathStep)
            {
                const int32 PathTileIdx = LocalPath[PathStep];
                FWorldTileData& PathTile = WorldTiles[PathTileIdx];

                if (RiverFlowUnits[PathTileIdx] <= KINDA_SMALL_NUMBER)
                {
                    PathTile.Elevation = FMath::Max(Step03SeaThreshold, PathTile.Elevation - Step03RiverChannelDepression);
                }

                RiverFlowUnits[PathTileIdx] += 1.0f;
                RiverLongestRun[PathTileIdx] = FMath::Max(RiverLongestRun[PathTileIdx], PathStep + 1);
                PathTile.WaterDepth = FMath::Max(
                    PathTile.WaterDepth,
                    ComputeRiverWaterDepth(RiverFlowUnits[PathTileIdx], RiverLongestRun[PathTileIdx]));

                RiverDownstream[PathTileIdx] = (PathStep + 1 < LocalPath.Num())
                    ? LocalPath[PathStep + 1]
                    : TerminalIndex;
            }

            if (bMerged && TerminalIndex != INDEX_NONE)
            {
                int32 DownstreamIdx = TerminalIndex;
                int32 DownstreamDistance = LocalPath.Num() + 1;
                int32 Guard = 0;

                while (WorldTiles.IsValidIndex(DownstreamIdx)
                    && RiverFlowUnits[DownstreamIdx] > 0.0f
                    && Guard < MaxRiverLengthTiles)
                {
                    RiverFlowUnits[DownstreamIdx] += 1.0f;
                    RiverLongestRun[DownstreamIdx] = FMath::Max(RiverLongestRun[DownstreamIdx], DownstreamDistance);
                    WorldTiles[DownstreamIdx].WaterDepth = FMath::Max(
                        WorldTiles[DownstreamIdx].WaterDepth,
                        ComputeRiverWaterDepth(RiverFlowUnits[DownstreamIdx], RiverLongestRun[DownstreamIdx]));

                    const int32 NextDownstreamIdx = RiverDownstream[DownstreamIdx];
                    if (NextDownstreamIdx == INDEX_NONE
                        || NextDownstreamIdx == DownstreamIdx
                        || !WorldTiles.IsValidIndex(NextDownstreamIdx))
                    {
                        break;
                    }

                    if (WorldTiles[NextDownstreamIdx].Elevation < Step03SeaThreshold
                        || WorldTiles[NextDownstreamIdx].WaterDepth >= Step03RiverLakeDepthThreshold)
                    {
                        break;
                    }

                    DownstreamIdx = NextDownstreamIdx;
                    ++DownstreamDistance;
                    ++Guard;
                }
            }

            if (bLakeCreated && WorldTiles.IsValidIndex(TerminalIndex))
            {
                FWorldTileData& LakeTile = WorldTiles[TerminalIndex];
                LakeTile.WaterDepth = FMath::Max(LakeTile.WaterDepth, 0.18f);
                LakeTile.Elevation = FMath::Max(Step03SeaThreshold, LakeTile.Elevation - 0.01f);

                const FIntPoint LakePoint(TerminalIndex % GridWidth, TerminalIndex / GridWidth);
                for (const FIntPoint& Offset : GRiverNeighborOffsets)
                {
                    const FIntPoint Neighbor(LakePoint.X + Offset.X, LakePoint.Y + Offset.Y);
                    if (!IsInGrid(Neighbor, GridWidth, GridHeight))
                    {
                        continue;
                    }

                    const int32 NeighborIdx = TileIndex(Neighbor.X, Neighbor.Y);
                    if (RiverFlowUnits[NeighborIdx] > 0.0f)
                    {
                        continue;
                    }

                    FWorldTileData& NeighborTile = WorldTiles[NeighborIdx];
                    if (NeighborTile.Elevation < Step03SeaThreshold
                        || NeighborTile.WaterDepth >= Step03RiverLakeDepthThreshold
                        || IsVolcanicRiverBlocked(Neighbor, Volcanoes))
                    {
                        continue;
                    }

                    if (FMath::Abs(NeighborTile.Elevation - LakeTile.Elevation) < 0.025f)
                    {
                        NeighborTile.WaterDepth = FMath::Max(NeighborTile.WaterDepth, Step03RiverLakeDepthThreshold);
                    }
                }
            }

            TotalRiverTiles += LocalPath.Num();

            UE_LOG(LogTemp, Log,
                TEXT("[RiverGen] River source=(%d,%d) End=(%d,%d) Length=%d Outlet=%s Merged=%s"),
                Source.X,
                Source.Y,
                EndPoint.X,
                EndPoint.Y,
                LocalPath.Num(),
                RiverOutletKindToString(OutletKind),
                bMerged ? TEXT("Yes") : TEXT("No"));
        }

        UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step03 Phase D: %d river sources, %d river tiles"),
            SelectedSources.Num(), TotalRiverTiles);
    }

    // ── Phase E: Lake placement using basin noise ──
    {
        const float BasinNoiseOff = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -5000.f, 5000.f);
        const float BasinScale = 0.008f;
        const float BasinThreshold = 0.70f;
        int32 LakeCount = 0;

        for (int32 Y = 3; Y < GridHeight - 3; ++Y)
        {
            for (int32 X = 3; X < GridWidth - 3; ++X)
            {
                FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];
                if (Tile.Elevation < 0.50f || Tile.Elevation > 0.62f) continue;
                if (Tile.WaterDepth > 0.0f) continue; // Already river/lake

                const float BasinNoise = FMath::PerlinNoise2D(FVector2D(
                    (X + BasinNoiseOff) * BasinScale, (Y + BasinNoiseOff + 2000.f) * BasinScale));
                if (BasinNoise < BasinThreshold) continue;

                // Check if this is a local depression
                bool bIsBasin = true;
                for (int32 DY = -2; DY <= 2 && bIsBasin; ++DY)
                {
                    for (int32 DX = -2; DX <= 2 && bIsBasin; ++DX)
                    {
                        if (DX == 0 && DY == 0) continue;
                        const int32 NX = X + DX, NY = Y + DY;
                        if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight) continue;
                        if (WorldTiles[TileIndex(NX, NY)].Elevation < Tile.Elevation - 0.02f)
                            bIsBasin = false;
                    }
                }

                if (bIsBasin)
                {
                    Tile.WaterDepth = 0.25f;
                    Tile.Elevation = FMath::Max(0.48f, Tile.Elevation - 0.04f);
                    ++LakeCount;

                    // Expand lake to adjacent similar-elevation tiles
                    for (int32 DY = -1; DY <= 1; ++DY)
                    {
                        for (int32 DX = -1; DX <= 1; ++DX)
                        {
                            if (DX == 0 && DY == 0) continue;
                            const int32 NX = X + DX, NY = Y + DY;
                            if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight) continue;
                            FWorldTileData& Adj = WorldTiles[TileIndex(NX, NY)];
                            if (Adj.WaterDepth <= 0.0f && FMath::Abs(Adj.Elevation - Tile.Elevation) < 0.04f)
                            {
                                Adj.WaterDepth = 0.15f;
                                Adj.Elevation = FMath::Max(0.48f, Adj.Elevation - 0.03f);
                            }
                        }
                    }
                }
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step03 Phase E: %d lake basins"), LakeCount);
    }

    // ── Phase F: Canyon carving near mountain ranges ──
    {
        const float CanyonNoiseOff = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -5000.f, 5000.f);
        const float CanyonScale = 0.018f;
        const float CanyonThreshold = 0.88f;
        const float CanyonDepression = 0.08f;
        int32 CanyonCount = 0;

        for (int32 Y = 1; Y < GridHeight - 1; ++Y)
        {
            for (int32 X = 1; X < GridWidth - 1; ++X)
            {
                FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];
                if (Tile.Elevation < 0.58f || Tile.Elevation > 0.78f) continue;

                // Check proximity to a mountain range
                const FVector2D TilePos{ static_cast<float>(X), static_cast<float>(Y) };
                float MinRangeDist = 9999.f;
                for (const FMountainRange& Range : MountainRanges)
                {
                    const FVector2D End = Range.Start + Range.Direction * Range.Length;
                    MinRangeDist = FMath::Min(MinRangeDist, DistanceToSegment2D(TilePos, Range.Start, End));
                }

                if (MinRangeDist > 20.0f) continue; // Too far from mountains

                // Canyon noise — abs-inverted for narrow channels
                const float CanyonNoise = 1.0f - FMath::Abs(FMath::PerlinNoise2D(FVector2D(
                    (X + CanyonNoiseOff) * CanyonScale,
                    (Y + CanyonNoiseOff + 3000.f) * CanyonScale)));

                if (CanyonNoise > CanyonThreshold)
                {
                    Tile.Elevation -= CanyonDepression;
                    Tile.Elevation = FMath::Max(0.48f, Tile.Elevation);
                    Tile.WaterDepth = FMath::Max(Tile.WaterDepth, 0.02f); // Mark as canyon (tiny water depth)
                    ++CanyonCount;
                }
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step03 Phase F: %d canyon tiles"), CanyonCount);
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step03 done. Provinces=%d Ranges=%d Volcanoes=%d"),
        MountainProvinces.Num(), MountainRanges.Num(), Volcanoes.Num());
}

// ════════════════════════════════════════════════════════════════════
// STEP 04 — Climate + Whittaker Biomes (v3.1)
//
// Four passes:
//   1. Temperature (latitude − lapse rate) + base Rainfall
//   2. Rain Shadow (upwind raycast)
//   3. Water feature biome assignment (River, Lake, Canyon, Lava)
//   4. Whittaker Diagram for remaining land tiles
// ════════════════════════════════════════════════════════════════════

void UWorldGenPipeline::Step04_AssignBiomes()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step04: Climate + Whittaker biomes..."));

    WindConfig = CurrentConfig.WindSettings;
    const FVector2D WindDir = WindConfig.PrevailingDirection.GetSafeNormal();
    const float LapseRate = FMath::Clamp(WindConfig.LapseRate, 0.0f, 1.0f);
    const float ShadowStrength = FMath::Clamp(WindConfig.RainShadowStrength, 0.0f, 1.0f);
    const float ShadowReach = FMath::Max(1.0f, WindConfig.RainShadowReach);

    const float SeaThreshold = 0.48f;
    const float DeepOceanThreshold = 0.30f;
    const float OceanThreshold = 0.40f;
    const float CoastalThreshold = 0.515f;

    const float TempOffX = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_BIOME, -10000.f, 10000.f);
    const float TempOffY = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_BIOME, -10000.f, 10000.f);
    const float RainOffX = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_BIOME, -10000.f, 10000.f);
    const float RainOffY = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_BIOME, -10000.f, 10000.f);

    // ── PASS 1: Temperature + base Rainfall ──
    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];
            const float SX = static_cast<float>(X), SY = static_cast<float>(Y);

            const float Latitude01 = 1.0f - FMath::Abs((SY / FMath::Max(1.0f, static_cast<float>(GridHeight - 1))) * 2.0f - 1.0f);
            const float TempNoise01 = (FMath::PerlinNoise2D(FVector2D((SX + TempOffX) * 0.010f, (SY + TempOffY) * 0.010f)) + 1.0f) * 0.5f;
            Tile.Temperature = FMath::Clamp(Latitude01 * 0.70f + TempNoise01 * 0.30f - Tile.Elevation * LapseRate, 0.0f, 1.0f);

            const float RainA = (FMath::PerlinNoise2D(FVector2D((SX + RainOffX) * 0.013f, (SY + RainOffY) * 0.013f)) + 1.0f) * 0.5f;
            const float RainB = (FMath::PerlinNoise2D(FVector2D((SX + RainOffX - 1500.f) * 0.026f, (SY + RainOffY - 1500.f) * 0.026f)) + 1.0f) * 0.5f;
            Tile.Rainfall = FMath::Clamp(RainA * 0.7f + RainB * 0.3f + (1.0f - Tile.Elevation) * 0.10f, 0.0f, 1.0f);
        }
    }

    // ── PASS 2: Rain Shadow ──
    {
        const float MountainElevThreshold = 0.68f;
        for (int32 Y = 0; Y < GridHeight; ++Y)
        {
            for (int32 X = 0; X < GridWidth; ++X)
            {
                FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];
                if (Tile.Elevation < SeaThreshold) continue;

                float MaxUpwindElev = 0.0f;
                float ClosestMountainDist = ShadowReach + 1.0f;
                const int32 Steps = FMath::CeilToInt(ShadowReach);
                for (int32 S = 1; S <= Steps; ++S)
                {
                    const int32 SX = X - FMath::RoundToInt(WindDir.X * S);
                    const int32 SY = Y - FMath::RoundToInt(WindDir.Y * S);
                    if (SX < 0 || SX >= GridWidth || SY < 0 || SY >= GridHeight) break;
                    const float SampleElev = WorldTiles[TileIndex(SX, SY)].Elevation;
                    if (SampleElev > MountainElevThreshold && SampleElev > MaxUpwindElev)
                    {
                        MaxUpwindElev = SampleElev;
                        ClosestMountainDist = FMath::Min(ClosestMountainDist, static_cast<float>(S));
                    }
                }
                if (MaxUpwindElev > MountainElevThreshold)
                {
                    const float DistFade = 1.0f - FMath::Clamp(ClosestMountainDist / ShadowReach, 0.0f, 1.0f);
                    const float BarrierStrength = (MaxUpwindElev - MountainElevThreshold) / (1.0f - MountainElevThreshold);
                    Tile.Rainfall *= FMath::Clamp(1.0f - ShadowStrength * DistFade * BarrierStrength, 0.0f, 1.0f);
                }
            }
        }
    }

    // ── PASS 3 & 4: Biome Assignment ──
    // Pre-cache all tags
    const FGameplayTag DeepOceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.DeepOcean")));
    const FGameplayTag OceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Ocean")));
    const FGameplayTag SeaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Sea")));
    const FGameplayTag CoastalTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")));
    const FGameplayTag MountainTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")));
    const FGameplayTag SnowPeakTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.SnowPeak")));
    const FGameplayTag AlpineTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Alpine")));
    const FGameplayTag HillsTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Hills")));
    const FGameplayTag TundraTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Tundra")));
    const FGameplayTag TaigaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Taiga")));
    const FGameplayTag DesertTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Desert")));
    const FGameplayTag SavannaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Savanna")));
    const FGameplayTag JungleTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Jungle")));
    const FGameplayTag ForestTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Forest")));
    const FGameplayTag WoodlandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Woodland")));
    const FGameplayTag GrasslandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Grassland")));
    const FGameplayTag PlainsTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Plains")));
    const FGameplayTag MarshTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Marsh")));
    const FGameplayTag SwampTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Swamp")));

    // New biome tags (v3.1)
    const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);
    const FGameplayTag LavaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);
    const FGameplayTag RiverTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.River")), false);
    const FGameplayTag LakeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lake")), false);
    const FGameplayTag CanyonTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Canyon")), false);
    const FGameplayTag SteppeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Steppe")), false);
    const FGameplayTag IceSheetTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.IceSheet")), false);
    const FGameplayTag ShrublandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Shrubland")), false);

    // Build lava flow mask from active volcanoes
    TSet<int32> LavaFlowTiles;
    TSet<int32> LavaCraterTiles;
    for (const FVolcanoData& V : Volcanoes)
    {
        const int32 CX = FMath::RoundToInt(V.Center.X);
        const int32 CY = FMath::RoundToInt(V.Center.Y);

        if (V.bActive)
        {
            // Lava crater (inner zone)
            const int32 CraterR = FMath::CeilToInt(V.ConeRadius * V.CraterRadiusFraction) + 1;
            for (int32 DY = -CraterR; DY <= CraterR; ++DY)
            {
                for (int32 DX = -CraterR; DX <= CraterR; ++DX)
                {
                    const int32 TX = CX + DX, TY = CY + DY;
                    if (TX < 0 || TX >= GridWidth || TY < 0 || TY >= GridHeight) continue;
                    if (FMath::Sqrt(float(DX * DX + DY * DY)) <= V.ConeRadius * V.CraterRadiusFraction)
                        LavaCraterTiles.Add(TileIndex(TX, TY));
                }
            }

            // Volcanic zone (outer cone)
            const int32 FlowR = FMath::CeilToInt(V.LavaFlowRadius);
            for (int32 DY = -FlowR; DY <= FlowR; ++DY)
            {
                for (int32 DX = -FlowR; DX <= FlowR; ++DX)
                {
                    const int32 TX = CX + DX, TY = CY + DY;
                    if (TX < 0 || TX >= GridWidth || TY < 0 || TY >= GridHeight) continue;
                    if (FMath::Sqrt(float(DX * DX + DY * DY)) <= V.LavaFlowRadius)
                        LavaFlowTiles.Add(TileIndex(TX, TY));
                }
            }
        }
        else
        {
            // Inactive volcano — just volcanic terrain
            const int32 FlowR = FMath::CeilToInt(V.ConeRadius);
            for (int32 DY = -FlowR; DY <= FlowR; ++DY)
            {
                for (int32 DX = -FlowR; DX <= FlowR; ++DX)
                {
                    const int32 TX = CX + DX, TY = CY + DY;
                    if (TX < 0 || TX >= GridWidth || TY < 0 || TY >= GridHeight) continue;
                    if (FMath::Sqrt(float(DX * DX + DY * DY)) <= V.ConeRadius)
                        LavaFlowTiles.Add(TileIndex(TX, TY));
                }
            }
        }
    }

    // ── Assign biome to every tile ──
    int32 RiverCount = 0, LakeCount = 0, CanyonCount = 0, LavaCount = 0;

    for (int32 Idx = 0; Idx < WorldTiles.Num(); ++Idx)
    {
        FWorldTileData& Tile = WorldTiles[Idx];

        // ── Ocean biomes ──
        if (Tile.Elevation < DeepOceanThreshold)
            Tile.BiomeType = DeepOceanTag;
        else if (Tile.Elevation < OceanThreshold)
            Tile.BiomeType = OceanTag;
        else if (Tile.Elevation < SeaThreshold)
            Tile.BiomeType = SeaTag;

        // ── Water feature overrides (must check before land biomes) ──
        else if (LavaCraterTiles.Contains(Idx) && LavaTag.IsValid())
        {
            Tile.BiomeType = LavaTag;
            ++LavaCount;
        }
        else if (LavaFlowTiles.Contains(Idx) && VolcanicTag.IsValid())
            Tile.BiomeType = VolcanicTag;
        else if (Tile.WaterDepth >= 0.15f && LakeTag.IsValid())
        {
            Tile.BiomeType = LakeTag;
            ++LakeCount;
        }
        else if (Tile.WaterDepth >= 0.04f && RiverTag.IsValid())
        {
            Tile.BiomeType = RiverTag;
            ++RiverCount;
        }
        else if (Tile.WaterDepth > 0.0f && Tile.WaterDepth < 0.04f && CanyonTag.IsValid())
        {
            Tile.BiomeType = CanyonTag;
            ++CanyonCount;
        }

        // ── Coastal ──
        else if (Tile.Elevation < CoastalThreshold)
            Tile.BiomeType = CoastalTag;

        // ── Highland biomes ──
        else if (Tile.Elevation > 0.82f && Tile.Temperature < 0.35f)
            Tile.BiomeType = SnowPeakTag;
        else if (Tile.Elevation > 0.76f)
            Tile.BiomeType = MountainTag;
        else if (Tile.Elevation > 0.68f && Tile.Temperature < 0.40f)
            Tile.BiomeType = AlpineTag;
        else if (Tile.Elevation > 0.68f)
            Tile.BiomeType = MountainTag;
        else if (Tile.Elevation > 0.58f)
            Tile.BiomeType = HillsTag;

        // ── Whittaker Diagram ──
        else if (Tile.Temperature < 0.20f)
        {
            if (Tile.Rainfall > 0.50f && IceSheetTag.IsValid()) Tile.BiomeType = IceSheetTag;
            else if (Tile.Rainfall > 0.40f) Tile.BiomeType = TaigaTag;
            else Tile.BiomeType = TundraTag;
        }
        else if (Tile.Temperature < 0.40f)
        {
            if (Tile.Rainfall > 0.65f) Tile.BiomeType = ForestTag;
            else if (Tile.Rainfall > 0.40f) Tile.BiomeType = TaigaTag;
            else Tile.BiomeType = SteppeTag.IsValid() ? SteppeTag : GrasslandTag;
        }
        else if (Tile.Temperature < 0.65f)
        {
            if (Tile.Rainfall > 0.70f) Tile.BiomeType = ForestTag;
            else if (Tile.Rainfall > 0.45f) Tile.BiomeType = WoodlandTag;
            else if (Tile.Rainfall > 0.25f) Tile.BiomeType = ShrublandTag.IsValid() ? ShrublandTag : PlainsTag;
            else Tile.BiomeType = DesertTag;
        }
        else
        {
            if (Tile.Rainfall > 0.75f) Tile.BiomeType = (Tile.Elevation < 0.57f) ? SwampTag : JungleTag;
            else if (Tile.Rainfall > 0.55f) Tile.BiomeType = (Tile.Elevation < 0.57f) ? MarshTag : JungleTag;
            else if (Tile.Rainfall > 0.30f) Tile.BiomeType = SavannaTag;
            else Tile.BiomeType = DesertTag;
        }

        // ── Voxel profile assignment ──
        Tile.VoxelProfile.bSmoothSDF = true;
        const bool bIsWaterTile = Tile.BiomeType.MatchesTag(DeepOceanTag) || Tile.BiomeType.MatchesTag(OceanTag) || Tile.BiomeType.MatchesTag(SeaTag);

        if (bIsWaterTile)
        {
            Tile.VoxelProfile.NoiseAmplitude = 8.0f;
            Tile.VoxelProfile.NoiseFrequency = 0.005f;
            Tile.VoxelProfile.OctaveCount = 1;
            Tile.VoxelProfile.bEnableCaves = false;
            // v4.0: No caves or canyons underwater
            Tile.VoxelProfile.CaveIntensity = 0.0f;
            Tile.VoxelProfile.CanyonIntensity = 0.0f;
        }
        else
        {
            const float EA = FMath::Clamp(Tile.Elevation, 0.0f, 1.0f);
            Tile.VoxelProfile.NoiseAmplitude = FMath::Lerp(20.0f, 220.0f, EA);
            Tile.VoxelProfile.NoiseFrequency = FMath::Lerp(0.008f, 0.028f, EA);
            Tile.VoxelProfile.OctaveCount = (EA > 0.75f) ? 5 : ((EA > 0.45f) ? 4 : 3);
            Tile.VoxelProfile.bEnableCaves = (Tile.Elevation > 0.55f);

            // v4.0: Default 3D SDF parameters for generic land
            Tile.VoxelProfile.CaveIntensity = (Tile.Elevation > 0.55f) ? 0.3f : 0.0f;
            Tile.VoxelProfile.CaveWormFrequency = 0.012f;
            Tile.VoxelProfile.CaveMinDepth = 8.0f;
            Tile.VoxelProfile.CaveMaxDepth = 96.0f;
            Tile.VoxelProfile.CaveZScale = 1.0f;
            Tile.VoxelProfile.CanyonIntensity = 0.0f;
            Tile.VoxelProfile.CanyonMaxDepth = 40.0f;
            Tile.VoxelProfile.CanyonSharpness = 3.0f;
            Tile.VoxelProfile.UndergroundFloorDepth = 200.0f;
            Tile.VoxelProfile.DensityBias = 0.0f;
            Tile.VoxelProfile.SurfaceRoughnessScale = 1.0f;

            if (Tile.BiomeType.MatchesTag(MountainTag) || Tile.BiomeType.MatchesTag(SnowPeakTag) || Tile.BiomeType.MatchesTag(AlpineTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Max(Tile.VoxelProfile.NoiseAmplitude, 520.0f);
                Tile.VoxelProfile.NoiseFrequency = FMath::Max(Tile.VoxelProfile.NoiseFrequency, 0.045f);
                Tile.VoxelProfile.OctaveCount = FMath::Max(Tile.VoxelProfile.OctaveCount, 6);
                Tile.VoxelProfile.bEnableCaves = true;
                // v4.0: Mountain caves — moderate density, medium depth
                Tile.VoxelProfile.CaveIntensity = 0.6f;
                Tile.VoxelProfile.CaveMaxDepth = 96.0f;
                Tile.VoxelProfile.CanyonIntensity = 0.3f;
                Tile.VoxelProfile.CanyonMaxDepth = 30.0f;
            }
            else if (Tile.BiomeType.MatchesTag(HillsTag))
            {
                // v4.0: Hills — light caves
                Tile.VoxelProfile.CaveIntensity = 0.4f;
                Tile.VoxelProfile.CaveMaxDepth = 64.0f;
            }
            else if (Tile.BiomeType.MatchesTag(CoastalTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Min(Tile.VoxelProfile.NoiseAmplitude, 28.0f);
                Tile.VoxelProfile.NoiseFrequency = FMath::Min(Tile.VoxelProfile.NoiseFrequency, 0.010f);
                Tile.VoxelProfile.OctaveCount = 2;
                Tile.VoxelProfile.bEnableCaves = false;
                // v4.0: No caves or canyons at coast
                Tile.VoxelProfile.CaveIntensity = 0.0f;
                Tile.VoxelProfile.CanyonIntensity = 0.0f;
            }
            else if (Tile.BiomeType.MatchesTag(DesertTag) || Tile.BiomeType.MatchesTag(SavannaTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Min(Tile.VoxelProfile.NoiseAmplitude, 65.0f);
                Tile.VoxelProfile.NoiseFrequency = FMath::Min(Tile.VoxelProfile.NoiseFrequency, 0.014f);
                Tile.VoxelProfile.OctaveCount = 3;
                // v4.0: Desert — sparse caves, strong canyons
                Tile.VoxelProfile.CaveIntensity = 0.1f;
                Tile.VoxelProfile.CaveMaxDepth = 48.0f;
                Tile.VoxelProfile.CanyonIntensity = 1.0f;
                Tile.VoxelProfile.CanyonMaxDepth = 50.0f;
                Tile.VoxelProfile.CanyonSharpness = 4.0f;
            }
            else if (Tile.BiomeType.MatchesTag(ForestTag) || Tile.BiomeType.MatchesTag(WoodlandTag))
            {
                // v4.0: Forests — light caves, no canyons
                Tile.VoxelProfile.CaveIntensity = 0.3f;
                Tile.VoxelProfile.CaveMaxDepth = 64.0f;
            }
            else if (Tile.BiomeType.MatchesTag(JungleTag))
            {
                // v4.0: Jungle — moderate caves (cenote-like)
                Tile.VoxelProfile.CaveIntensity = 0.5f;
                Tile.VoxelProfile.CaveMaxDepth = 80.0f;
            }
            else if (Tile.BiomeType.MatchesTag(SwampTag) || Tile.BiomeType.MatchesTag(MarshTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Min(Tile.VoxelProfile.NoiseAmplitude, 18.0f);
                Tile.VoxelProfile.NoiseFrequency = FMath::Min(Tile.VoxelProfile.NoiseFrequency, 0.008f);
                Tile.VoxelProfile.OctaveCount = 2;
                Tile.VoxelProfile.bEnableCaves = false;
                // v4.0: Swamps — shallow caves only
                Tile.VoxelProfile.CaveIntensity = 0.2f;
                Tile.VoxelProfile.CaveMaxDepth = 32.0f;
                Tile.VoxelProfile.CanyonIntensity = 0.0f;
            }
            else if (Tile.BiomeType.MatchesTag(TundraTag) || (IceSheetTag.IsValid() && Tile.BiomeType.MatchesTag(IceSheetTag)))
            {
                // v4.0: Frozen — no caves, no canyons
                Tile.VoxelProfile.CaveIntensity = 0.0f;
                Tile.VoxelProfile.CanyonIntensity = 0.0f;
            }
            else if (VolcanicTag.IsValid() && Tile.BiomeType.MatchesTag(VolcanicTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Max(Tile.VoxelProfile.NoiseAmplitude, 380.0f);
                Tile.VoxelProfile.NoiseFrequency = FMath::Max(Tile.VoxelProfile.NoiseFrequency, 0.035f);
                Tile.VoxelProfile.OctaveCount = FMath::Max(Tile.VoxelProfile.OctaveCount, 5);
                Tile.VoxelProfile.bEnableCaves = true;
                // v4.0: Volcanic — dense caves with horizontal bias (lava tubes)
                Tile.VoxelProfile.CaveIntensity = 0.8f;
                Tile.VoxelProfile.CaveMaxDepth = 80.0f;
                Tile.VoxelProfile.CaveZScale = 0.3f;  // horizontal bias
                Tile.VoxelProfile.CanyonIntensity = 0.2f;
                Tile.VoxelProfile.CanyonMaxDepth = 20.0f;
            }
            else if (CanyonTag.IsValid() && Tile.BiomeType.MatchesTag(CanyonTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Max(Tile.VoxelProfile.NoiseAmplitude, 300.0f);
                Tile.VoxelProfile.NoiseFrequency = 0.032f;
                Tile.VoxelProfile.OctaveCount = 5;
                Tile.VoxelProfile.bEnableCaves = true;
                // v4.0: Canyon biome — very strong canyon carving
                Tile.VoxelProfile.CaveIntensity = 0.4f;
                Tile.VoxelProfile.CaveMaxDepth = 64.0f;
                Tile.VoxelProfile.CanyonIntensity = 1.5f;
                Tile.VoxelProfile.CanyonMaxDepth = 60.0f;
                Tile.VoxelProfile.CanyonSharpness = 2.5f;
            }
            else if (Tile.BiomeType.MatchesTag(TaigaTag))
            {
                // v4.0: Taiga — sparse caves
                Tile.VoxelProfile.CaveIntensity = 0.2f;
                Tile.VoxelProfile.CaveMaxDepth = 48.0f;
            }
        }
    }

    // ── Diagnostic ──
    int32 DeepOceanCount = 0;
    int32 OceanCount = 0;
    int32 SeaCount = 0;
    int32 CoastalCount = 0;
    int32 LakeDiagCount = 0;
    int32 RiverDiagCount = 0;

    for (const FWorldTileData& Tile : WorldTiles)
    {
        if (Tile.BiomeType.MatchesTag(DeepOceanTag)) ++DeepOceanCount;
        else if (Tile.BiomeType.MatchesTag(OceanTag)) ++OceanCount;
        else if (Tile.BiomeType.MatchesTag(SeaTag)) ++SeaCount;
        else if (Tile.BiomeType.MatchesTag(CoastalTag)) ++CoastalCount;

        if (LakeTag.IsValid() && Tile.BiomeType.MatchesTag(LakeTag)) ++LakeDiagCount;
        if (RiverTag.IsValid() && Tile.BiomeType.MatchesTag(RiverTag)) ++RiverDiagCount;
    }

    const int32 TotalTiles = WorldTiles.Num();
    const int32 WaterCount = DeepOceanCount + OceanCount + SeaCount + LakeDiagCount + RiverDiagCount;
    const int32 LandCount = FMath::Max(0, TotalTiles - WaterCount);
    const float WaterPct = TotalTiles > 0 ? (static_cast<float>(WaterCount) * 100.0f) / static_cast<float>(TotalTiles) : 0.0f;
    const float LandPct = TotalTiles > 0 ? (static_cast<float>(LandCount) * 100.0f) / static_cast<float>(TotalTiles) : 0.0f;

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Step04 done. Water=%d (%d%%) Land=%d (%d%%) Rivers=%d Lakes=%d Canyons=%d Lava=%d"),
        WaterCount, FMath::RoundToInt(WaterPct),
        LandCount, FMath::RoundToInt(LandPct),
        RiverCount, LakeCount, CanyonCount, LavaCount);

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] WaterBalance Step04 WaterPct=%.1f LandPct=%.1f DeepOcean=%d Ocean=%d Sea=%d Coast=%d Lake=%d River=%d Land=%d"),
        WaterPct,
        LandPct,
        DeepOceanCount,
        OceanCount,
        SeaCount,
        CoastalCount,
        LakeDiagCount,
        RiverDiagCount,
        LandCount);

    if (WaterPct > 55.0f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] WARNING: Water-heavy world generated. WaterPct=%.1f"), WaterPct);
    }

    if (LandPct < 40.0f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] WARNING: Low playable land. LandPct=%.1f"), LandPct);
    }
}

// ════════════════════════════════════════════════════════════════════
// STEPS 05–16 (unchanged)
// ════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════
// LAKE-DESCRIPTOR-QUALITY-01: BuildLakeBasins
//
// Diagnose every WaterDepth-derived basin candidate after Step04 biome
// assignment, then accept only real inland multi-tile lake basins for
// Step05 handoff. This keeps candidate diagnostics visible without
// letting 1-tile lava/volcanic/lake artifacts reach the terrain snapshot.
// ════════════════════════════════════════════════════════════════════

void UWorldGenPipeline::BuildLakeBasins()
{
    LakeBasins.Reset();

    if (WorldTiles.IsEmpty() || GridWidth <= 0 || GridHeight <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LakeFix] BuildLakeBasins: no tile data — skipping."));
        return;
    }

    constexpr float WaterThreshold = 0.48f;
    constexpr float LakeWaterDepthThreshold = 0.15f;
    constexpr int32 MinGeneratedLakeBasinTiles = 3;
    constexpr int32 MinGeneratedLakeBasinWidthTiles = 2;
    constexpr int32 MinGeneratedLakeBasinHeightTiles = 2;
    constexpr int32 MaxLoggedCandidateBasins = 20;

    const FGameplayTag DeepOceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.DeepOcean")), false);
    const FGameplayTag OceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Ocean")), false);
    const FGameplayTag SeaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Sea")), false);
    const FGameplayTag CoastalTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")), false);
    const FGameplayTag RiverTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.River")), false);
    const FGameplayTag LakeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lake")), false);
    const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);
    const FGameplayTag LavaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);

    struct FLakeBasinQualitySummary
    {
        int32 CandidateBasins = 0;
        int32 Accepted = 0;
        int32 RejectedSmall = 0;
        int32 RejectedBiome = 0;
        int32 RejectedOcean = 0;
        int32 RejectedInvalid = 0;
        int32 FinalLakeTiles = 0;
    };

    const auto IsOceanLikeBiome = [&](const FGameplayTag& BiomeTag) -> bool
    {
        return (DeepOceanTag.IsValid() && BiomeTag.MatchesTag(DeepOceanTag))
            || (OceanTag.IsValid() && BiomeTag.MatchesTag(OceanTag))
            || (SeaTag.IsValid() && BiomeTag.MatchesTag(SeaTag))
            || (CoastalTag.IsValid() && BiomeTag.MatchesTag(CoastalTag));
    };

    const auto IsPotentialLakeCandidate = [&](const FWorldTileData& Tile) -> bool
    {
        return Tile.WaterDepth >= LakeWaterDepthThreshold && Tile.Elevation >= WaterThreshold;
    };

    const int32 TileCount = GridWidth * GridHeight;
    TArray<bool> Visited;
    Visited.SetNumZeroed(TileCount);

    FLakeBasinQualitySummary Summary;
    int32 NextAcceptedBasinID = 0;
    int32 LoggedCandidates = 0;

    TArray<FIntPoint> Queue;
    Queue.Reserve(256);

    TArray<FIntPoint> BasinCells;
    BasinCells.Reserve(256);

    for (int32 StartY = 0; StartY < GridHeight; ++StartY)
    {
        for (int32 StartX = 0; StartX < GridWidth; ++StartX)
        {
            const int32 StartIdx = StartY * GridWidth + StartX;
            if (Visited[StartIdx])
            {
                continue;
            }

            const FWorldTileData& StartTile = WorldTiles[StartIdx];
            if (!IsPotentialLakeCandidate(StartTile))
            {
                Visited[StartIdx] = true;
                continue;
            }

            const int32 CandidateBasinID = Summary.CandidateBasins++;

            FLakeBasinData Basin;
            Basin.MinTileX = StartX;
            Basin.MaxTileX = StartX;
            Basin.MinTileY = StartY;
            Basin.MaxTileY = StartY;

            BasinCells.Reset();

            int64 SumX = 0;
            int64 SumY = 0;
            float SumElevation = 0.0f;
            int32 LakeBiomeTileCount = 0;
            bool bHasVolcanicBiome = false;
            bool bHasLavaBiome = false;
            bool bHasOceanLikeBiome = false;

            Queue.Reset();
            Queue.Add(FIntPoint(StartX, StartY));
            Visited[StartIdx] = true;

            int32 Head = 0;
            while (Head < Queue.Num())
            {
                const FIntPoint Current = Queue[Head++];
                const int32 Idx = Current.Y * GridWidth + Current.X;
                const FWorldTileData& CurrentTile = WorldTiles[Idx];

                BasinCells.Add(Current);
                ++Basin.TileCount;
                SumX += Current.X;
                SumY += Current.Y;
                SumElevation += CurrentTile.Elevation;

                if (LakeTag.IsValid() && CurrentTile.BiomeType.MatchesTag(LakeTag))
                {
                    ++LakeBiomeTileCount;
                }
                if (VolcanicTag.IsValid() && CurrentTile.BiomeType.MatchesTag(VolcanicTag))
                {
                    bHasVolcanicBiome = true;
                }
                if (LavaTag.IsValid() && CurrentTile.BiomeType.MatchesTag(LavaTag))
                {
                    bHasLavaBiome = true;
                }
                if (IsOceanLikeBiome(CurrentTile.BiomeType))
                {
                    bHasOceanLikeBiome = true;
                }

                Basin.MinTileX = FMath::Min(Basin.MinTileX, Current.X);
                Basin.MaxTileX = FMath::Max(Basin.MaxTileX, Current.X);
                Basin.MinTileY = FMath::Min(Basin.MinTileY, Current.Y);
                Basin.MaxTileY = FMath::Max(Basin.MaxTileY, Current.Y);

                const int32 Neighbors[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
                for (const auto& Neighbor : Neighbors)
                {
                    const int32 NX = Current.X + Neighbor[0];
                    const int32 NY = Current.Y + Neighbor[1];
                    if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight)
                    {
                        continue;
                    }

                    const int32 NeighborIdx = NY * GridWidth + NX;
                    if (Visited[NeighborIdx])
                    {
                        continue;
                    }

                    Visited[NeighborIdx] = true;
                    if (IsPotentialLakeCandidate(WorldTiles[NeighborIdx]))
                    {
                        Queue.Add(FIntPoint(NX, NY));
                    }
                }
            }

            if (Basin.TileCount <= 0 || BasinCells.Num() == 0)
            {
                ++Summary.RejectedInvalid;
                continue;
            }

            const FVector2D Centroid(
                static_cast<float>(SumX) / static_cast<float>(Basin.TileCount),
                static_cast<float>(SumY) / static_cast<float>(Basin.TileCount));

            FIntPoint CenterCell = BasinCells[0];
            float BestDistSq = MAX_flt;
            for (const FIntPoint& Cell : BasinCells)
            {
                const float DX = static_cast<float>(Cell.X) - Centroid.X;
                const float DY = static_cast<float>(Cell.Y) - Centroid.Y;
                const float DistSq = DX * DX + DY * DY;
                if (DistSq < BestDistSq)
                {
                    BestDistSq = DistSq;
                    CenterCell = Cell;
                }
            }

            Basin.CenterTileX = CenterCell.X;
            Basin.CenterTileY = CenterCell.Y;
            Basin.WaterSurfaceElevation01 = SumElevation / static_cast<float>(Basin.TileCount);
            Basin.FloorElevation01 = FMath::Max(0.0f, Basin.WaterSurfaceElevation01 - 0.02f);
            Basin.ShoreFalloffTiles = 2.0f;
            Basin.SourceType = 1; // BasinNoise / generic inland water candidate

            const int32 BasinWidthTiles = Basin.MaxTileX - Basin.MinTileX + 1;
            const int32 BasinHeightTiles = Basin.MaxTileY - Basin.MinTileY + 1;
            const FGameplayTag CenterBiome = WorldTiles[TileIndex(Basin.CenterTileX, Basin.CenterTileY)].BiomeType;

            bool bAccepted = true;
            FString Reason = TEXT("Accepted");

            if (!FMath::IsFinite(Basin.WaterSurfaceElevation01)
                || !FMath::IsFinite(Basin.FloorElevation01)
                || Basin.WaterSurfaceElevation01 < WaterThreshold
                || Basin.WaterSurfaceElevation01 > 1.0f
                || Basin.FloorElevation01 > Basin.WaterSurfaceElevation01)
            {
                bAccepted = false;
                Reason = TEXT("InvalidWaterElevation");
                ++Summary.RejectedInvalid;
            }
            else if (IsOceanLikeBiome(CenterBiome) || bHasOceanLikeBiome)
            {
                bAccepted = false;
                Reason = TEXT("OceanOrCoastal");
                ++Summary.RejectedOcean;
            }
            else if ((LavaTag.IsValid() && CenterBiome.MatchesTag(LavaTag))
                || (VolcanicTag.IsValid() && CenterBiome.MatchesTag(VolcanicTag))
                || bHasLavaBiome
                || bHasVolcanicBiome)
            {
                bAccepted = false;
                Reason = bHasLavaBiome ? TEXT("LavaBiome") : TEXT("VolcanicBiome");
                ++Summary.RejectedBiome;
            }
            else if ((RiverTag.IsValid() && CenterBiome.MatchesTag(RiverTag))
                || !LakeTag.IsValid()
                || !CenterBiome.MatchesTag(LakeTag)
                || LakeBiomeTileCount != Basin.TileCount)
            {
                bAccepted = false;
                Reason = TEXT("NotLakeLike");
                ++Summary.RejectedBiome;
            }
            else if (Basin.TileCount < MinGeneratedLakeBasinTiles)
            {
                bAccepted = false;
                Reason = TEXT("TooSmall");
                ++Summary.RejectedSmall;
            }
            else if (BasinWidthTiles < MinGeneratedLakeBasinWidthTiles || BasinHeightTiles < MinGeneratedLakeBasinHeightTiles)
            {
                Reason = TEXT("ThinExtentNotEnforced");
            }

            if (bAccepted)
            {
                Basin.BasinID = NextAcceptedBasinID++;
                LakeBasins.Add(Basin);
                ++Summary.Accepted;
                Summary.FinalLakeTiles += Basin.TileCount;
            }

            if (LoggedCandidates < MaxLoggedCandidateBasins)
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[LakeFix] Candidate basin Basin=%d Tiles=%d Bounds=(%d,%d)-(%d,%d) Biome=%s WaterElev=%.3f Accepted=%s Reason=%s"),
                    CandidateBasinID,
                    Basin.TileCount,
                    Basin.MinTileX,
                    Basin.MinTileY,
                    Basin.MaxTileX,
                    Basin.MaxTileY,
                    *CenterBiome.ToString(),
                    Basin.WaterSurfaceElevation01,
                    bAccepted ? TEXT("Yes") : TEXT("No"),
                    *Reason);
                ++LoggedCandidates;
            }
        }
    }

    const int32 RejectedTotal = Summary.CandidateBasins - Summary.Accepted;

    UE_LOG(LogTemp, Log,
        TEXT("[LakeFix] Step03 LakeBasins=%d LakeTiles=%d CandidateBasins=%d Accepted=%d Rejected=%d"),
        LakeBasins.Num(),
        Summary.FinalLakeTiles,
        Summary.CandidateBasins,
        Summary.Accepted,
        RejectedTotal);

    UE_LOG(LogTemp, Log,
        TEXT("[LakeFix] Basin quality summary: Candidates=%d Accepted=%d RejectedSmall=%d RejectedBiome=%d RejectedOcean=%d RejectedInvalid=%d FinalLakeTiles=%d"),
        Summary.CandidateBasins,
        Summary.Accepted,
        Summary.RejectedSmall,
        Summary.RejectedBiome,
        Summary.RejectedOcean,
        Summary.RejectedInvalid,
        Summary.FinalLakeTiles);

    if (Summary.CandidateBasins > MaxLoggedCandidateBasins)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[LakeFix] Candidate basin logging truncated. Logged=%d Total=%d"),
            MaxLoggedCandidateBasins,
            Summary.CandidateBasins);
    }
}

bool UWorldGenPipeline::Step05_InitVoxelChunks()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step05: Init voxel chunks..."));

    if (!WorldRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step05 failed: WorldRef is null."));
        return false;
    }

    UVoxelWorldManager* VoxelMgr = WorldRef->GetSubsystem<UVoxelWorldManager>();
    if (!VoxelMgr)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step05 failed: UVoxelWorldManager not found."));
        return false;
    }

    VoxelMgr->SetWorldSeed(WorldSeed);
    VoxelMgr->SetPipelineMountainRanges(MountainRanges);
    VoxelMgr->SetPipelineVolcanoes(Volcanoes);
    VoxelMgr->SetPipelineLakeBasins(LakeBasins); // LAKE-FIX-01A

    // Diagnostic: verify resource data is already present before VoxelWorldManager
    // snapshots/caches the tile array.
    int32 TilesWithVeinData = 0;
    int32 TilesWithResourceTags = 0;

    for (const FWorldTileData& Tile : WorldTiles)
    {
        if (Tile.VeinConfig.PresentCategories.Num() > 0)
        {
            ++TilesWithVeinData;
        }

        if (Tile.ResourceTags.Num() > 0)
        {
            ++TilesWithResourceTags;
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[WorldGen] Step05 handoff diagnostic: VeinTiles=%d ResourceTagTiles=%d / %d"),
        TilesWithVeinData,
        TilesWithResourceTags,
        WorldTiles.Num());

    const bool bAccepted = VoxelMgr->GenerateTerrainFromTiles(
        WorldTiles,
        GridWidth,
        GridHeight,
        CurrentConfig.TerrainMode,
        CurrentConfig.BlockHeightStep);

    if (!bAccepted)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step05: terrain request was not accepted."));
        return false;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Step05 done. TerrainMode=%s BlockHeightStep=%d"),
        CurrentConfig.TerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
        CurrentConfig.BlockHeightStep);
    return true;
}

void UWorldGenPipeline::Step06_PCGDecorationPass()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step06 skipped: decorations are dispatched by WorldGenBootstrapActor after terrain-ready."));
}

void UWorldGenPipeline::Step07_ApplyWorldDriver()
{
    // IWorldDriver is a Phase 4 extension point.  When implemented, this step
    // will apply an IWorldDriver to post-process the tile grid (e.g. a Pangaea
    // driver could consolidate landmasses, a Water World driver could flood
    // low elevations).  No default driver is applied — tiles pass through as-is.
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step07: No IWorldDriver applied (Phase 4 extension point)."));
}

void UWorldGenPipeline::Step08_SeedResources()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step08: Seeding resources (v6.0 noise-driven)..."));

    // ── Gameplay tags ──
    const FGameplayTag StoneTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Stone")));
    const FGameplayTag WoodTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Wood")));
    const FGameplayTag OreTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Ore")));
    const FGameplayTag IronTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Iron")), false);
    const FGameplayTag CopperTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Copper")), false);
    const FGameplayTag GoldTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Gold")), false);
    const FGameplayTag CrystalTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Crystal")), false);
    const FGameplayTag FossilTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Fossil")), false);

    // Biome tags for weighting
    const FGameplayTag MountainTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")));
    const FGameplayTag SnowPeakTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.SnowPeak")));
    const FGameplayTag AlpineTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Alpine")));
    const FGameplayTag ForestTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Forest")));
    const FGameplayTag WoodlandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Woodland")));
    const FGameplayTag JungleTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Jungle")));
    const FGameplayTag DesertTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Desert")));
    const FGameplayTag SwampTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Swamp")));
    const FGameplayTag MarshTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Marsh")));
    const FGameplayTag HillsTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Hills")));
    const FGameplayTag CanyonTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Canyon")), false);
    const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);

    // ── 1D vertical noise scan config ──
    const int32 ScanDepthMin = 2;
    const int32 ScanDepthMax = 100;
    const int32 ScanStep = 4;

    const int32 NumCategories = FResourceNoiseRegistry::Num();
    const float SeedF = static_cast<float>(WorldSeed & 0x7FFFFFFF);

    int32 TilesWithOre = 0;
    int32 SurfaceNodeTiles = 0;

    for (int32 TileIdx = 0; TileIdx < WorldTiles.Num(); ++TileIdx)
    {
        FWorldTileData& Tile = WorldTiles[TileIdx];
        Tile.ResourceTags.Reset();

        const bool bHighland =
            Tile.BiomeType.MatchesTag(MountainTag) ||
            Tile.BiomeType.MatchesTag(SnowPeakTag) ||
            Tile.BiomeType.MatchesTag(AlpineTag);

        const bool bHills = Tile.BiomeType.MatchesTag(HillsTag);

        const bool bForested =
            Tile.BiomeType.MatchesTag(ForestTag) ||
            Tile.BiomeType.MatchesTag(WoodlandTag) ||
            Tile.BiomeType.MatchesTag(JungleTag);

        // ── Legacy resource tags ──
        if (bHighland)
        {
            Tile.ResourceTags.Add(StoneTag);
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.5f)
                Tile.ResourceTags.Add(OreTag);
        }
        else if (bHills)
        {
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.4f)
                Tile.ResourceTags.Add(StoneTag);
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.75f)
                Tile.ResourceTags.Add(OreTag);
        }
        else if (bForested)
        {
            Tile.ResourceTags.Add(WoodTag);
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.8f)
                Tile.ResourceTags.Add(StoneTag);
        }
        else if (Tile.BiomeType.MatchesTag(SwampTag) || Tile.BiomeType.MatchesTag(MarshTag))
        {
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.5f)
                Tile.ResourceTags.Add(WoodTag);
        }
        else if (Tile.BiomeType.MatchesTag(DesertTag))
        {
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.9f)
                Tile.ResourceTags.Add(StoneTag);
        }
        else if (Tile.Elevation > 0.4f && RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.7f)
        {
            Tile.ResourceTags.Add(StoneTag);
        }

        // ── Reset vein config ──
        FResourceVeinConfig& Vein = Tile.VeinConfig;
        Vein.bHasVeins = false;
        Vein.bHasSurfaceExposure = false;
        Vein.PresentCategories.Reset();
        Vein.SpawnSeeds.Reset();
        Vein.VeinRichness.Reset();
        Vein.DepthRanges.Reset();

        if (Tile.Elevation < 0.48f)
        {
            continue;
        }

        float Weights[3] = { 0.5f, 0.5f, 0.5f };

        if (bHighland)
        {
            Weights[0] = 1.0f;  Weights[1] = 0.6f;  Weights[2] = 0.3f;
        }
        else if (bHills)
        {
            Weights[0] = 0.8f;  Weights[1] = 0.4f;  Weights[2] = 0.5f;
        }
        else if (bForested)
        {
            Weights[0] = 0.3f;  Weights[1] = 0.7f;  Weights[2] = 0.4f;
        }
        else if (Tile.BiomeType.MatchesTag(DesertTag))
        {
            Weights[0] = 0.4f;  Weights[1] = 0.3f;  Weights[2] = 0.9f;
        }

        if (CanyonTag.IsValid() && Tile.BiomeType.MatchesTag(CanyonTag))
        {
            Weights[0] = 0.7f;  Weights[2] = 0.8f;
        }
        if (VolcanicTag.IsValid() && Tile.BiomeType.MatchesTag(VolcanicTag))
        {
            Weights[0] = 1.2f;  Weights[1] = 0.9f;
        }

        const int32 TileX = TileIdx % GridWidth;
        const int32 TileY = TileIdx / GridWidth;

        FResourceSurveyData& Survey = Tile.ResourceSurvey;
        Survey = FResourceSurveyData();

        for (int32 Cat = 0; Cat < NumCategories; ++Cat)
        {
            const FResourceNoiseProfile& Prof = FResourceNoiseRegistry::GetProfile(
                static_cast<EResourceCategory>(Cat));

            float PeakDensity = 0.0f;
            float PeakDepth = 0.0f;

            for (int32 Depth = ScanDepthMin; Depth <= ScanDepthMax; Depth += ScanStep)
            {
                const float Z = -static_cast<float>(Depth);

                const float N1 = FMath::PerlinNoise3D(FVector(
                    (TileX + SeedF * 0.71f + Prof.SeedOffset) * Prof.Frequency,
                    (TileY + SeedF * 0.83f + Prof.SeedOffset) * Prof.Frequency,
                    (Z + SeedF * 0.97f + Prof.SeedOffset) * Prof.Frequency * Prof.ZScale
                ));

                const float N2 = FMath::PerlinNoise3D(FVector(
                    (TileX + 3000.0f + SeedF * 0.41f + Prof.SeedOffset) * Prof.Frequency,
                    (TileY + 3000.0f + SeedF * 0.59f + Prof.SeedOffset) * Prof.Frequency,
                    (Z + 3000.0f + SeedF * 0.67f + Prof.SeedOffset) * Prof.Frequency * Prof.ZScale
                ));

                const float Mask = 1.0f - (N1 * N1 + N2 * N2);
                const float Weighted = FMath::Clamp(Mask * Weights[Cat], 0.0f, 1.0f);

                if (Weighted > PeakDensity)
                {
                    PeakDensity = Weighted;
                    PeakDepth = static_cast<float>(Depth);
                }
            }

            Survey.DensityByCategory[Cat] = PeakDensity;
            Survey.PeakDepthByCategory[Cat] = PeakDepth;
        }

        // ── Build veins ──
        bool bHasAnyVein = false;

        for (int32 Cat = 0; Cat < NumCategories; ++Cat)
        {
            const FResourceNoiseProfile& Prof = FResourceNoiseRegistry::GetProfile(
                static_cast<EResourceCategory>(Cat));

            if (Survey.DensityByCategory[Cat] < Prof.PresenceThreshold)
            {
                continue;
            }

            Vein.PresentCategories.Add(static_cast<EResourceCategory>(Cat));

            const int64 SubSeed = WorldSeed
                ^ (static_cast<int64>(TileIdx) * 2654435761LL)
                ^ (static_cast<int64>(Cat) * 40503LL);

            Vein.SpawnSeeds.Add(SubSeed);
            Vein.VeinRichness.Add(Survey.DensityByCategory[Cat]);

            const float Spread = (Cat == static_cast<int32>(EResourceCategory::Fossil)) ? 8.0f : 20.0f;
            const float Peak = Survey.PeakDepthByCategory[Cat];

            Vein.DepthRanges.Add(FVector2D(
                FMath::Max(2.0f, Peak - Spread),
                FMath::Min(100.0f, Peak + Spread)));

            bHasAnyVein = true;
        }

        Vein.bHasVeins = bHasAnyVein;

        if (!bHasAnyVein)
        {
            continue;
        }

        ++TilesWithOre;

        // ── NEW: Surface exposure logic ──
        const bool bNearSurface =
            Survey.PeakDepthByCategory[0] <= 14.0f ||
            Survey.PeakDepthByCategory[1] <= 14.0f ||
            Survey.PeakDepthByCategory[2] <= 10.0f;

        const bool bGoodBiome =
            bHighland || bHills ||
            (CanyonTag.IsValid() && Tile.BiomeType.MatchesTag(CanyonTag)) ||
            (VolcanicTag.IsValid() && Tile.BiomeType.MatchesTag(VolcanicTag));

        if (bNearSurface && bGoodBiome)
        {
            const float Chance = bHighland ? 0.10f : 0.045f;

            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) < Chance)
            {
                Vein.bHasSurfaceExposure = true;
                ++SurfaceNodeTiles;
            }
        }

        // ── Resource tags ──
        const float Thresh = FResourceNoiseRegistry::GetProfile(EResourceCategory::Metal).PresenceThreshold;

        if (Survey.DensityByCategory[0] >= Thresh)
        {
            if (IronTag.IsValid()) Tile.ResourceTags.AddUnique(IronTag);
            Tile.ResourceTags.AddUnique(OreTag);
        }
        if (Survey.DensityByCategory[1] >= Thresh)
        {
            if (CrystalTag.IsValid()) Tile.ResourceTags.AddUnique(CrystalTag);
        }
        if (Survey.DensityByCategory[2] >= Thresh)
        {
            if (FossilTag.IsValid()) Tile.ResourceTags.AddUnique(FossilTag);
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Step08 done. VeinTiles=%d SurfaceNodes=%d"),
        TilesWithOre,
        SurfaceNodeTiles);
}

void UWorldGenPipeline::Step12_GenerateCosmicRoster()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step12: Generating cosmic roster..."));

    if (!WorldRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step12: WorldRef null."));
        return;
    }

    UCosmicSimulationSubsystem* CosmicSim = WorldRef->GetSubsystem<UCosmicSimulationSubsystem>();
    if (!CosmicSim)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step12: CosmicSimulationSubsystem not found."));
        return;
    }

    // Seed a base pantheon of cosmic entities across key domains.
    // Emergence thresholds are staggered so entities awaken at different
    // stages of colony development.  Power comes from anchors (shrines),
    // colonist PurposeNeed, and narrative events.
    struct FEntitySeed
    {
        const TCHAR* TagName;
        const TCHAR* DomainTagName;
        float EmergenceThreshold;
    };

    static const FEntitySeed Seeds[] =
    {
        { TEXT("WorldForge.Cosmic.Entity.War"),       TEXT("WorldForge.Domain.War"),       8.0f  },
        { TEXT("WorldForge.Cosmic.Entity.Nature"),     TEXT("WorldForge.Domain.Nature"),    6.0f  },
        { TEXT("WorldForge.Cosmic.Entity.Commerce"),   TEXT("WorldForge.Domain.Commerce"),  10.0f },
        { TEXT("WorldForge.Cosmic.Entity.Knowledge"),  TEXT("WorldForge.Domain.Knowledge"), 12.0f },
        { TEXT("WorldForge.Cosmic.Entity.Death"),      TEXT("WorldForge.Domain.Death"),     15.0f },
        { TEXT("WorldForge.Cosmic.Entity.Creation"),   TEXT("WorldForge.Domain.Creation"),  7.0f  },
    };

    int32 RegisteredCount = 0;
    for (const FEntitySeed& Seed : Seeds)
    {
        const FGameplayTag EntityTag = FGameplayTag::RequestGameplayTag(FName(Seed.TagName), false);
        const FGameplayTag DomainTag = FGameplayTag::RequestGameplayTag(FName(Seed.DomainTagName), false);

        if (!EntityTag.IsValid())
        {
            continue;
        }

        TArray<FGameplayTag> Domains;
        if (DomainTag.IsValid())
        {
            Domains.Add(DomainTag);
        }

        CosmicSim->RegisterEntity(EntityTag, Domains, Seed.EmergenceThreshold);
        ++RegisteredCount;
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step12: %d cosmic entities registered."), RegisteredCount);
}
void UWorldGenPipeline::Step14_AssignWorldTraits()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step14: Assigning world traits..."));

    WorldTraits.Reset();

    // ── Analyze generated world data and assign descriptive trait tags ──
    // These traits can be queried by gameplay systems to adapt behavior.

    // Count biome distribution
    int32 WaterTiles = 0;
    int32 LandTiles = 0;
    int32 MountainTiles = 0;
    int32 VolcanicTiles = 0;

    const FGameplayTag OceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Ocean")), false);
    const FGameplayTag SeaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Sea")), false);
    const FGameplayTag DeepOceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.DeepOcean")), false);
    const FGameplayTag MountainTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")), false);
    const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);

    for (const FWorldTileData& Tile : WorldTiles)
    {
        if (Tile.BiomeType.MatchesTag(OceanTag) ||
            Tile.BiomeType.MatchesTag(SeaTag) ||
            Tile.BiomeType.MatchesTag(DeepOceanTag))
        {
            ++WaterTiles;
        }
        else
        {
            ++LandTiles;
        }

        if (Tile.BiomeType.MatchesTag(MountainTag))
        {
            ++MountainTiles;
        }
        if (Tile.BiomeType.MatchesTag(VolcanicTag))
        {
            ++VolcanicTiles;
        }
    }

    const float WaterRatio = (WorldTiles.Num() > 0)
        ? static_cast<float>(WaterTiles) / static_cast<float>(WorldTiles.Num())
        : 0.0f;

    // Maritime world: >70% water
    if (WaterRatio > 0.70f)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.WorldTrait.Maritime")), false);
        if (Tag.IsValid()) WorldTraits.Add(Tag);
    }

    // Continental world: <50% water
    if (WaterRatio < 0.50f)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.WorldTrait.Continental")), false);
        if (Tag.IsValid()) WorldTraits.Add(Tag);
    }

    // Mountainous: >3% mountain tiles
    if (LandTiles > 0 && static_cast<float>(MountainTiles) / static_cast<float>(LandTiles) > 0.03f)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.WorldTrait.Mountainous")), false);
        if (Tag.IsValid()) WorldTraits.Add(Tag);
    }

    // Volcanic: any volcanic tiles present
    if (VolcanicTiles > 0)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.WorldTrait.Volcanic")), false);
        if (Tag.IsValid()) WorldTraits.Add(Tag);
    }

    // Diverse: 3+ civilizations
    if (Civilizations.Num() >= 3)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.WorldTrait.Diverse")), false);
        if (Tag.IsValid()) WorldTraits.Add(Tag);
    }

    // Warlike: check if history has war events
    int32 WarEvents = 0;
    for (const FHistoryEventData& Evt : HistoryLog)
    {
        if (Evt.EventType.IsValid() &&
            Evt.EventType.ToString().Contains(TEXT("War")))
        {
            ++WarEvents;
        }
    }

    if (WarEvents >= 3)
    {
        const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.WorldTrait.Warlike")), false);
        if (Tag.IsValid()) WorldTraits.Add(Tag);
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step14: %d world traits assigned."), WorldTraits.Num());
}

void UWorldGenPipeline::Step15_RecordFinalState()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step15: Recording final state..."));

    // Record a generation summary for debug and save purposes.
    // When FWorldLoreState is implemented, this step will populate it
    // with the complete world generation output for persistence.

    int32 LandTiles = 0;
    int32 WaterTiles = 0;

    const FGameplayTag DeepOceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.DeepOcean")), false);
    const FGameplayTag OceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Ocean")), false);
    const FGameplayTag SeaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Sea")), false);

    for (const FWorldTileData& Tile : WorldTiles)
    {
        const bool bWater =
            Tile.BiomeType.MatchesTag(DeepOceanTag) ||
            Tile.BiomeType.MatchesTag(OceanTag) ||
            Tile.BiomeType.MatchesTag(SeaTag);

        if (bWater)
        {
            ++WaterTiles;
        }
        else
        {
            ++LandTiles;
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Step15: Final state — Tiles=%d Land=%d Water=%d Civs=%d Events=%d Currencies=%d Traits=%d MountainRanges=%d Volcanoes=%d"),
        WorldTiles.Num(),
        LandTiles,
        WaterTiles,
        Civilizations.Num(),
        HistoryLog.Num(),
        Currencies.Num(),
        WorldTraits.Num(),
        MountainRanges.Num(),
        Volcanoes.Num());
}

void UWorldGenPipeline::Step16_FinalizeWorldMap()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step16: Finalizing world map..."));

    if (!WorldRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step16: WorldRef null."));
        return;
    }

    // Initialize fog of war: all tiles start Unexplored.
    // Tiles owned by civilizations get Surveyed status.
    for (FWorldTileData& Tile : WorldTiles)
    {
        if (Tile.ControllingCivID >= 0)
        {
            Tile.KnowledgeState.KnowledgeLevel = EKnowledgeLevel::Surveyed;
            Tile.KnowledgeState.DataAccuracy = 1.0f;
        }
    }

    // Notify WorldMapSubsystem to finalize (if available at this point).
    // Note: the bootstrap also calls InitializeFromPipeline after GenerateWorld
    // completes.  This early call ensures fog-of-war state is baked into the
    // tiles before the bootstrap picks them up.

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step16: World map finalized. Fog-of-war initialized for %d civ-controlled tiles."),
        [&]() { int32 N = 0; for (const FWorldTileData& T : WorldTiles) { if (T.ControllingCivID >= 0) ++N; } return N; }());
}

void UWorldGenPipeline::Step09_PlaceCivilizations()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step09: Placing civilizations..."));

    if (!RNGRef) return;

    UCivilizationGenerator* CivGen = NewObject<UCivilizationGenerator>(this);

    Civilizations = CivGen->GenerateCivilizations(
        CurrentConfig.StartingCivCount,
        WorldTiles,
        GridWidth,
        RNGRef
    );

    // Territory claim (CRITICAL FIX)
    UCivilizationGenerator::ClaimTerritories(
        Civilizations,
        WorldTiles,
        GridWidth,
        GridHeight
    );

    // DEBUG: verify territory assignment actually worked
    int32 TotalControlledTiles = 0;
    for (const FCivilizationData& Civ : Civilizations)
    {
        TotalControlledTiles += Civ.ControlledTileIDs.Num();
    }

    UE_LOG(LogTemp, Log, TEXT("[CivGen] Total controlled tiles: %d"), TotalControlledTiles);

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step09: %d civs placed"), Civilizations.Num());
}

void UWorldGenPipeline::Step10_GenerateCulturalDNA()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step10: Cultural DNA..."));
    if (!RNGRef) return;
    UCivilizationGenerator* CivGen = NewObject<UCivilizationGenerator>(this);
    CivGen->GenerateCulturalDNA(Civilizations, WorldTiles, RNGRef);
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step10: Cultural DNA done for %d civs"), Civilizations.Num());
}

void UWorldGenPipeline::Step11_GenerateEconomyLayer()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step11: Economy layer..."));
    if (!WorldRef) { UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step11: WorldRef null")); return; }
    if (UWorldEconomySimulation* Econ = WorldRef->GetSubsystem<UWorldEconomySimulation>())
        Econ->GenerateWorldEconomy(Civilizations, WorldTiles, Currencies, RNGRef);
    else
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step11: UWorldEconomySimulation not found"));
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step11: %d currencies"), Currencies.Num());
}

void UWorldGenPipeline::Step13_RunPreHistory()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step13: Pre-history..."));

    if (!RNGRef)
    {
        return;
    }

    FHistoricalSimConfig SimCfg;
    SimCfg.TotalYears = CurrentConfig.PreHistoryYears;
    SimCfg.WarFrequency = CurrentConfig.WarFrequency;
    SimCfg.DisasterFrequency = CurrentConfig.DisasterFrequency;
    SimCfg.EconomicComplexity = CurrentConfig.EconomicComplexity;

    UHistoricalSimulation* HistSim = NewObject<UHistoricalSimulation>(this);
    HistSim->RunSimulation(SimCfg, Civilizations, WorldTiles, Currencies, HistoryLog, RNGRef);

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step13: %d events"), HistoryLog.Num());

    {
        FWorldHistoryContext Context;
        Context.RNG = RNGRef;
        Context.CurrentYear = CurrentConfig.PreHistoryYears;

        FWorldHistoryGenerator Generator;
        Generator.Run(Context);

        const int32 NameCount = FMath::Min(Civilizations.Num(), Context.GeneratedNames.Num());
        TSet<FString> UsedNames;

        for (int32 Index = 0; Index < NameCount; ++Index)
        {
            FString RawName = CleanNameArtifacts(Context.GeneratedNames[Index]);

            if (RawName.Len() > 0)
            {
                RawName[0] = FChar::ToUpper(RawName[0]);
            }

            FString FinalName = RawName;
            int32 Suffix = 1;

            while (UsedNames.Contains(FinalName))
            {
                FinalName = FString::Printf(TEXT("%s %d"), *RawName, Suffix);
                ++Suffix;
            }

            UsedNames.Add(FinalName);
            Civilizations[Index].Name = FText::FromString(FinalName).ToString();

            UE_LOG(LogTemp, Log, TEXT("[WorldHistory] Assigned Civilization %d Name: %s"),
                Civilizations[Index].CivID,
                *Civilizations[Index].Name);
        }

        if (Context.GeneratedFigures.Num() == Civilizations.Num())
        {
            for (int32 Index = 0; Index < Civilizations.Num(); ++Index)
            {
                Civilizations[Index].LegendaryFigure = Context.GeneratedFigures[Index];

                UE_LOG(LogTemp, Log, TEXT("[WorldHistory] Civilization %d Figure Stored As: %s"),
                    Civilizations[Index].CivID,
                    *Civilizations[Index].LegendaryFigure);
            }
        }

        for (int32 Index = 0; Index < NameCount; ++Index)
        {
            if (Context.RaceOrigins.IsValidIndex(Index))
            {
                Civilizations[Index].RaceOrigin = Context.RaceOrigins[Index];

                UE_LOG(LogTemp, Log, TEXT("[WorldHistory] Civilization %d Origin Stored As: %s"),
                    Civilizations[Index].CivID,
                    *Civilizations[Index].RaceOrigin);
            }
        }
    }

    // Step13.5: Lore generation.
    // Uses finalized civ names/origins from the block above.
    {
        FWorldLoreState LoreState;
        LoreState.LoreSeed = static_cast<int32>(WorldSeed);

        TArray<FHistoricalFigure> EmptyFigures;

        FLoreGenerationContext LoreContext
        {
            LoreState,
            *RNGRef,
            Civilizations,
            EmptyFigures,
            nullptr,
            WorldTiles,
            GridWidth,
            GridHeight,
            static_cast<int32>(WorldSeed)
        };

        GenerateWorldLore(LoreContext);

        
        UE_LOG(LogTemp, Log,
            TEXT("[WorldGen] Step13.5: Lore generated | %s"),
            *LoreState.GetSummaryString());

        // Dump full generated lore to log and to Saved/WorldLoreDump.txt
        LoreState.LogFullLore();

        const FString LoreDumpPath = FPaths::ProjectSavedDir() / TEXT("WorldLoreDump.txt");

        FString LoreText;
        LoreText += TEXT("========== WORLD LORE DUMP ==========\n");
        LoreText += LoreState.GetSummaryString() + TEXT("\n\n");

        LoreText += TEXT("---- ARTIFACTS ----\n");
        for (const FLoreArtifact& Artifact : LoreState.Artifacts)
        {
            LoreText += FString::Printf(TEXT("%s | %s\n"),
                *Artifact.DisplayName.ToString(),
                *Artifact.Description.ToString());
        }

        LoreText += TEXT("\n---- FALLEN CITIES ----\n");
        for (const FLoreFallenCity& City : LoreState.FallenCities)
        {
            LoreText += FString::Printf(TEXT("%s | %s\n"),
                *City.DisplayName.ToString(),
                *City.LoreSnippet.ToString());
        }

        LoreText += TEXT("\n---- LEGENDS ----\n");
        for (const FLoreLegend& Legend : LoreState.Legends)
        {
            LoreText += FString::Printf(TEXT("%s | %s\n"),
                *Legend.Title.ToString(),
                *Legend.NarrativeText.ToString());
        }

        LoreText += TEXT("\n---- COLONIST BACKSTORIES ----\n");
        for (const FColonistBackstory& Colonist : LoreState.ColonistBackstories)
        {
            LoreText += FString::Printf(TEXT("%s | %s\n"),
                *Colonist.DisplayName.ToString(),
                *Colonist.BackstoryText.ToString());
        }

        LoreText += TEXT("\n========== END WORLD LORE DUMP ==========\n");

        FFileHelper::SaveStringToFile(LoreText, *LoreDumpPath);

        UE_LOG(LogTemp, Log, TEXT("[WorldLore] Full lore written to: %s"), *LoreDumpPath);

    }
}
