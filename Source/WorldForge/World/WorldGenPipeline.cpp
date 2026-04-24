// WorldGenPipeline.cpp
#include "WorldGenPipeline.h"
#include "../Core/WorldForgeRNG.h"
#include "GameplayTagContainer.h"
#include "../Civilization/CivilizationGenerator.h"
#include "../Civilization/HistoricalSimulation.h"
#include "../Civilization/WorldHistory/WorldHistoryGenerator.h"
#include "../Civilization/WorldHistory/WorldHistoryContext.h"
#include "../Economy/WorldEconomySimulation.h"
#include "VoxelWorldManager.h"

namespace
{
    static FString CleanNameArtifacts(const FString& Input)
    {
        FString Result = Input;

        // Remove triple repeated characters (rrr -> rr)
        for (int32 i = 0; i < Result.Len() - 2; ++i)
        {
            if (Result[i] == Result[i + 1] && Result[i] == Result[i + 2])
            {
                Result.RemoveAt(i + 2);
                --i;
            }
        }

        // Remove repeated 2-letter syllables (e.g. riri -> ri)
        for (int32 i = 0; i < Result.Len() - 3; ++i)
        {
            if (Result[i] == Result[i + 2] &&
                Result[i + 1] == Result[i + 3])
            {
                Result.RemoveAt(i + 2, 2);
                --i;
            }
        }

        // Keep previous replacements (safe)
        Result.ReplaceInline(TEXT("ririr"), TEXT("rir"));
        Result.ReplaceInline(TEXT("lalal"), TEXT("lal"));

        return Result;
    }

    static FVector2D Rotate2D(const FVector2D& Vec, float Degrees)
    {
        const float Rad = FMath::DegreesToRadians(Degrees);
        const float C = FMath::Cos(Rad);
        const float S = FMath::Sin(Rad);

        return FVector2D(
            Vec.X * C - Vec.Y * S,
            Vec.X * S + Vec.Y * C
        );
    }

    static float DistanceToSegment2D(const FVector2D& P, const FVector2D& A, const FVector2D& B)
    {
        const FVector2D AB = B - A;
        const float Denom = FVector2D::DotProduct(AB, AB);

        if (Denom <= KINDA_SMALL_NUMBER)
        {
            return FVector2D::Distance(P, A);
        }

        const float T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / Denom, 0.f, 1.f);
        const FVector2D Closest = A + AB * T;
        return FVector2D::Distance(P, Closest);
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

    // ── Perlin FBM helper (fractal Brownian motion) ──
    // Layers multiple octaves of UE's built-in PerlinNoise2D to produce
    // richer terrain variation than a single sample.
    static float PerlinFBM2D(float X, float Y, int32 Octaves, float Persistence, float Lacunarity)
    {
        float Sum = 0.0f;
        float Amplitude = 1.0f;
        float Frequency = 1.0f;
        float MaxVal = 0.0f;

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

    // Reset generated results for repeat-safe execution
    Civilizations.Reset();
    HistoryLog.Reset();
    Currencies.Reset();
    WorldTraits.Reset();
    MountainRanges.Reset();
    MountainProvinces.Reset();
    VolcanicHotspots.Reset();

    // Allocate tiles
    WorldTiles.SetNum(GridWidth * GridHeight);
    for (int32 i = 0; i < WorldTiles.Num(); i++)
    {
        WorldTiles[i].TileID = i;
        WorldTiles[i].KnowledgeState.TileID = i;
        WorldTiles[i].KnowledgeState.KnowledgeLevel = EKnowledgeLevel::Unexplored;
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Start Seed=%lld Grid=%dx%d"),
        WorldSeed, GridWidth, GridHeight);

    Step01_InitializeSeed();
    Step02_GenerateHeightmap();
    Step03_RunTectonics();
    Step04_AssignBiomes();
    Step05_InitVoxelChunks();
    Step06_PCGDecorationPass();
    Step07_ApplyWorldDriver();
    Step08_SeedResources();
    Step09_PlaceCivilizations();
    Step10_GenerateCulturalDNA();
    Step11_GenerateEconomyLayer();
    Step12_GenerateCosmicRoster();
    Step13_RunPreHistory();
    Step14_AssignWorldTraits();
    Step15_RecordFinalState();
    Step16_FinalizeWorldMap();

    bComplete = true;
    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Complete Tiles=%d Civs=%d Events=%d Currencies=%d Hotspots=%d"),
        WorldTiles.Num(), Civilizations.Num(),
        HistoryLog.Num(), Currencies.Num(), VolcanicHotspots.Num());
}

void UWorldGenPipeline::HandlePCGDecorationPass_Implementation(const TArray<FWorldTileData>& InWorldTiles, int32 InGridWidth, int32 InGridHeight)
{
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WorldGen] HandlePCGDecorationPass default implementation. Tiles=%d Grid=%dx%d"),
        InWorldTiles.Num(),
        InGridWidth,
        InGridHeight
    );
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

