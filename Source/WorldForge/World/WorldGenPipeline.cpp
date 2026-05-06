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

    // CRITICAL HANDOFF FIX:
    // Resource veins must be seeded before voxel terrain initialization.
    // Step05 hands WorldTiles to VoxelWorldManager, which may immediately cache/snapshot
    // tile metadata even if actual terrain generation is deferred.
    // If resources are seeded after Step05, VoxelWorldManager sees 0 vein tiles.
    Step08_SeedResources();
    PushPartialMapUpdate();                      // resource overlay/data now exists before terrain snapshot

    Step05_InitVoxelChunks();
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

    bComplete = true;
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Complete Tiles=%d Civs=%d Events=%d Currencies=%d Hotspots=%d Volcanoes=%d"),
        WorldTiles.Num(), Civilizations.Num(),
        HistoryLog.Num(), Currencies.Num(), VolcanicHotspots.Num(), Volcanoes.Num());
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
    const float ChannelAThreshold = 0.38f;
    const float ChannelAStrength = 0.55f;

    // ── Channel carver B — secondary ocean gap (slightly higher freq) ──
    const float ChannelBScale = 0.0038f;
    const float ChannelBThreshold = 0.42f;
    const float ChannelBStrength = 0.45f;

    // ── Land shaping ──
    const float LandPower = 0.72f;      // higher = more ocean (was 0.55)
    const float SeaThreshold = 0.48f;
    const float LandBias = 0.06f;       // much less bias (was 0.12)

    // ── Edge fade ──
    const FVector2D MapCenter((GridWidth - 1) * 0.5f, (GridHeight - 1) * 0.5f);
    const float MaxCenterDist = FVector2D::Distance(FVector2D::ZeroVector, MapCenter);

    VolcanicHotspots.Reset();

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];
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
                (WarpedX + OffX) * ContinentScale,
                (WarpedY + OffY) * ContinentScale,
                ContinentOctaves, ContinentPersist, ContinentLacun);
            float Continent01 = (ContinentRaw + 1.0f) * 0.5f;

            // ── 3. Power curve ──
            Continent01 = FMath::Pow(FMath::Clamp(Continent01, 0.0f, 1.0f), LandPower);

            // ── 4. Channel carver A — broad primary ocean gap ──
            {
                const float ChNoise = PerlinFBM2D(
                    (WarpedX + ChannelAOffX) * ChannelAScale,
                    (WarpedY + ChannelAOffY) * ChannelAScale,
                    3, 0.45f, 2.2f);
                const float Ch01 = (ChNoise + 1.0f) * 0.5f;

                if (Ch01 > ChannelAThreshold)
                {
                    const float Depth = (Ch01 - ChannelAThreshold) / (1.0f - ChannelAThreshold);
                    Continent01 -= Depth * ChannelAStrength;
                }
            }

            // ── 5. Channel carver B — secondary ocean gap ──
            {
                const float ChNoise = PerlinFBM2D(
                    (WarpedX + ChannelBOffX) * ChannelBScale,
                    (WarpedY + ChannelBOffY) * ChannelBScale,
                    3, 0.50f, 2.0f);
                const float Ch01 = (ChNoise + 1.0f) * 0.5f;

                if (Ch01 > ChannelBThreshold)
                {
                    const float Depth = (Ch01 - ChannelBThreshold) / (1.0f - ChannelBThreshold);
                    Continent01 -= Depth * ChannelBStrength;
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
            const float EdgeFade = FMath::SmoothStep(0.65f, 0.92f, EdgeNorm);

            // ── 8. Combine ──
            float Elevation = Continent01 * 0.85f + Detail01 * DetailWeight + LandBias;
            Elevation *= (1.0f - EdgeFade);

            // ── 9. Ridge noise for volcanic hotspot candidates ──
            const float RidgeRaw = FMath::PerlinNoise2D(FVector2D(
                (WarpedX + OffX + 1000.f) * 0.012f,
                (WarpedY + OffY + 1000.f) * 0.012f));
            float RidgeVal = 1.0f - FMath::Abs(RidgeRaw);
            RidgeVal = RidgeVal * RidgeVal;

            if (Elevation > SeaThreshold)
            {
                const float CoastalFade = FMath::Clamp(
                    (Elevation - SeaThreshold) / 0.15f, 0.0f, 1.0f);
                Elevation += RidgeVal * 0.10f * CoastalFade;

                if (RidgeVal > 0.78f && CoastalFade > 0.3f)
                {
                    VolcanicHotspots.Add(FIntPoint(X, Y));
                }
            }

            Tile.Elevation = FMath::Clamp(Elevation, 0.0f, 1.0f);
            Tile.Temperature = 0.5f;
            Tile.Rainfall = 0.5f;
        }
    }

    // ── Diagnostic: count water tiles ──
    int32 WaterCount = 0;
    for (const FWorldTileData& Tile : WorldTiles)
    {
        if (Tile.Elevation < SeaThreshold) ++WaterCount;
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step02 done. Water=%d (%d%%) Land=%d (%d%%) VolcanicHotspots=%d"),
        WaterCount, WorldTiles.Num() > 0 ? WaterCount * 100 / WorldTiles.Num() : 0,
        WorldTiles.Num() - WaterCount, WorldTiles.Num() > 0 ? (WorldTiles.Num() - WaterCount) * 100 / WorldTiles.Num() : 0,
        VolcanicHotspots.Num());
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

            Tile.Elevation = FMath::Clamp(Elevation, 0.0f, 1.0f);
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
    // Trace downhill paths from high-elevation land tiles.
    // Rivers depress elevation slightly and are marked via WaterDepth > 0.
    {
        const int32 MaxRiverSources = FMath::Clamp(GridWidth / 8, 8, 64);
        const float RiverSourceMinElev = 0.70f;
        const float RiverDepression = 0.03f;
        const float RiverMinSpacing = GridWidth * 0.06f;

        // Collect candidate sources (high elevation land)
        TArray<FIntPoint> RiverSources;
        for (int32 Y = 2; Y < GridHeight - 2; ++Y)
        {
            for (int32 X = 2; X < GridWidth - 2; ++X)
            {
                const FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];
                if (Tile.Elevation >= RiverSourceMinElev)
                {
                    RiverSources.Add(FIntPoint(X, Y));
                }
            }
        }

        // Shuffle and select spaced sources
        for (int32 i = RiverSources.Num() - 1; i > 0; --i)
        {
            const int32 j = RNGRef->NextInt(UWorldForgeRNG::BRANCH_TERRAIN, 0, i);
            RiverSources.Swap(i, j);
        }

        TArray<FIntPoint> SelectedSources;
        for (const FIntPoint& Src : RiverSources)
        {
            if (SelectedSources.Num() >= MaxRiverSources) break;
            bool bTooClose = false;
            for (const FIntPoint& Existing : SelectedSources)
            {
                if (FVector2D::Distance(FVector2D(Src), FVector2D(Existing)) < RiverMinSpacing)
                {
                    bTooClose = true; break;
                }
            }
            if (!bTooClose) SelectedSources.Add(Src);
        }

        // Trace each river downhill
        int32 TotalRiverTiles = 0;
        const int32 MaxRiverLength = GridWidth;
        static const FIntPoint Neighbors[] = {
            {-1, 0}, {1, 0}, {0, -1}, {0, 1},
            {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
        };

        for (const FIntPoint& Source : SelectedSources)
        {
            FIntPoint Current = Source;
            TSet<int32> Visited;

            for (int32 Step = 0; Step < MaxRiverLength; ++Step)
            {
                const int32 CurIdx = TileIndex(Current.X, Current.Y);
                if (Visited.Contains(CurIdx)) break;
                Visited.Add(CurIdx);

                FWorldTileData& CurTile = WorldTiles[CurIdx];

                // Reached ocean — stop
                if (CurTile.Elevation < 0.48f) break;

                // Mark as river (small WaterDepth signals river presence)
                CurTile.WaterDepth = FMath::Max(CurTile.WaterDepth, 0.05f);
                CurTile.Elevation = FMath::Max(0.48f, CurTile.Elevation - RiverDepression);
                ++TotalRiverTiles;

                // Find lowest neighbor
                float LowestElev = CurTile.Elevation;
                FIntPoint LowestNeighbor = Current;
                for (const FIntPoint& N : Neighbors)
                {
                    const int32 NX = Current.X + N.X;
                    const int32 NY = Current.Y + N.Y;
                    if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight) continue;
                    const float NElev = WorldTiles[TileIndex(NX, NY)].Elevation;
                    if (NElev < LowestElev)
                    {
                        LowestElev = NElev;
                        LowestNeighbor = FIntPoint(NX, NY);
                    }
                }

                // If no downhill neighbor, break (local minimum = lake candidate)
                if (LowestNeighbor == Current)
                {
                    // Create a small lake at the local minimum
                    for (const FIntPoint& N : Neighbors)
                    {
                        const int32 NX = Current.X + N.X;
                        const int32 NY = Current.Y + N.Y;
                        if (NX >= 0 && NX < GridWidth && NY >= 0 && NY < GridHeight)
                        {
                            FWorldTileData& LakeTile = WorldTiles[TileIndex(NX, NY)];
                            if (LakeTile.Elevation >= 0.48f && LakeTile.Elevation < 0.58f)
                            {
                                LakeTile.WaterDepth = FMath::Max(LakeTile.WaterDepth, 0.15f);
                            }
                        }
                    }
                    CurTile.WaterDepth = FMath::Max(CurTile.WaterDepth, 0.20f);
                    break;
                }

                Current = LowestNeighbor;
            }
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
                if (Tile.Elevation < 0.48f) continue;

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
        if (Tile.Elevation < 0.30f)
            Tile.BiomeType = DeepOceanTag;
        else if (Tile.Elevation < 0.40f)
            Tile.BiomeType = OceanTag;
        else if (Tile.Elevation < 0.48f)
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
        else if (Tile.Elevation < 0.53f)
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
    int32 WaterCount = 0, LandCount = 0;
    for (const FWorldTileData& Tile : WorldTiles)
    {
        const bool bWater = Tile.BiomeType.MatchesTag(DeepOceanTag) || Tile.BiomeType.MatchesTag(OceanTag) || Tile.BiomeType.MatchesTag(SeaTag);
        if (bWater) ++WaterCount; else ++LandCount;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Step04 done. Water=%d (%d%%) Land=%d (%d%%) Rivers=%d Lakes=%d Canyons=%d Lava=%d"),
        WaterCount, WorldTiles.Num() > 0 ? WaterCount * 100 / WorldTiles.Num() : 0,
        LandCount, WorldTiles.Num() > 0 ? LandCount * 100 / WorldTiles.Num() : 0,
        RiverCount, LakeCount, CanyonCount, LavaCount);
}

// ════════════════════════════════════════════════════════════════════
// STEPS 05–16 (unchanged)
// ════════════════════════════════════════════════════════════════════

void UWorldGenPipeline::Step05_InitVoxelChunks()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step05: Init voxel chunks..."));

    if (!WorldRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step05 failed: WorldRef is null."));
        return;
    }

    UVoxelWorldManager* VoxelMgr = WorldRef->GetSubsystem<UVoxelWorldManager>();
    if (!VoxelMgr)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step05 failed: UVoxelWorldManager not found."));
        return;
    }

    VoxelMgr->SetPipelineMountainRanges(MountainRanges);
    VoxelMgr->SetPipelineVolcanoes(Volcanoes);

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
        return;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Step05 done. TerrainMode=%s BlockHeightStep=%d"),
        CurrentConfig.TerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
        CurrentConfig.BlockHeightStep);
}

void UWorldGenPipeline::Step06_PCGDecorationPass()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step06: Decoration hook..."));
    HandlePCGDecorationPass(WorldTiles, GridWidth, GridHeight);
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step06 done"));
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