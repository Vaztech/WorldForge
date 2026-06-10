#include "Utility/WorldForgeTerrainMath.h"

// ════════════════════════════════════════════════════════════════════
// WorldForgeTerrainMath — v3.0
//
// Design principles:
//   1. Tile.Elevation (0–1) is the ONLY source of MACRO terrain shape.
//   2. PipelineMountainRanges add LOCALIZED ridge displacement on top.
//   3. PipelineVolcanoes add composite cone+crater displacement on top.
//   4. Ridge detail is narrow, sharp, and jagged — not blobs.
//   5. Voxel system is a pure renderer of this data.
//
// The pipeline (Step02 + Step03) encodes continents and general
// elevation into Tile.Elevation.  Step03 also stores mountain range
// line segments and volcano primitives.  This file converts all of
// them into world-space voxel heights: a smooth macro layer plus
// sharp ridge detail plus volcanic cone geometry.
// ════════════════════════════════════════════════════════════════════

namespace
{
    constexpr float LakeBasinCoreScale = 0.65f;
    constexpr float LakeBasinEdgeBlendScale = 1.25f;
    constexpr float LakeMinShorelineFeatherTiles = 2.0f;
    constexpr float LakeMaxShorelineFeatherTiles = 3.0f;
    constexpr float LakeMinFloorLoweringVoxels = 0.50f;
    constexpr float LakeMaxFloorLoweringVoxels = 0.70f;
    constexpr float LakeOuterDryInfluence = 0.70f;
    constexpr float CoastShoreFeatherTiles = 2.5f;
    constexpr float CoastMaxLoweringVoxels = 10.0f;
    constexpr float CoastMinLandAboveSea = 1.5f;
    constexpr float CoastRampStrength = 0.45f;
    constexpr float CoastOrdinaryCliffStartAboveSea = 8.0f;
    constexpr float CoastOrdinaryCliffEndAboveSea = 20.0f;
    constexpr float CoastMaxShapeHeightAboveSea = 22.0f;
    constexpr float CoastLargeDeltaWarningVoxels = 9.5f;
    constexpr float CoastWaterDepthEpsilon = 0.01f;
    constexpr float CoastDeltaLogThreshold = 0.10f;
    constexpr int32 CoastSearchRadiusTiles = 3;

    struct FWorldForgeLakeShapeSample
    {
        const FLakeBasinData* Basin = nullptr;
        float Influence = 0.0f;
        float DesiredFloorHeight = 0.0f;
        float WaterSurfaceHeight = 0.0f;
        float LoweringVoxels = 0.0f;
    };

    static float WF_Bilerp(float V00, float V10, float V01, float V11, float FX, float FY)
    {
        const float X0 = FMath::Lerp(V00, V10, FX);
        const float X1 = FMath::Lerp(V01, V11, FX);
        return FMath::Lerp(X0, X1, FY);
    }

    // Simple 2D hash-based noise for crag detail.
    // Avoids pulling in heavy noise libraries for a small overlay.
    static float WF_CragNoise2D(float X, float Y)
    {
        return FMath::PerlinNoise2D(FVector2D(X, Y));
    }