void UWorldGenPipeline::Step02_GenerateHeightmap()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step02: Heightmap..."));

    // ── Deterministic offsets ──
    const float TerrainOffsetX = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.0f, 10000.0f);
    const float TerrainOffsetY = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_TERRAIN, -10000.0f, 10000.0f);
    const float BiomeOffsetX = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_BIOME, -10000.0f, 10000.0f);
    const float BiomeOffsetY = RNGRef->NextFloatRange(UWorldForgeRNG::BRANCH_BIOME, -10000.0f, 10000.0f);

    // ── Noise scales ──
    const float MacroScaleA = 0.0060f;
    const float MacroScaleB = 0.0120f;
    const float DetailScale = 0.0280f;
    const float TempScale = 0.0100f;
    const float RainScale = 0.0130f;

    const FVector2D MapCenter((GridWidth - 1) * 0.5f, (GridHeight - 1) * 0.5f);
    const float MaxCenterDist = FVector2D::Distance(FVector2D(0.0f, 0.0f), MapCenter);

    // ====================================================================
    // CONTINENT SEEDS
    // ====================================================================
    // Spawn 5–8 smaller, well-separated elliptical blobs plus archipelago
    // islands near their edges.  Each blob gets domain-warped boundaries
    // and noise-driven interior elevation instead of a flat boost.

    struct FContinentSeed
    {
        FVector2D Center;
        float     RadiusA;       // major half-axis (in tiles)
        float     RadiusB;       // minor half-axis
        float     RotRad;        // rotation in radians
        bool      bIsIsland;     // true = archipelago island (smaller, lower max elevation)
        bool      bVolcanicCandidate;  // true = can host a volcano in future steps
    };

    const int32 NumContinents = RNGRef->NextInt(UWorldForgeRNG::BRANCH_TERRAIN, 4, 6);

    TArray<FContinentSeed> Seeds;
    Seeds.Reserve(NumContinents + 20);  // room for islands

    // Keep seeds away from the extreme edges so the edge-ocean pass can work.
    const float Margin = 0.12f;  // 12% inset from each edge

    // -- Minimum spacing enforcement --
    const float MinSeedSpacing = GridWidth * 0.18f;
    const int32 MaxRetries = 10;
    int32 RetryCount = 0;

    for (int32 i = 0; i < NumContinents; ++i)
    {
        FContinentSeed Seed;
        Seed.bIsIsland = false;
        Seed.bVolcanicCandidate = false;

        Seed.Center.X = FMath::Lerp(
            GridWidth * Margin,
            GridWidth * (1.0f - Margin),
            RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));

        Seed.Center.Y = FMath::Lerp(
            GridHeight * Margin,
            GridHeight * (1.0f - Margin),
            RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));

        // Radius range scaled to grid size.  On 256×256 this gives roughly
        // 28–68 tile radii — large enough to read as continents, small enough
        // that 5–8 of them fit with ocean channels between.
        const float BaseRadius = GridWidth * 0.24f;
        Seed.RadiusA = BaseRadius * FMath::Lerp(0.55f, 1.6f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
        Seed.RadiusB = Seed.RadiusA * FMath::Lerp(0.45f, 0.85f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));

        Seed.RotRad = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) * PI;

        // Check spacing against all previously placed seeds
        bool bTooClose = false;
        for (const FContinentSeed& Existing : Seeds)
        {
            if (FVector2D::Distance(Seed.Center, Existing.Center) < MinSeedSpacing)
            {
                bTooClose = true;
                break;
            }
        }

        if (bTooClose && RetryCount < MaxRetries)
        {
            ++RetryCount;
            --i;  // re-roll this index
            continue;
        }
        RetryCount = 0;

        Seeds.Add(Seed);
    }

    // ====================================================================
    // ARCHIPELAGO ISLANDS — spawn small landmasses near continent edges
    // ====================================================================
    // These create island chains, barrier islands, and volcanic arcs that
    // break up the open ocean and give future volcano placement natural sites.

    const int32 NumIslands = RNGRef->NextInt(UWorldForgeRNG::BRANCH_TERRAIN, 6, 12);

    for (int32 i = 0; i < NumIslands; ++i)
    {
        // Pick a random main continent to spawn near
        const int32 ParentIdx = RNGRef->NextInt(UWorldForgeRNG::BRANCH_TERRAIN, 0, Seeds.Num() - 1);
        const FContinentSeed& Parent = Seeds[ParentIdx];

        const float Angle = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) * 2.0f * PI;
        const float SpawnDist = (Parent.RadiusA + Parent.RadiusB) * 0.5f
            + RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) * GridWidth * 0.08f;

        FContinentSeed Isle;
        Isle.bIsIsland = true;
        Isle.bVolcanicCandidate = (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) > 0.55f);

        Isle.Center.X = Parent.Center.X + FMath::Cos(Angle) * SpawnDist;
        Isle.Center.Y = Parent.Center.Y + FMath::Sin(Angle) * SpawnDist;

        // Clamp to grid (keep off absolute edges)
        Isle.Center.X = FMath::Clamp(Isle.Center.X, 5.0f, static_cast<float>(GridWidth - 5));
        Isle.Center.Y = FMath::Clamp(Isle.Center.Y, 5.0f, static_cast<float>(GridHeight - 5));

        // Small radii — these are islands, not continents
        const float IsleBase = GridWidth * FMath::Lerp(0.03f, 0.08f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
        Isle.RadiusA = IsleBase * FMath::Lerp(0.8f, 1.2f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
        Isle.RadiusB = IsleBase * FMath::Lerp(0.8f, 1.2f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
        Isle.RotRad = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN) * PI;

        Seeds.Add(Isle);
    }

    // ── Breakup noise offset (consumed once, deterministic) ──
    const float BreakupScale = 0.0024f;

    // ── Domain-warp settings for fractal coastlines ──
    const float WarpScale = 0.016f;   // controls coastline feature frequency
    const float WarpStrength = 8.0f;    // how jagged coastlines get (in tiles)

    // ── Interior-noise settings (replaces flat ContinentBoost) ──
    const float InteriorDetailScale = 0.030f;  // rolling-hills frequency
    const float InteriorRidgeScale = 0.014f;  // mountain-ridge frequency

    // ====================================================================
    // PER-TILE LOOP
    // ====================================================================
    for (int32 Y = 0; Y < GridHeight; Y++)
    {
        for (int32 X = 0; X < GridWidth; X++)
        {
            FWorldTileData& Tile = WorldTiles[TileIndex(X, Y)];

            const float SX = static_cast<float>(X);
            const float SY = static_cast<float>(Y);

            // ── Base elevation from noise ──
            const float MacroA = FMath::PerlinNoise2D(FVector2D(
                (SX + TerrainOffsetX) * MacroScaleA,
                (SY + TerrainOffsetY) * MacroScaleA));

            const float MacroB = FMath::PerlinNoise2D(FVector2D(
                (SX + TerrainOffsetX + 2000.0f) * MacroScaleB,
                (SY + TerrainOffsetY + 2000.0f) * MacroScaleB));

            const float Detail = FMath::PerlinNoise2D(FVector2D(
                (SX + TerrainOffsetX - 4000.0f) * DetailScale,
                (SY + TerrainOffsetY - 4000.0f) * DetailScale));

            const float Macro01A = (MacroA + 1.0f) * 0.5f;
            const float Macro01B = (MacroB + 1.0f) * 0.5f;
            const float Detail01 = (Detail + 1.0f) * 0.5f;

            // ── Domain warp — compute warped sample position ──
            // Two independent noise fields shift X and Y, giving fractal
            // coastlines with peninsulas, bays, and inlets.
            const float WarpX = FMath::PerlinNoise2D(FVector2D(
                (SX + TerrainOffsetX + 300.0f) * WarpScale,
                (SY + TerrainOffsetY + 300.0f) * WarpScale)) * WarpStrength;

            const float WarpY = FMath::PerlinNoise2D(FVector2D(
                (SX + TerrainOffsetX + 700.0f) * WarpScale,
                (SY + TerrainOffsetY + 700.0f) * WarpScale)) * WarpStrength;

            const float WarpedX = SX + WarpX;
            const float WarpedY = SY + WarpY;

            // ── Continent mask (multi-seed with domain warp) ──
            // For each seed, use the WARPED position to compute elliptical
            // distance.  Take MAX across all seeds.
            float ContinentMask = 0.0f;
            float BestInterior = 0.0f;   // 0..1 how deep inside the winning continent
            bool  bBestIsIsland = false;

            for (const FContinentSeed& Seed : Seeds)
            {
                // Translate to seed-local coords using warped position
                float LX = WarpedX - Seed.Center.X;
                float LY = WarpedY - Seed.Center.Y;

                // Rotate into ellipse-aligned frame
                const float CosR = FMath::Cos(Seed.RotRad);
                const float SinR = FMath::Sin(Seed.RotRad);
                const float RX = LX * CosR + LY * SinR;
                const float RY = -LX * SinR + LY * CosR;

                // Normalised elliptical distance (1.0 = on the ellipse boundary)
                const float EllipDist = FMath::Sqrt(
                    (RX * RX) / (Seed.RadiusA * Seed.RadiusA) +
                    (RY * RY) / (Seed.RadiusB * Seed.RadiusB));

                // Smooth falloff: 1.0 at center, fading to 0 around dist ~1.0
                const float Influence = FMath::Clamp(1.0f - EllipDist, 0.0f, 1.0f);
                const float SeedMask = Influence * Influence * (3.0f - 2.0f * Influence);  // smoothstep

                if (SeedMask > ContinentMask)
                {
                    ContinentMask = SeedMask;
                    BestInterior = Influence;
                    bBestIsIsland = Seed.bIsIsland;
                }
            }

            // ── Edge-ocean ring ──
            const float DistToCenter = FVector2D::Distance(FVector2D(SX, SY), MapCenter);
            const float EdgeNorm = FMath::Clamp(DistToCenter / MaxCenterDist, 0.0f, 1.0f);
            const float EdgeOcean = FMath::SmoothStep(0.70f, 1.0f, EdgeNorm);

            // ── Continent-breakup noise ──
            const float BreakupRaw = FMath::PerlinNoise2D(FVector2D(
                (SX + TerrainOffsetX + 7777.0f) * BreakupScale,
                (SY + TerrainOffsetY + 7777.0f) * BreakupScale));
            const float Breakup01 = (BreakupRaw + 1.0f) * 0.5f;
            const float BreakupCarve = FMath::Clamp((0.35f - Breakup01) / 0.35f, 0.0f, 1.0f) * 0.10f;

            // ── Interior terrain noise (replaces flat ContinentBoost) ──
            // Instead of pushing all continent interiors to uniform high
            // elevation, we use noise to create varied terrain: plains next
            // to rolling hills next to ridge-mountains.  The Interior value
            // acts as a MASK (fading features toward the coast) rather than
            // a direct elevation source.
            float InteriorContribution = 0.0f;

            if (BestInterior > 0.01f)
            {
                // Detail noise — rolling hills, plains, gentle valleys within the continent
                const float DetailFBM = PerlinFBM2D(
                    (SX + TerrainOffsetX + 500.0f) * InteriorDetailScale,
                    (SY + TerrainOffsetY + 500.0f) * InteriorDetailScale,
                    4, 0.5f, 2.0f);
                const float DetailNorm = (DetailFBM + 1.0f) * 0.5f;  // remap -1..1 → 0..1

                // Ridge noise — sharp mountain-range-like features via abs trick
                const float RidgeRaw = FMath::PerlinNoise2D(FVector2D(
                    (SX + TerrainOffsetX + 1000.0f) * InteriorRidgeScale,
                    (SY + TerrainOffsetY + 1000.0f) * InteriorRidgeScale));
                float RidgeVal = 1.0f - FMath::Abs(RidgeRaw);   // invert abs → sharp ridges
                RidgeVal = RidgeVal * RidgeVal;                   // square to sharpen peaks

                // ── Coastal-fade mask (kills the sombrero) ──
                // Instead of multiplying noise by BestInterior (which peaks at
                // center → dome), use a threshold that is 1.0 across most of
                // the continent interior and only fades in the outermost 25%
                // coastal fringe.  This means terrain noise is at full strength
                // everywhere except right at the shoreline.
                const float CoastalFade = FMath::Clamp((BestInterior - 0.08f) / 0.20f, 0.0f, 1.0f);
                // CoastalFade = 0 when Interior < 0.10 (shore)
                // CoastalFade = 1 when Interior > 0.35 (most of the continent)

                // NO GentleRise — that was the dome.  Let noise alone drive
                // elevation so mountains appear wherever the ridge noise peaks,
                // not at the geometric center.
                const float DetailLayer = DetailNorm * 0.08f * CoastalFade;
                const float RidgeLayer = RidgeVal * 0.42f * CoastalFade;

                InteriorContribution = DetailLayer + RidgeLayer;

                // Islands get reduced interior contribution — they shouldn't
                // have massive mountain ranges.
                if (bBestIsIsland)
                {
                    InteriorContribution *= 0.40f;
                }

                // ── Volcanic hotspot detection (for future Step) ──
                // Record tiles where ridge noise peaks inside continent interiors.
                // These are natural volcano placement sites.
                if (RidgeVal > 0.96f && BestInterior > 0.45f)
                {
                    VolcanicHotspots.Add(FIntPoint(X, Y));
                }
            }

            // ── Combine ──
            const float LandBias = 0.12f;

            float Elevation =
                Macro01A * 0.55f +
                Macro01B * 0.30f +
                Detail01 * 0.15f +
                LandBias
                - BreakupCarve;

            // Apply continent mask: outside blobs → crush to ocean floor.
            // Inside blobs → keep elevation + add interior terrain contribution
            // (which replaces the old flat ContinentBoost).
            const float OceanFloor = 0.24f;
            Elevation = FMath::Lerp(
                Elevation * OceanFloor,
                Elevation + InteriorContribution,
                ContinentMask);

            // Apply edge-ocean ring on top (drown the outer rim)
            Elevation = FMath::Lerp(Elevation, Elevation * OceanFloor, EdgeOcean);

            Tile.Elevation = FMath::Clamp(Elevation, 0.0f, 1.0f);

            // ── Temperature ──
            const float Latitude01 = 1.0f - FMath::Abs((SY / float(GridHeight - 1)) * 2.0f - 1.0f);

            const float TempNoise = FMath::PerlinNoise2D(FVector2D(
                (SX + BiomeOffsetX) * TempScale,
                (SY + BiomeOffsetY) * TempScale));
            const float TempNoise01 = (TempNoise + 1.0f) * 0.5f;

            float Temperature =
                Latitude01 * 0.70f +
                TempNoise01 * 0.30f;
            Temperature -= Tile.Elevation * 0.25f;
            Tile.Temperature = FMath::Clamp(Temperature, 0.0f, 1.0f);

            // ── Rainfall ──
            const float RainNoiseA = FMath::PerlinNoise2D(FVector2D(
                (SX + BiomeOffsetX + 3000.0f) * RainScale,
                (SY + BiomeOffsetY + 3000.0f) * RainScale));
            const float RainNoiseB = FMath::PerlinNoise2D(FVector2D(
                (SX + BiomeOffsetX - 1500.0f) * (RainScale * 2.0f),
                (SY + BiomeOffsetY - 1500.0f) * (RainScale * 2.0f)));

            const float Rain01A = (RainNoiseA + 1.0f) * 0.5f;
            const float Rain01B = (RainNoiseB + 1.0f) * 0.5f;

            float Rainfall =
                Rain01A * 0.7f +
                Rain01B * 0.3f;
            Rainfall += (1.0f - Tile.Elevation) * 0.10f;
            Tile.Rainfall = FMath::Clamp(Rainfall, 0.0f, 1.0f);
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Step02 done. Seeds=%d (continents=%d islands=%d) VolcanicHotspots=%d"),
        Seeds.Num(), NumContinents, Seeds.Num() - NumContinents, VolcanicHotspots.Num());
}

void UWorldGenPipeline::Step03_RunTectonics()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step03: Tectonics..."));

    MountainRanges.Reset();
    MountainProvinces.Reset();

    const FMountainGenerationSettings& Settings = CurrentConfig.MountainSettings;

    // Generate tectonic provinces and mountain ranges using the project RNG only.
    const int32 ProvinceCount = FMath::Clamp(
        RNGRef->NextInt(UWorldForgeRNG::BRANCH_TERRAIN, Settings.MinProvinceCount, Settings.MaxProvinceCount),
        1,
        64);

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
            RNGRef->NextInt(UWorldForgeRNG::BRANCH_TERRAIN, Settings.MinRangesPerProvince, Settings.MaxRangesPerProvince),
            1,
            32);

        MountainProvinces.Add(Province);
    }

    for (const FMountainProvince& Province : MountainProvinces)
    {
        for (int32 RangeIdx = 0; RangeIdx < Province.RangeCount; ++RangeIdx)
        {
            const float DirAlpha = RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN);
            const float DirJitter = FMath::Lerp(
                -Settings.ProvinceDirectionJitterDegrees,
                Settings.ProvinceDirectionJitterDegrees,
                DirAlpha);

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

            const float BaseHeight = FMath::Lerp(
                Settings.MinHeightMultiplier,
                Settings.MaxHeightMultiplier,
                RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));

            Range.HeightMultiplier = BaseHeight * Province.Strength;

            Range.BasinOffset = Range.Width * FMath::Lerp(1.0f, 1.8f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
            Range.BasinWidth = Range.Width * FMath::Lerp(0.7f, 1.3f, RNGRef->NextFloat(UWorldForgeRNG::BRANCH_TERRAIN));
            Range.BasinStrength = Range.HeightMultiplier * Settings.BasinStrengthScale;

            MountainRanges.Add(Range);
        }
    }

    // Keep some broad tectonic variation, but let ridge ranges drive the visible structure.
    const float PlateStrength = 0.03f;
    const float RidgeNoiseStrength = 0.18f;
    const float ValleyNoiseStrength = 0.09f;
    const float PlateScale = 0.004f;
    const float RidgeScale = 0.0014f;

    for (int32 Y = 0; Y < GridHeight; Y++)
    {
        for (int32 X = 0; X < GridWidth; X++)
        {
            const int32 Index = TileIndex(X, Y);
            FWorldTileData& Tile = WorldTiles[Index];

            float Elevation = Tile.Elevation;

            const float PlateNoise =
                FMath::PerlinNoise2D(FVector2D(X * PlateScale, Y * PlateScale));
            Elevation += PlateNoise * PlateStrength;

            const float RidgeNoise =
                FMath::Abs(FMath::PerlinNoise2D(FVector2D(X * RidgeScale, Y * RidgeScale)));
            Elevation += RidgeNoise * RidgeNoiseStrength;

            const float ValleyNoise =
                FMath::PerlinNoise2D(FVector2D((X + 1000) * RidgeScale, (Y + 1000) * RidgeScale));
            Elevation -= FMath::Abs(ValleyNoise) * ValleyNoiseStrength;

            const float MountainContribution = CalculateMountainContributionAtTile(
                FVector2D(float(X), float(Y)),
                MountainRanges,
                Settings.FalloffSharpness);

            Elevation += MountainContribution;
            Elevation = FMath::Clamp(Elevation, 0.0f, 1.0f);

            Tile.Elevation = Elevation;
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Step03 done. Provinces=%d Ranges=%d"),
        MountainProvinces.Num(),
        MountainRanges.Num());
}