    static FGameplayTag WF_GetBiomeCoastalTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")), false);
        return Tag;
    }

    static FGameplayTag WF_GetBiomeOceanTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Ocean")), false);
        return Tag;
    }

    static FGameplayTag WF_GetBiomeSeaTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Sea")), false);
        return Tag;
    }

    static FGameplayTag WF_GetBiomeDeepOceanTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.DeepOcean")), false);
        return Tag;
    }

    static FGameplayTag WF_GetBiomeWaterTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Water")), false);
        return Tag;
    }

    static FGameplayTag WF_GetBiomeLakeTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lake")), false);
        return Tag;
    }

    static FGameplayTag WF_GetBiomeMountainTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")), false);
        return Tag;
    }

    static FGameplayTag WF_GetBiomeCanyonTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Canyon")), false);
        return Tag;
    }

    static FGameplayTag WF_GetBiomeVolcanicTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);
        return Tag;
    }

    static FGameplayTag WF_GetBiomeLavaTag()
    {
        static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);
        return Tag;
    }

    static float WF_Smooth01(float T)
    {
        T = FMath::Clamp(T, 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }

    static float WF_SmoothStepRange(float Value, float RangeMin, float RangeMax)
    {
        if (RangeMax <= RangeMin)
        {
            return Value >= RangeMax ? 1.0f : 0.0f;
        }

        return WF_Smooth01((Value - RangeMin) / (RangeMax - RangeMin));
    }

    static float WF_ComputeLakeFloorLoweringVoxels(const FWorldForgeTerrainSnapshot& Snapshot)
    {
        return FMath::Clamp(
            static_cast<float>(Snapshot.LakeBasinDepthVoxels) * 0.05f,
            LakeMinFloorLoweringVoxels,
            LakeMaxFloorLoweringVoxels);
    }

    static FVector2D WF_VoxelToTileCoords(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot)
    {
        const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
        const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
        const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;

        return FVector2D(
            (VoxelX / ChunkSize) + HalfGridX,
            (VoxelY / ChunkSize) + HalfGridY);
    }

    static FIntPoint WF_VoxelToTileCell(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot)
    {
        const FVector2D TileCoords = WF_VoxelToTileCoords(VoxelX, VoxelY, Snapshot);
        return Snapshot.ClampCell(FIntPoint(
            FMath::FloorToInt(TileCoords.X),
            FMath::FloorToInt(TileCoords.Y)));
    }

    static bool WF_IsCoastalWaterTile(
        const FWorldForgeTerrainSnapshot& Snapshot,
        const FIntPoint& Cell)
    {
        const FGameplayTag BiomeTag = Snapshot.GetBiomeTag(Cell);
        if (!BiomeTag.IsValid())
        {
            return false;
        }

        if (BiomeTag.MatchesTag(WF_GetBiomeLakeTag()))
        {
            return false;
        }

        const bool bTaggedAsCoastalWater =
            BiomeTag.MatchesTag(WF_GetBiomeOceanTag()) ||
            BiomeTag.MatchesTag(WF_GetBiomeSeaTag()) ||
            BiomeTag.MatchesTag(WF_GetBiomeDeepOceanTag()) ||
            BiomeTag.MatchesTag(WF_GetBiomeWaterTag()) ||
            BiomeTag.MatchesTag(WF_GetBiomeCoastalTag());

        if (!bTaggedAsCoastalWater)
        {
            return false;
        }

        const FWorldTileData* Tile = Snapshot.GetTile(Cell);
        if (!Tile)
        {
            return false;
        }

        const float SafeWaterThreshold = FMath::Clamp(Snapshot.WaterThreshold + 0.02f, 0.0f, 1.0f);
        return Tile->Elevation <= SafeWaterThreshold || Tile->WaterDepth > CoastWaterDepthEpsilon;
    }

    static bool WF_ShouldPreserveCoastalCliff(const FGameplayTag& BiomeTag)
    {
        if (!BiomeTag.IsValid())
        {
            return false;
        }

        return BiomeTag.MatchesTag(WF_GetBiomeLavaTag()) ||
            BiomeTag.MatchesTag(WF_GetBiomeVolcanicTag()) ||
            BiomeTag.MatchesTag(WF_GetBiomeMountainTag()) ||
            BiomeTag.MatchesTag(WF_GetBiomeCanyonTag());
    }

    static float WF_EllipseRadiusAlongDirection(const FVector2D& DirectionUnit, float RadiusX, float RadiusY)
    {
        const float SafeRadiusX = FMath::Max(0.25f, RadiusX);
        const float SafeRadiusY = FMath::Max(0.25f, RadiusY);
        const float Denominator =
            ((DirectionUnit.X * DirectionUnit.X) / (SafeRadiusX * SafeRadiusX)) +
            ((DirectionUnit.Y * DirectionUnit.Y) / (SafeRadiusY * SafeRadiusY));

        if (Denominator <= KINDA_SMALL_NUMBER)
        {
            return FMath::Min(SafeRadiusX, SafeRadiusY);
        }

        return 1.0f / FMath::Sqrt(Denominator);
    }

    static FWorldForgeLakeShapeSample WF_EvaluateLakeShapeSample(
        float VoxelX,
        float VoxelY,
        const FWorldForgeTerrainSnapshot& Snapshot)
    {
        FWorldForgeLakeShapeSample BestSample;

        if (!Snapshot.IsValid() || Snapshot.PipelineLakeBasins.Num() == 0)
        {
            return BestSample;
        }

        const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
        const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
        const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;
        const FVector2D SampleTile(
            (VoxelX / ChunkSize) + HalfGridX,
            (VoxelY / ChunkSize) + HalfGridY);

        const float WaterDepth = FWorldForgeTerrainMath::SampleTileWaterDepthBilinear(VoxelX, VoxelY, Snapshot);
        const float WaterMask = WF_SmoothStepRange(WaterDepth, 0.01f, 0.08f);
        const float MaxLoweringVoxels = WF_ComputeLakeFloorLoweringVoxels(Snapshot);

        for (const FLakeBasinData& Basin : Snapshot.PipelineLakeBasins)
        {
            if (Basin.TileCount <= 0)
            {
                continue;
            }

            const FVector2D BasinCenter(
                static_cast<float>(Basin.CenterTileX) + 0.5f,
                static_cast<float>(Basin.CenterTileY) + 0.5f);

            const float BasinHalfExtentTilesX =
                FMath::Max(0.75f, static_cast<float>(Basin.MaxTileX - Basin.MinTileX + 1) * 0.5f);
            const float BasinHalfExtentTilesY =
                FMath::Max(0.75f, static_cast<float>(Basin.MaxTileY - Basin.MinTileY + 1) * 0.5f);
            const float ShorelineFeatherTiles = FMath::Clamp(
                Basin.ShoreFalloffTiles > KINDA_SMALL_NUMBER
                    ? Basin.ShoreFalloffTiles
                    : 2.5f,
                LakeMinShorelineFeatherTiles,
                LakeMaxShorelineFeatherTiles);

            const float CoreRadiusX = FMath::Max(0.50f, BasinHalfExtentTilesX * LakeBasinCoreScale);
            const float CoreRadiusY = FMath::Max(0.50f, BasinHalfExtentTilesY * LakeBasinCoreScale);
            const float OuterRadiusX = FMath::Max(
                BasinHalfExtentTilesX * LakeBasinEdgeBlendScale,
                BasinHalfExtentTilesX + ShorelineFeatherTiles);
            const float OuterRadiusY = FMath::Max(
                BasinHalfExtentTilesY * LakeBasinEdgeBlendScale,
                BasinHalfExtentTilesY + ShorelineFeatherTiles);

            const FVector2D Offset = SampleTile - BasinCenter;
            const float DistanceTiles = Offset.Size();
            const FVector2D DirectionUnit = DistanceTiles > KINDA_SMALL_NUMBER
                ? (Offset / DistanceTiles)
                : FVector2D(1.0f, 0.0f);

            const float CoreRadiusAlongDirection = WF_EllipseRadiusAlongDirection(
                DirectionUnit,
                CoreRadiusX,
                CoreRadiusY);
            const float OuterRadiusAlongDirection = WF_EllipseRadiusAlongDirection(
                DirectionUnit,
                OuterRadiusX,
                OuterRadiusY);

            if (DistanceTiles >= OuterRadiusAlongDirection)
            {
                continue;
            }

            float Influence = 1.0f - WF_SmoothStepRange(
                DistanceTiles,
                CoreRadiusAlongDirection,
                OuterRadiusAlongDirection);
            Influence *= FMath::Lerp(LakeOuterDryInfluence, 1.0f, WaterMask);
            Influence = FMath::Clamp(Influence, 0.0f, 1.0f);

            if (Influence <= BestSample.Influence)
            {
                continue;
            }

            const float WaterSurfaceHeight = FWorldForgeTerrainMath::ElevationToVoxelHeight(
                Basin.WaterSurfaceElevation01,
                Snapshot);
            const float BasinFloorHeight = FWorldForgeTerrainMath::ElevationToVoxelHeight(
                Basin.FloorElevation01,
                Snapshot);
            const float LoweringVoxels = FMath::Clamp(
                WaterSurfaceHeight - BasinFloorHeight,
                LakeMinFloorLoweringVoxels,
                MaxLoweringVoxels);

            BestSample.Basin = &Basin;
            BestSample.Influence = Influence;
            BestSample.WaterSurfaceHeight = WaterSurfaceHeight;
            BestSample.LoweringVoxels = LoweringVoxels;
            BestSample.DesiredFloorHeight = WaterSurfaceHeight - LoweringVoxels;
        }

        return BestSample;
    }

    // Distance from point P to line segment (A → B).
    // Returns the minimum distance and writes the parametric T (0–1) along AB.
    static float PointToSegmentDistance2D(
        const FVector2D& P,
        const FVector2D& A,
        const FVector2D& B,
        float& OutT)
    {
        const FVector2D AB = B - A;
        const float ABLenSq = AB.SizeSquared();

        if (ABLenSq <= KINDA_SMALL_NUMBER)
        {
            OutT = 0.0f;
            return FVector2D::Distance(P, A);
        }

        OutT = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / ABLenSq, 0.0f, 1.0f);
        const FVector2D ClosestPoint = A + AB * OutT;
        return FVector2D::Distance(P, ClosestPoint);
    }
}