void UWorldGenPipeline::Step04_AssignBiomes()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step04: Biomes..."));

    // Pre-cache biome tags outside the tight per-tile loop.
    // ── Water biomes ──
    const FGameplayTag DeepOceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.DeepOcean")));
    const FGameplayTag OceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Ocean")));
    const FGameplayTag SeaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Sea")));
    const FGameplayTag CoastalTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")));

    // ── Highland biomes ──
    const FGameplayTag MountainTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")));
    const FGameplayTag SnowPeakTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.SnowPeak")));
    const FGameplayTag AlpineTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Alpine")));
    const FGameplayTag HillsTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Hills")));

    // ── Temperature-driven land biomes ──
    const FGameplayTag TundraTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Tundra")));
    const FGameplayTag TaigaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Taiga")));
    const FGameplayTag DesertTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Desert")));
    const FGameplayTag SavannaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Savanna")));
    const FGameplayTag JungleTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Jungle")));

    // ── Moisture-driven land biomes ──
    const FGameplayTag ForestTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Forest")));
    const FGameplayTag WoodlandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Woodland")));
    const FGameplayTag GrasslandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Grassland")));
    const FGameplayTag PlainsTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Plains")));
    const FGameplayTag MarshTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Marsh")));
    const FGameplayTag SwampTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Swamp")));

    for (FWorldTileData& Tile : WorldTiles)
    {
        // ==================================================================
        // ---- Water / coastal biomes ----
        // Thresholds aligned with the voxel height formula so the biome map
        // agrees with what the player actually sees in 3D.
        // ==================================================================
        if (Tile.Elevation < 0.30f)
        {
            Tile.BiomeType = DeepOceanTag;
        }
        else if (Tile.Elevation < 0.40f)
        {
            Tile.BiomeType = OceanTag;
        }
        else if (Tile.Elevation < 0.48f)
        {
            Tile.BiomeType = SeaTag;
        }
        else if (Tile.Elevation < 0.53f)
        {
            Tile.BiomeType = CoastalTag;
        }
        // ==================================================================
        // ---- Highland biomes (elevation-dominant) ----
        // Split the old single "Mountain" band into three tiers so that the
        // map shows snow-capped peaks, exposed alpine rock, and forested
        // mountains as distinct features.
        // ==================================================================
        else if (Tile.Elevation > 0.82f && Tile.Temperature < 0.35f)
        {
            // Extreme peaks with cold temps → permanent snow cap
            Tile.BiomeType = SnowPeakTag;
        }
        else if (Tile.Elevation > 0.76f)
        {
            // Very high but not frozen → bare mountain rock
            Tile.BiomeType = MountainTag;
        }
        else if (Tile.Elevation > 0.68f && Tile.Temperature < 0.40f)
        {
            // High + cold → alpine meadow / treeline
            Tile.BiomeType = AlpineTag;
        }
        else if (Tile.Elevation > 0.68f)
        {
            // High but warm → still reads as mountain
            Tile.BiomeType = MountainTag;
        }
        else if (Tile.Elevation > 0.58f)
        {
            Tile.BiomeType = HillsTag;
        }
        // ==================================================================
        // ---- Lowland biomes (temperature × rainfall matrix) ----
        // These produce the variety you need: tundra, taiga, desert, savanna,
        // jungle, forest, woodland, grassland, plains, marsh, swamp.
        // Extend this section as you add more biome tags.
        // ==================================================================

        // ── Frozen band (Temperature < 0.25) ──
        else if (Tile.Temperature < 0.25f)
        {
            Tile.BiomeType = (Tile.Rainfall > 0.40f) ? TaigaTag : TundraTag;
        }

        // ── Cold-temperate band (0.25–0.45) ──
        else if (Tile.Temperature < 0.45f)
        {
            if (Tile.Rainfall > 0.65f)
                Tile.BiomeType = ForestTag;       // dense conifer/mixed forest
            else if (Tile.Rainfall > 0.40f)
                Tile.BiomeType = WoodlandTag;     // open canopy
            else
                Tile.BiomeType = GrasslandTag;    // cold steppe
        }

        // ── Warm-temperate band (0.45–0.70) ──
        else if (Tile.Temperature < 0.70f)
        {
            if (Tile.Rainfall > 0.70f)
                Tile.BiomeType = ForestTag;       // deciduous forest
            else if (Tile.Rainfall > 0.45f)
                Tile.BiomeType = WoodlandTag;
            else if (Tile.Rainfall > 0.25f)
                Tile.BiomeType = PlainsTag;
            else
                Tile.BiomeType = DesertTag;       // temperate desert (cold desert)
        }

        // ── Hot band (Temperature >= 0.70) ──
        else
        {
            if (Tile.Rainfall > 0.75f)
            {
                // Hot + very wet: jungle or swamp depending on elevation
                Tile.BiomeType = (Tile.Elevation < 0.57f) ? SwampTag : JungleTag;
            }
            else if (Tile.Rainfall > 0.55f)
            {
                // Hot + moderately wet: marsh or jungle-edge
                Tile.BiomeType = (Tile.Elevation < 0.57f) ? MarshTag : JungleTag;
            }
            else if (Tile.Rainfall > 0.30f)
            {
                Tile.BiomeType = SavannaTag;
            }
            else
            {
                Tile.BiomeType = DesertTag;
            }
        }

        // ==================================================================
        // Voxel profile -- bSmoothSDF ALWAYS true
        // ==================================================================
        Tile.VoxelProfile.bSmoothSDF = true;

        const bool bIsWaterTile =
            Tile.BiomeType.MatchesTag(DeepOceanTag) ||
            Tile.BiomeType.MatchesTag(OceanTag) ||
            Tile.BiomeType.MatchesTag(SeaTag);

        if (bIsWaterTile)
        {
            Tile.VoxelProfile.NoiseAmplitude = 8.0f;
            Tile.VoxelProfile.NoiseFrequency = 0.005f;
            Tile.VoxelProfile.OctaveCount = 1;
            Tile.VoxelProfile.bEnableCaves = false;
        }
        else
        {
            // Land and coastal tiles: scale profile to elevation.
            const float ElevationAlpha = FMath::Clamp(Tile.Elevation, 0.0f, 1.0f);

            Tile.VoxelProfile.NoiseAmplitude = FMath::Lerp(20.0f, 220.0f, ElevationAlpha);
            Tile.VoxelProfile.NoiseFrequency = FMath::Lerp(0.008f, 0.028f, ElevationAlpha);
            Tile.VoxelProfile.OctaveCount = (ElevationAlpha > 0.75f) ? 5 : ((ElevationAlpha > 0.45f) ? 4 : 3);
            Tile.VoxelProfile.bEnableCaves = (Tile.Elevation > 0.55f);

            // Mountain / SnowPeak / Alpine override
            if (Tile.BiomeType.MatchesTag(MountainTag) ||
                Tile.BiomeType.MatchesTag(SnowPeakTag) ||
                Tile.BiomeType.MatchesTag(AlpineTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Max(Tile.VoxelProfile.NoiseAmplitude, 520.0f);
                Tile.VoxelProfile.NoiseFrequency = FMath::Max(Tile.VoxelProfile.NoiseFrequency, 0.045f);
                Tile.VoxelProfile.OctaveCount = FMath::Max(Tile.VoxelProfile.OctaveCount, 6);
                Tile.VoxelProfile.bEnableCaves = true;
            }
            // Coastal band: keep very flat so the shoreline doesn't spike.
            else if (Tile.BiomeType.MatchesTag(CoastalTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Min(Tile.VoxelProfile.NoiseAmplitude, 28.0f);
                Tile.VoxelProfile.NoiseFrequency = FMath::Min(Tile.VoxelProfile.NoiseFrequency, 0.010f);
                Tile.VoxelProfile.OctaveCount = 2;
                Tile.VoxelProfile.bEnableCaves = false;
            }
            // Desert / Savanna: gentle terrain, no caves.
            else if (Tile.BiomeType.MatchesTag(DesertTag) ||
                Tile.BiomeType.MatchesTag(SavannaTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Min(Tile.VoxelProfile.NoiseAmplitude, 65.0f);
                Tile.VoxelProfile.NoiseFrequency = FMath::Min(Tile.VoxelProfile.NoiseFrequency, 0.014f);
                Tile.VoxelProfile.OctaveCount = 3;
            }
            // Swamp / Marsh: very flat, low noise.
            else if (Tile.BiomeType.MatchesTag(SwampTag) ||
                Tile.BiomeType.MatchesTag(MarshTag))
            {
                Tile.VoxelProfile.NoiseAmplitude = FMath::Min(Tile.VoxelProfile.NoiseAmplitude, 18.0f);
                Tile.VoxelProfile.NoiseFrequency = FMath::Min(Tile.VoxelProfile.NoiseFrequency, 0.008f);
                Tile.VoxelProfile.OctaveCount = 2;
                Tile.VoxelProfile.bEnableCaves = false;
            }
        }
    }

    // ── Diagnostic: count biome distribution ──
    int32 WaterCount = 0;
    int32 LandCount = 0;
    for (const FWorldTileData& Tile : WorldTiles)
    {
        const bool bWater =
            Tile.BiomeType.MatchesTag(DeepOceanTag) ||
            Tile.BiomeType.MatchesTag(OceanTag) ||
            Tile.BiomeType.MatchesTag(SeaTag);
        if (bWater) ++WaterCount; else ++LandCount;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[WorldGen] Step04 done. Water=%d (%d%%) Land=%d (%d%%)"),
        WaterCount,
        (WorldTiles.Num() > 0) ? (WaterCount * 100 / WorldTiles.Num()) : 0,
        LandCount,
        (WorldTiles.Num() > 0) ? (LandCount * 100 / WorldTiles.Num()) : 0);
}

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

    // ── FIX (Issue 1 — Dual Mountain Brain):
    // Feed Step03's tectonic mountain ranges to the voxel terrain generator
    // BEFORE terrain generation starts.  This ensures the 3D voxel terrain
    // places mountains in the same locations as the 2D tile map.
    VoxelMgr->SetPipelineMountainRanges(MountainRanges);

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

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WorldGen] Step05 done. TerrainMode=%s BlockHeightStep=%d"),
        CurrentConfig.TerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
        CurrentConfig.BlockHeightStep
    );
}