// ─── Core elevation → height conversion ─────────────────────────

float FWorldForgeTerrainMath::ElevationToVoxelHeight(
    float Elevation01,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    const float Elev = FMath::Clamp(Elevation01, 0.0f, 1.0f);
    const float SeaLevel = static_cast<float>(Snapshot.SeaLevelVoxels);
    const float WaterThreshold = FMath::Clamp(Snapshot.WaterThreshold, 0.05f, 0.95f);

    if (Elev <= WaterThreshold)
    {
        // ── Underwater: [0, WaterThreshold] → [SeaLevel - OceanDepth, SeaLevel]
        const float T = Elev / WaterThreshold;
        const float OceanDepth = static_cast<float>(FMath::Max(1, Snapshot.OceanDepthVoxels));
        return FMath::Lerp(SeaLevel - OceanDepth, SeaLevel, T);
    }
    else
    {
        // ── Land: [WaterThreshold, 1.0] → [SeaLevel, SeaLevel + MaxLandHeight]
        const float LandRange = 1.0f - WaterThreshold;
        const float T = (Elev - WaterThreshold) / FMath::Max(0.001f, LandRange);

        const float Exponent = FMath::Max(0.1f, Snapshot.LandElevationExponent);
        const float Shaped = FMath::Pow(FMath::Clamp(T, 0.0f, 1.0f), Exponent);

        const float MaxLandHeight = static_cast<float>(FMath::Max(1, Snapshot.MaxLandHeightVoxels));
        return SeaLevel + Shaped * MaxLandHeight;
    }
}

// ─── Bilinear tile elevation sampler ─────────────────────────────

float FWorldForgeTerrainMath::SampleTileElevationBilinear(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (!Snapshot.IsValid())
    {
        return 0.0f;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;

    // Convert voxel coords to tile-grid coords (0-based)
    const float CellX = (VoxelX / ChunkSize) + HalfGridX;
    const float CellY = (VoxelY / ChunkSize) + HalfGridY;

    const int32 X0 = FMath::FloorToInt(CellX);
    const int32 Y0 = FMath::FloorToInt(CellY);
    const int32 X1 = X0 + 1;
    const int32 Y1 = Y0 + 1;

    const float FX = CellX - static_cast<float>(X0);
    const float FY = CellY - static_cast<float>(Y0);

    const FIntPoint C00 = Snapshot.ClampCell(FIntPoint(X0, Y0));
    const FIntPoint C10 = Snapshot.ClampCell(FIntPoint(X1, Y0));
    const FIntPoint C01 = Snapshot.ClampCell(FIntPoint(X0, Y1));
    const FIntPoint C11 = Snapshot.ClampCell(FIntPoint(X1, Y1));

    const float E00 = Snapshot.GetTileElevation(C00, 0.0f);
    const float E10 = Snapshot.GetTileElevation(C10, 0.0f);
    const float E01 = Snapshot.GetTileElevation(C01, 0.0f);
    const float E11 = Snapshot.GetTileElevation(C11, 0.0f);

    return WF_Bilerp(E00, E10, E01, E11, FX, FY);
}

// ─── Primary surface height (voxel resolution) ──────────────────

float FWorldForgeTerrainMath::ComputeSurfaceHeight(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    const float Elevation01 = SampleTileElevationBilinear(VoxelX, VoxelY, Snapshot);
    return ElevationToVoxelHeight(Elevation01, Snapshot);
}

// ─── Cache path (tile elevation already known) ──────────────────

float FWorldForgeTerrainMath::ComputeSurfaceHeightFromTileElevation(
    float TileElevation01,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    return ElevationToVoxelHeight(TileElevation01, Snapshot);
}

// ─── Normalized land elevation (for material classification) ────

float FWorldForgeTerrainMath::ComputeNormalizedLandElevation(
    float SurfaceHeight,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    const float SeaLevel = static_cast<float>(Snapshot.SeaLevelVoxels);
    const float MaxLandHeight = static_cast<float>(FMath::Max(1, Snapshot.MaxLandHeightVoxels));

    if (SurfaceHeight <= SeaLevel)
    {
        return 0.0f;
    }

    return FMath::Clamp((SurfaceHeight - SeaLevel) / MaxLandHeight, 0.0f, 1.0f);
}

float FWorldForgeTerrainMath::SampleCoastalWaterDistanceTiles(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (!Snapshot.IsValid())
    {
        return -1.0f;
    }

    const FVector2D TileCoords = WF_VoxelToTileCoords(VoxelX, VoxelY, Snapshot);
    const FIntPoint TileCell = WF_VoxelToTileCell(VoxelX, VoxelY, Snapshot);

    float BestDistanceTiles = -1.0f;

    for (int32 OffsetY = -CoastSearchRadiusTiles; OffsetY <= CoastSearchRadiusTiles; ++OffsetY)
    {
        for (int32 OffsetX = -CoastSearchRadiusTiles; OffsetX <= CoastSearchRadiusTiles; ++OffsetX)
        {
            const FIntPoint CandidateCell = Snapshot.ClampCell(FIntPoint(
                TileCell.X + OffsetX,
                TileCell.Y + OffsetY));

            if (!WF_IsCoastalWaterTile(Snapshot, CandidateCell))
            {
                continue;
            }

            const FVector2D CandidateCenter(
                static_cast<float>(CandidateCell.X) + 0.5f,
                static_cast<float>(CandidateCell.Y) + 0.5f);
            const float DistanceTiles = FVector2D::Distance(TileCoords, CandidateCenter);

            if (BestDistanceTiles < 0.0f || DistanceTiles < BestDistanceTiles)
            {
                BestDistanceTiles = DistanceTiles;
            }
        }
    }

    return BestDistanceTiles;
}

float FWorldForgeTerrainMath::ApplyCoastalShorelineShaping(
    float VoxelX,
    float VoxelY,
    float MacroHeight,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (!Snapshot.IsValid())
    {
        return MacroHeight;
    }

    const float SeaLevel = static_cast<float>(Snapshot.SeaLevelVoxels);
    const float LandAboveSea = MacroHeight - SeaLevel;

    if (LandAboveSea <= CoastMinLandAboveSea || LandAboveSea >= CoastMaxShapeHeightAboveSea)
    {
        return MacroHeight;
    }

    const FGameplayTag BiomeTag = Snapshot.GetBiomeTag(WF_VoxelToTileCell(VoxelX, VoxelY, Snapshot));
    if (WF_ShouldPreserveCoastalCliff(BiomeTag))
    {
        return MacroHeight;
    }

    const float CoastDistanceTiles = SampleCoastalWaterDistanceTiles(VoxelX, VoxelY, Snapshot);
    if (CoastDistanceTiles < 0.0f || CoastDistanceTiles > CoastShoreFeatherTiles)
    {
        return MacroHeight;
    }

    const float DistanceWeight = 1.0f - WF_SmoothStepRange(
        CoastDistanceTiles,
        0.0f,
        CoastShoreFeatherTiles);
    if (DistanceWeight <= KINDA_SMALL_NUMBER)
    {
        return MacroHeight;
    }

    const float CliffPreserveAlpha = WF_SmoothStepRange(
        LandAboveSea,
        CoastOrdinaryCliffStartAboveSea,
        CoastOrdinaryCliffEndAboveSea);
    const float LoweringBudgetScale = 1.0f - CliffPreserveAlpha;
    if (LoweringBudgetScale <= KINDA_SMALL_NUMBER)
    {
        return MacroHeight;
    }

    const float RampTargetHeight = SeaLevel + CoastMinLandAboveSea +
        (CoastDistanceTiles / CoastShoreFeatherTiles) * CoastMaxLoweringVoxels;
    const float TargetLowering = FMath::Max(0.0f, MacroHeight - RampTargetHeight);
    if (TargetLowering <= KINDA_SMALL_NUMBER)
    {
        return MacroHeight;
    }

    const float LoweringCap = FMath::Min(
        MacroHeight - (SeaLevel + CoastMinLandAboveSea),
        CoastMaxLoweringVoxels * LoweringBudgetScale);
    if (LoweringCap <= KINDA_SMALL_NUMBER)
    {
        return MacroHeight;
    }

    const float Lowering = FMath::Min(
        TargetLowering * (CoastRampStrength * (0.75f + 0.25f * DistanceWeight)),
        LoweringCap * DistanceWeight);
    if (Lowering <= KINDA_SMALL_NUMBER)
    {
        return MacroHeight;
    }

    return FMath::Max(
        SeaLevel + CoastMinLandAboveSea,
        MacroHeight - Lowering);
}

void FWorldForgeTerrainMath::LogCoastDiagnostics(
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (!Snapshot.IsValid())
    {
        return;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[CoastFix] Active ShoreFeatherTiles=%.2f MaxLoweringVoxels=%.2f MinLandAboveSea=%.2f"),
        CoastShoreFeatherTiles,
        CoastMaxLoweringVoxels,
        CoastMinLandAboveSea);

    const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;
    const int32 SampleStepX = FMath::Max(1, Snapshot.GridWidth / 24);
    const int32 SampleStepY = FMath::Max(1, Snapshot.GridHeight / 24);

    int32 CoastCellsAffected = 0;
    float CoastDeltaMin = 0.0f;
    float CoastDeltaMax = 0.0f;
    FIntPoint MaxDeltaCell = FIntPoint::ZeroValue;
    bool bHasDeltaSamples = false;

    for (int32 CellY = 0; CellY < Snapshot.GridHeight; CellY += SampleStepY)
    {
        for (int32 CellX = 0; CellX < Snapshot.GridWidth; CellX += SampleStepX)
        {
            const float VoxelX = (static_cast<float>(CellX) + 0.5f - HalfGridX) * ChunkSize;
            const float VoxelY = (static_cast<float>(CellY) + 0.5f - HalfGridY) * ChunkSize;
            const float MacroHeight = ComputeSurfaceHeight(VoxelX, VoxelY, Snapshot);
            const float ShapedHeight = ApplyCoastalShorelineShaping(VoxelX, VoxelY, MacroHeight, Snapshot);
            const float Delta = MacroHeight - ShapedHeight;

            if (Delta <= CoastDeltaLogThreshold)
            {
                continue;
            }

            ++CoastCellsAffected;

            if (!bHasDeltaSamples)
            {
                CoastDeltaMin = Delta;
                CoastDeltaMax = Delta;
                MaxDeltaCell = FIntPoint(CellX, CellY);
                bHasDeltaSamples = true;
            }
            else
            {
                CoastDeltaMin = FMath::Min(CoastDeltaMin, Delta);
                if (Delta > CoastDeltaMax)
                {
                    CoastDeltaMax = Delta;
                    MaxDeltaCell = FIntPoint(CellX, CellY);
                }
            }
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[CoastFix] Sample CoastDeltaMin=%.2f CoastDeltaMax=%.2f CoastCellsAffected=%d"),
        bHasDeltaSamples ? CoastDeltaMin : 0.0f,
        bHasDeltaSamples ? CoastDeltaMax : 0.0f,
        CoastCellsAffected);

    if (bHasDeltaSamples && CoastDeltaMax > CoastLargeDeltaWarningVoxels)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[CoastFix] WARNING: Large shoreline height delta detected. MaxDelta=%.2f Cell=(%d,%d)"),
            CoastDeltaMax,
            MaxDeltaCell.X,
            MaxDeltaCell.Y);
    }
}