void UWorldGenPipeline::Step06_PCGDecorationPass()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step06: Decoration hook..."));

    HandlePCGDecorationPass(WorldTiles, GridWidth, GridHeight);

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step06 done"));
}

void UWorldGenPipeline::Step07_ApplyWorldDriver()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step07 -- TODO"));
}

void UWorldGenPipeline::Step08_SeedResources()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step08: Seeding resources..."));

    const FGameplayTag StoneTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Stone")));
    const FGameplayTag WoodTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Wood")));
    const FGameplayTag OreTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Ore")));

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

    for (FWorldTileData& Tile : WorldTiles)
    {
        Tile.ResourceTags.Reset();

        // -----------------------------
        // Mountain / SnowPeak / Alpine: Stone + Ore
        // -----------------------------
        if (Tile.BiomeType.MatchesTag(MountainTag) ||
            Tile.BiomeType.MatchesTag(SnowPeakTag) ||
            Tile.BiomeType.MatchesTag(AlpineTag))
        {
            Tile.ResourceTags.Add(StoneTag);

            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.5f)
            {
                Tile.ResourceTags.Add(OreTag);
            }
        }

        // -----------------------------
        // Hills: moderate stone chance
        // -----------------------------
        else if (Tile.BiomeType.MatchesTag(HillsTag))
        {
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.4f)
            {
                Tile.ResourceTags.Add(StoneTag);
            }
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.75f)
            {
                Tile.ResourceTags.Add(OreTag);
            }
        }

        // -----------------------------
        // Forest / Woodland / Jungle: Wood (+ small stone chance)
        // -----------------------------
        else if (Tile.BiomeType.MatchesTag(ForestTag) ||
            Tile.BiomeType.MatchesTag(WoodlandTag) ||
            Tile.BiomeType.MatchesTag(JungleTag))
        {
            Tile.ResourceTags.Add(WoodTag);

            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.8f)
            {
                Tile.ResourceTags.Add(StoneTag);
            }
        }

        // -----------------------------
        // Swamp / Marsh: Wood at lower rate
        // -----------------------------
        else if (Tile.BiomeType.MatchesTag(SwampTag) ||
            Tile.BiomeType.MatchesTag(MarshTag))
        {
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.5f)
            {
                Tile.ResourceTags.Add(WoodTag);
            }
        }

        // -----------------------------
        // Desert: mostly empty
        // -----------------------------
        else if (Tile.BiomeType.MatchesTag(DesertTag))
        {
            if (RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.9f)
            {
                Tile.ResourceTags.Add(StoneTag);
            }
        }

        // -----------------------------
        // General fallback (mid terrain)
        // -----------------------------
        else
        {
            if (Tile.Elevation > 0.4f &&
                RNGRef->NextFloat(UWorldForgeRNG::BRANCH_RESOURCES) > 0.7f)
            {
                Tile.ResourceTags.Add(StoneTag);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step08 done"));
}

// Steps 12, 14, 15, 16 remain stubs for now
void UWorldGenPipeline::Step12_GenerateCosmicRoster()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step12 -- TODO"));
}