// ─── Mountain ridge detail layer (v2.1) ─────────────────────────
//
// Iterates all PipelineMountainRanges from Step03 and computes
// additive voxel displacement.  Each range is a line segment in
// tile-space (Start → Start + Direction * Length).
//
// Produces:
//   • Narrow ridge spines with steep power-curve falloff
//   • High-frequency crag noise for jagged broken profiles
//   • Smooth fade-in/fade-out at range endpoints
//
// This is ADDITIVE — it goes on top of the macro height, never
// replaces it.  It's only called on land voxels.

float FWorldForgeTerrainMath::SampleMountainRidgeHeight(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (Snapshot.PipelineMountainRanges.Num() == 0)
    {
        return 0.0f;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;

    // Convert voxel position to tile-space coordinates
    const float TileX = (VoxelX / ChunkSize) + HalfGridX;
    const float TileY = (VoxelY / ChunkSize) + HalfGridY;
    const FVector2D Point(TileX, TileY);

    // Tuning knobs from snapshot
    const float FalloffPower = FMath::Max(0.5f, Snapshot.MountainFalloffPower);
    const float RidgeHeightScale = FMath::Max(0.0f, Snapshot.MountainRidgeHeightScale);
    const float RidgeWidthScale = FMath::Max(0.1f, Snapshot.MountainRidgeWidthScale);
    const float CragFreq = FMath::Max(0.001f, Snapshot.MountainCragFrequency);
    const float CragAmp = FMath::Max(0.0f, Snapshot.MountainCragAmplitude);

    if (RidgeHeightScale <= KINDA_SMALL_NUMBER)
    {
        return 0.0f;
    }

    const float MinConfiguredWidthTiles = FMath::Max(
        1.0f,
        FMath::Min(Snapshot.MountainMinWidthVoxels, Snapshot.MountainMaxWidthVoxels));
    const float MaxConfiguredWidthTiles = FMath::Max(
        MinConfiguredWidthTiles,
        FMath::Max(Snapshot.MountainMinWidthVoxels, Snapshot.MountainMaxWidthVoxels));

    const float MinConfiguredHeightVoxels = FMath::Max(
        0.0f,
        FMath::Min(Snapshot.MountainMinHeightVoxels, Snapshot.MountainMaxHeightVoxels));
    const float MaxConfiguredHeightVoxels = FMath::Max(
        MinConfiguredHeightVoxels,
        FMath::Max(Snapshot.MountainMinHeightVoxels, Snapshot.MountainMaxHeightVoxels));

    constexpr float ShoulderWidthMultiplier = 2.25f;
    constexpr float FoothillWidthMultiplier = 3.25f;
    constexpr float CentralPeakWidthFraction = 0.42f;
    constexpr float ShoulderHeightFraction = 0.55f;
    constexpr float FoothillHeightFraction = 0.20f;
    constexpr float SpineBoostFraction = 0.20f;
    constexpr float OverlapStackScale = 0.45f;
    constexpr float SafeStackClampMultiplier = 2.0f;
    constexpr float EndpointFadeFraction = 0.08f;

    float StrongestRidgeHeight = 0.0f;
    float StackedRidgeHeight = 0.0f;

    for (const FMountainRange& Range : Snapshot.PipelineMountainRanges)
    {
        // Range spine: Start → Start + Direction * Length (tile coords)
        const FVector2D RangeStart = Range.Start;
        const FVector2D RangeEnd = RangeStart + Range.Direction * FMath::Max(1.0f, Range.Length);

        // Distance from this voxel to the range spine (in tile units)
        float T = 0.0f;
        const float DistTiles = PointToSegmentDistance2D(Point, RangeStart, RangeEnd, T);

        // Width in tile units — Range.Width is already in tile units from Step03.
        // The snapshot width settings bound the primary ridge, while shoulders
        // and foothills extend the massif without turning the whole map into mountains.
        const float EffectiveWidthTiles = FMath::Clamp(
            FMath::Max(1.0f, Range.Width * RidgeWidthScale),
            MinConfiguredWidthTiles,
            MaxConfiguredWidthTiles);

        const float ShoulderWidthTiles = FMath::Max(
            EffectiveWidthTiles,
            EffectiveWidthTiles * ShoulderWidthMultiplier);
        const float FoothillWidthTiles = FMath::Max(
            ShoulderWidthTiles,
            EffectiveWidthTiles * FoothillWidthMultiplier);

        // Skip if outside the range's influence radius
        if (DistTiles >= FoothillWidthTiles)
        {
            continue;
        }

        // ── Radial layers: sharp spine, broad shoulders, and low foothills ──
        const float RidgeNorm = DistTiles / EffectiveWidthTiles;
        const float ShoulderNorm = DistTiles / ShoulderWidthTiles;
        const float FoothillNorm = DistTiles / FoothillWidthTiles;

        const float CentralFalloff = FMath::Pow(
            1.0f - FMath::Clamp(RidgeNorm, 0.0f, 1.0f),
            FalloffPower
        );

        const float CentralPeakWidth = FMath::Max(1.0f, EffectiveWidthTiles * CentralPeakWidthFraction);
        const float SpineNorm = DistTiles / CentralPeakWidth;
        const float SpineBoost = FMath::Pow(
            1.0f - FMath::Clamp(SpineNorm, 0.0f, 1.0f),
            FalloffPower * 1.4f
        );

        const float ShoulderFalloff = FMath::Pow(
            1.0f - FMath::Clamp(ShoulderNorm, 0.0f, 1.0f),
            FMath::Max(0.4f, FalloffPower * 0.65f)
        );

        const float FoothillFalloff = FMath::Pow(
            1.0f - FMath::Clamp(FoothillNorm, 0.0f, 1.0f),
            FMath::Max(0.35f, FalloffPower * 0.45f)
        );

        // ── Endpoint fade: smooth taper at both ends of the range ──
        const float EndFadeIn = FMath::Clamp(T / EndpointFadeFraction, 0.0f, 1.0f);
        const float EndFadeOut = FMath::Clamp((1.0f - T) / EndpointFadeFraction, 0.0f, 1.0f);
        const float LengthMask = WF_Smooth01(FMath::Min(EndFadeIn, EndFadeOut));

        // ── Ridge height from Step03 data ──
        // HeightMultiplier from Step03 is already a meaningful scale factor.
        // RidgeHeightScale lets the player tune overall mountain drama.
        const float RawBaseRidgeVoxels = FMath::Max(0.0f, Range.HeightMultiplier) * RidgeHeightScale;
        if (RawBaseRidgeVoxels <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        const float BaseRidgeVoxels = FMath::Clamp(
            RawBaseRidgeVoxels,
            MinConfiguredHeightVoxels,
            MaxConfiguredHeightVoxels);

        const float MassifShape = FMath::Clamp(
            FMath::Max(CentralFalloff, ShoulderFalloff * ShoulderHeightFraction)
            + FoothillFalloff * FoothillHeightFraction
            + SpineBoost * SpineBoostFraction,
            0.0f,
            1.35f);

        // ── Crag noise: high-frequency jaggedness along the spine ──
        // Only significant near the ridge spine (RadialFalloff already handles this).
        // Use voxel coordinates for sub-tile variation.
        const float RangeNoiseOffset =
            Range.Start.X * 37.0f +
            Range.Start.Y * 57.0f +
            Range.Length * 11.0f +
            Range.Width * 19.0f;
        const float CragNoise = WF_CragNoise2D(
            VoxelX * CragFreq + 317.0f + RangeNoiseOffset,
            VoxelY * CragFreq + 519.0f - RangeNoiseOffset * 0.73f
        );

        // Crag contribution: signed noise scaled by amplitude and spine proximity
        const float CragMask = FMath::Clamp(
            CentralFalloff * 0.85f + ShoulderFalloff * 0.35f,
            0.0f,
            1.0f) * LengthMask;
        const float SafeCragAmp = FMath::Min(CragAmp, MaxConfiguredHeightVoxels * 0.25f);
        const float CragContrib = CragNoise * SafeCragAmp * CragMask;

        // ── Final ridge contribution for this range ──
        const float RidgeContrib = FMath::Max(
            0.0f,
            (BaseRidgeVoxels * MassifShape * LengthMask) + CragContrib);

        StrongestRidgeHeight = FMath::Max(StrongestRidgeHeight, RidgeContrib);
        StackedRidgeHeight += RidgeContrib;
    }

    const float OverlapBonus = FMath::Max(0.0f, StackedRidgeHeight - StrongestRidgeHeight) * OverlapStackScale;
    const float SafeMaxMountainHeight = FMath::Max(
        MaxConfiguredHeightVoxels,
        MinConfiguredHeightVoxels) * SafeStackClampMultiplier;

    return FMath::Clamp(
        StrongestRidgeHeight + OverlapBonus,
        0.0f,
        SafeMaxMountainHeight);
}

// ─── Volcano composite height (v3.0) ────────────────────────────
//
// Each volcano is a cone with a crater carved from the top.
// The tile-space geometry from Step03 is converted to voxel-space
// and sampled at sub-tile resolution.

float FWorldForgeTerrainMath::SampleVolcanoHeight(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (Snapshot.PipelineVolcanoes.Num() == 0)
    {
        return 0.0f;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;

    // Convert voxel position to tile-space
    const float TileX = (VoxelX / ChunkSize) + HalfGridX;
    const float TileY = (VoxelY / ChunkSize) + HalfGridY;

    float MaxContrib = 0.0f;

    for (const FVolcanoData& V : Snapshot.PipelineVolcanoes)
    {
        const float DX = TileX - V.Center.X;
        const float DY = TileY - V.Center.Y;
        const float Dist = FMath::Sqrt(DX * DX + DY * DY);

        if (Dist >= V.ConeRadius)
        {
            continue;
        }

        // Cone: steep radial gradient, peak at center
        const float NormDist = Dist / V.ConeRadius;
        const float ConeElev = FMath::Pow(
            1.0f - FMath::Clamp(NormDist, 0.0f, 1.0f),
            V.ConeSteepness);

        // Crater: inverted bowl subtracted from center
        float CraterCarve = 0.0f;
        const float CraterRadius = V.ConeRadius * V.CraterRadiusFraction;
        if (Dist < CraterRadius && CraterRadius > KINDA_SMALL_NUMBER)
        {
            const float CraterNorm = Dist / CraterRadius;
            CraterCarve = (1.0f - CraterNorm * CraterNorm) * V.CraterDepthFraction;
        }

        // Convert 0-1 contribution to voxel height using MaxLandHeightVoxels
        const float HeightScale = V.ConeHeight *
            static_cast<float>(FMath::Max(1, Snapshot.MaxLandHeightVoxels));
        const float Contribution = (ConeElev - CraterCarve) * HeightScale;

        MaxContrib = FMath::Max(MaxContrib, FMath::Max(0.0f, Contribution));
    }

    return MaxContrib;
}

// ─── Canyon ridge noise (v4.0) ──────────────────────────────────
//
// Domain-warped ridge noise for canyon carving paths.
// Ridge noise = 1.0 - abs(Perlin), which produces narrow bands of
// high values along zero-crossings of the underlying noise.
// Domain warping feeds noise back into the coordinates, making
// the canyon paths meander naturally instead of following straight
// lines.  The result is a 0–1 ridge value.

float FWorldForgeTerrainMath::SampleCanyonRidgeNoise(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    const float RidgeFreq = FMath::Max(0.0001f, Snapshot.CanyonRidgeFrequency);
    const float WarpStrength = FMath::Max(0.0f, Snapshot.CanyonDomainWarpStrength);
    const float WarpFreq = FMath::Max(0.0001f, Snapshot.CanyonDomainWarpFrequency);

    // Domain warp: offset coordinates by a low-frequency noise field
    // to make canyon paths curve and meander.
    const float WarpX = FMath::PerlinNoise2D(FVector2D(
        VoxelX * WarpFreq + 317.0f,
        VoxelY * WarpFreq + 519.0f
    )) * WarpStrength;

    const float WarpY = FMath::PerlinNoise2D(FVector2D(
        VoxelX * WarpFreq + 1013.0f,
        VoxelY * WarpFreq + 2731.0f
    )) * WarpStrength;

    // Sample Perlin at warped coordinates
    const float WarpedX = VoxelX + WarpX;
    const float WarpedY = VoxelY + WarpY;

    const float RawPerlin = FMath::PerlinNoise2D(FVector2D(
        WarpedX * RidgeFreq,
        WarpedY * RidgeFreq
    ));

    // Ridge transform: peaks at zero-crossings of the noise
    const float Ridge = 1.0f - FMath::Abs(RawPerlin);

    return FMath::Clamp(Ridge, 0.0f, 1.0f);
}

// ─── LAKE-FIX-01A: Bilinear WaterDepth sampler ──────────────────
//
// Mirrors SampleTileElevationBilinear but reads Tile.WaterDepth.
// Returns 0.0 on land tiles, ~0.15–0.25 at lake-center tiles.

float FWorldForgeTerrainMath::SampleTileWaterDepthBilinear(
    float VoxelX,
    float VoxelY,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (!Snapshot.IsValid())
    {
        return 0.0f;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;

    const float CellX = (VoxelX / ChunkSize) + HalfGridX;
    const float CellY = (VoxelY / ChunkSize) + HalfGridY;

    const int32 X0 = FMath::FloorToInt(CellX);
    const int32 Y0 = FMath::FloorToInt(CellY);
    const int32 X1 = X0 + 1;
    const int32 Y1 = Y0 + 1;

    const float FX = CellX - static_cast<float>(X0);
    const float FY = CellY - static_cast<float>(Y0);

    auto GetWaterDepth = [&](int32 CX, int32 CY) -> float
    {
        const FIntPoint Cell = Snapshot.ClampCell(FIntPoint(CX, CY));
        const int32 Idx = Snapshot.TileIndex(Cell);
        if (Snapshot.WorldTiles.IsValidIndex(Idx))
        {
            return Snapshot.WorldTiles[Idx].WaterDepth;
        }
        return 0.0f;
    };

    const float W00 = GetWaterDepth(X0, Y0);
    const float W10 = GetWaterDepth(X1, Y0);
    const float W01 = GetWaterDepth(X0, Y1);
    const float W11 = GetWaterDepth(X1, Y1);

    return WF_Bilerp(W00, W10, W01, W11, FX, FY);
}

// ─── LAKE-TERRAIN-VISUAL-01: Basin shaping (elliptical, shallow-safe) ──
//
// Uses accepted lake basin descriptors instead of a hard sea-level target.
// Each basin gets an elliptical core plus an outer shoreline feather ring.
// The shape can both raise an overly deep macro hole and lower an overly high
// floor, keeping the rendered basin within ~0.5–0.7 voxels below the lake
// surface while avoiding square cliffs.

float FWorldForgeTerrainMath::ApplyLakeBasinShaping(
    float VoxelX,
    float VoxelY,
    float MacroHeight,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (Snapshot.PipelineLakeTileCount == 0)
    {
        return MacroHeight;
    }

    const FWorldForgeLakeShapeSample LakeSample = WF_EvaluateLakeShapeSample(
        VoxelX,
        VoxelY,
        Snapshot);

    if (!LakeSample.Basin || LakeSample.Influence <= KINDA_SMALL_NUMBER)
    {
        return MacroHeight;
    }

    return FMath::Lerp(
        MacroHeight,
        LakeSample.DesiredFloorHeight,
        LakeSample.Influence);
}

float FWorldForgeTerrainMath::ApplyRiverChannelShaping(
    float VoxelX,
    float VoxelY,
    float MacroHeight,
    const FWorldForgeTerrainSnapshot& Snapshot)
{
    if (!Snapshot.IsValid() || Snapshot.RiverSurfaceDescCount == 0 || Snapshot.RiverSurfaceDescs.Num() == 0)
    {
        return MacroHeight;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;
    const FVector2D SampleTile(
        (VoxelX / ChunkSize) + HalfGridX,
        (VoxelY / ChunkSize) + HalfGridY);

    const float WaterDepth = SampleTileWaterDepthBilinear(VoxelX, VoxelY, Snapshot);
    const float WaterMask = WF_SmoothStepRange(WaterDepth, 0.02f, 0.08f);

    auto DistanceToSegmentTiles = [](const FVector2D& Point, const FVector2D& A, const FVector2D& B) -> float
    {
        const FVector2D AB = B - A;
        const float LengthSq = FVector2D::DotProduct(AB, AB);
        if (LengthSq <= KINDA_SMALL_NUMBER)
        {
            return FVector2D::Distance(Point, A);
        }

        const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSq, 0.0f, 1.0f);
        return FVector2D::Distance(Point, A + (AB * T));
    };

    float BestInfluence = 0.0f;
    float BestDepthVoxels = 0.0f;

    for (const FWorldForgeRiverSurfaceDesc& Desc : Snapshot.RiverSurfaceDescs)
    {
        if (Desc.TilePath.Num() <= 0)
        {
            continue;
        }

        const float WidthRadiusTiles = FMath::Max(0.45f, Desc.AverageWidth * 0.5f);
        const float OuterRadiusTiles = WidthRadiusTiles + FMath::Max(0.5f, Desc.BankFalloffTiles);

        if (SampleTile.X < (static_cast<float>(Desc.MinTile.X) - OuterRadiusTiles) ||
            SampleTile.X > (static_cast<float>(Desc.MaxTile.X + 1) + OuterRadiusTiles) ||
            SampleTile.Y < (static_cast<float>(Desc.MinTile.Y) - OuterRadiusTiles) ||
            SampleTile.Y > (static_cast<float>(Desc.MaxTile.Y + 1) + OuterRadiusTiles))
        {
            continue;
        }

        float ClosestDistanceTiles = BIG_NUMBER;
        if (Desc.TilePath.Num() == 1)
        {
            const FVector2D TileCenter(
                static_cast<float>(Desc.TilePath[0].X) + 0.5f,
                static_cast<float>(Desc.TilePath[0].Y) + 0.5f);
            ClosestDistanceTiles = FVector2D::Distance(SampleTile, TileCenter);
        }
        else
        {
            for (int32 PathIndex = 1; PathIndex < Desc.TilePath.Num(); ++PathIndex)
            {
                const FVector2D A(
                    static_cast<float>(Desc.TilePath[PathIndex - 1].X) + 0.5f,
                    static_cast<float>(Desc.TilePath[PathIndex - 1].Y) + 0.5f);
                const FVector2D B(
                    static_cast<float>(Desc.TilePath[PathIndex].X) + 0.5f,
                    static_cast<float>(Desc.TilePath[PathIndex].Y) + 0.5f);
                ClosestDistanceTiles = FMath::Min(
                    ClosestDistanceTiles,
                    DistanceToSegmentTiles(SampleTile, A, B));
            }
        }

        if (ClosestDistanceTiles > OuterRadiusTiles)
        {
            continue;
        }

        const float DistanceDenom = FMath::Max(KINDA_SMALL_NUMBER, OuterRadiusTiles - WidthRadiusTiles);
        const float DistanceMask = 1.0f - FMath::Clamp((ClosestDistanceTiles - WidthRadiusTiles) / DistanceDenom, 0.0f, 1.0f);
        const float Influence = FMath::Max(WaterMask, DistanceMask);
        if (Influence > BestInfluence)
        {
            BestInfluence = Influence;
            BestDepthVoxels = FMath::Max(0.5f, Desc.ChannelDepthVoxels);
        }
    }

    if (BestInfluence <= KINDA_SMALL_NUMBER)
    {
        return MacroHeight;
    }

    const float SeaLevel = static_cast<float>(Snapshot.SeaLevelVoxels);
    const float LoweredHeight = FMath::Max(SeaLevel, MacroHeight - (BestDepthVoxels * BestInfluence));
    return FMath::Min(MacroHeight, LoweredHeight);
}