void UWorldGenPipeline::Step14_AssignWorldTraits()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step14 -- TODO"));
}

void UWorldGenPipeline::Step15_RecordFinalState()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step15 -- TODO"));
}

void UWorldGenPipeline::Step16_FinalizeWorldMap()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step16 -- TODO"));
}

void UWorldGenPipeline::Step09_PlaceCivilizations()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step09: Placing civilizations..."));

    if (!RNGRef)
    {
        return;
    }

    UCivilizationGenerator* CivGen = NewObject<UCivilizationGenerator>(this);
    Civilizations = CivGen->GenerateCivilizations(
        CurrentConfig.StartingCivCount, WorldTiles, GridWidth, RNGRef);

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step09: %d civs placed"), Civilizations.Num());
}

void UWorldGenPipeline::Step10_GenerateCulturalDNA()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step10: Cultural DNA..."));

    if (!RNGRef)
    {
        return;
    }

    UCivilizationGenerator* CivGen = NewObject<UCivilizationGenerator>(this);
    CivGen->GenerateCulturalDNA(Civilizations, WorldTiles, RNGRef);

    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step10: Cultural DNA done for %d civs"), Civilizations.Num());
}

void UWorldGenPipeline::Step11_GenerateEconomyLayer()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldGen] Step11: Economy layer..."));

    if (!WorldRef)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step11: WorldRef null"));
        return;
    }

    if (UWorldEconomySimulation* Econ = WorldRef->GetSubsystem<UWorldEconomySimulation>())
    {
        Econ->GenerateWorldEconomy(Civilizations, WorldTiles, Currencies, RNGRef);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGen] Step11: UWorldEconomySimulation not found"));
    }

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
            FString RawName = Context.GeneratedNames[Index];

            RawName = CleanNameArtifacts(RawName);

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

            FText NameText = FText::FromString(FinalName);
            Civilizations[Index].Name = NameText.ToString();

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[WorldHistory] Assigned Civilization %d Name: %s"),
                Civilizations[Index].CivID,
                *Civilizations[Index].Name);
        }

        if (Context.GeneratedFigures.Num() == Civilizations.Num())
        {
            for (int32 Index = 0; Index < Civilizations.Num(); ++Index)
            {
                Civilizations[Index].LegendaryFigure = Context.GeneratedFigures[Index];

                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[WorldHistory] Civilization %d Figure Stored As: %s"),
                    Civilizations[Index].CivID,
                    *Civilizations[Index].LegendaryFigure);
            }
        }

        for (int32 Index = 0; Index < NameCount; ++Index)
        {
            if (Context.RaceOrigins.IsValidIndex(Index))
            {
                Civilizations[Index].RaceOrigin = Context.RaceOrigins[Index];

                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[WorldHistory] Civilization %d Origin Stored As: %s"),
                    Civilizations[Index].CivID,
                    *Civilizations[Index].RaceOrigin);
            }
        }
    }
}