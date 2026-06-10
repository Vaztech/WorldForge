// ════════════════════════════════════════════════════════════════════
// WorldForgeTerrainGenerator.cpp — v5.0
//
// 3D SDF LAYERED DENSITY COMPOSITOR + HYBRID CHUNKING MODIFICATION PATH
//
// The voxel generator produces a true 3D density field via layered
// composition.  Each layer is independent and additive:
//
//   Layer 1: Base terrain (2D heightmap → density gradient) [existing]
//   Layer 2: Worm cave networks (3D dual-noise intersection)
//   Layer 3: Canyon carving (domain-warped ridge noise)
//   Layer 4: Lava tube sub-layer (horizontal-biased worm caves)
//   Layer 5: Biome density bias
//   Foundation: Solid floor below all terrain [existing]
//
// v5.0 MODIFICATION PATH:
//   After all base layers, the modification map is sampled.
//   Modifications are SDF brush volumes (sphere, cylinder, box)
//   indexed by chunk coordinate.  Only chunks with active digs
//   are evaluated — O(M) per modified chunk, O(0) for pristine
//   chunks.  Replaces the v4.0 O(N) flat RemovedTerrainBlocks scan.
//
// Positive density = air.  Negative density = solid.
// Voxel Plugin meshes the isosurface at density = 0.
// ════════════════════════════════════════════════════════════════════

#include "WorldForgeTerrainGenerator.h"

#include "VoxelMaterialBuilder.h"
#include "Utility/WorldForgeTerrainSettings.h"
#include "Utility/WorldForgeTerrainMath.h"

namespace
{
    constexpr float WF_LakeBasinCoreScale = 0.65f;
    constexpr float WF_LakeBasinEdgeBlendScale = 1.25f;
    constexpr float WF_LakeMinShorelineFeatherTiles = 2.0f;
    constexpr float WF_LakeMaxShorelineFeatherTiles = 3.0f;
    constexpr float WF_LakeMinLoweringVoxels = 0.50f;
    constexpr float WF_LakeMaxLoweringVoxels = 0.70f;
    constexpr float WF_LakeSteepEdgeWarningThreshold = 0.40f;
    constexpr float WF_CoastMaterialReachTiles = 1.85f;
    constexpr float WF_CoastSandSlopeMaxDegrees = 14.0f;

    static float WF_FractalPerlin2D(float X, float Y, float Frequency, int32 Octaves)
    {
        float Total = 0.0f;
        float Amplitude = 1.0f;
        float SumAmplitude = 0.0f;
        float CurrentFrequency = FMath::Max(KINDA_SMALL_NUMBER, Frequency);

        for (int32 OctaveIndex = 0; OctaveIndex < FMath::Max(1, Octaves); ++OctaveIndex)
        {
            const float Sample = FMath::PerlinNoise2D(FVector2D(X * CurrentFrequency, Y * CurrentFrequency));
            Total += Sample * Amplitude;
            SumAmplitude += Amplitude;

            CurrentFrequency *= 2.0f;
            Amplitude *= 0.5f;
        }

        if (SumAmplitude <= KINDA_SMALL_NUMBER)
        {
            return 0.0f;
        }

        return Total / SumAmplitude;
    }

    static float WF_Perlin3D(float X, float Y, float Z)
    {
        return FMath::PerlinNoise3D(FVector(X, Y, Z));
    }

    static float WF_ComputeLakeFloorLoweringVoxels(const FWorldForgeTerrainSnapshot& Snapshot)
    {
        return FMath::Clamp(
            static_cast<float>(Snapshot.LakeBasinDepthVoxels) * 0.05f,
            WF_LakeMinLoweringVoxels,
            WF_LakeMaxLoweringVoxels);
    }

    static void WF_LogLakeTerrainShapeDiagnostics(const FWorldForgeTerrainSnapshot& Snapshot)
    {
        if (!Snapshot.IsValid() || Snapshot.PipelineLakeBasins.Num() == 0)
        {
            return;
        }

        const float ChunkSizeF = static_cast<float>(FMath::Max(1, Snapshot.ChunkSizeVoxels));
        const float HalfGridX = static_cast<float>(Snapshot.GridWidth) * 0.5f;
        const float HalfGridY = static_cast<float>(Snapshot.GridHeight) * 0.5f;
        const float MaxLoweringVoxels = WF_ComputeLakeFloorLoweringVoxels(Snapshot);

        for (const FLakeBasinData& Basin : Snapshot.PipelineLakeBasins)
        {
            if (Basin.TileCount <= 0)
            {
                continue;
            }

            const float CenterX = (static_cast<float>(Basin.CenterTileX) + 0.5f - HalfGridX) * ChunkSizeF;
            const float CenterY = (static_cast<float>(Basin.CenterTileY) + 0.5f - HalfGridY) * ChunkSizeF;
            const float WaterSurfaceHeight = FWorldForgeTerrainMath::ElevationToVoxelHeight(
                Basin.WaterSurfaceElevation01,
                Snapshot);
            const float BasinFloorHeight = FWorldForgeTerrainMath::ElevationToVoxelHeight(
                Basin.FloorElevation01,
                Snapshot);
            const float LoweringVoxels = FMath::Clamp(
                WaterSurfaceHeight - BasinFloorHeight,
                WF_LakeMinLoweringVoxels,
                MaxLoweringVoxels);
            const float ShorelineFeatherTiles = FMath::Clamp(
                Basin.ShoreFalloffTiles > KINDA_SMALL_NUMBER
                    ? Basin.ShoreFalloffTiles
                    : 2.5f,
                WF_LakeMinShorelineFeatherTiles,
                WF_LakeMaxShorelineFeatherTiles);
            const float MaxEdgeDelta = LoweringVoxels / FMath::Max(1.0f, ShorelineFeatherTiles);
            const int32 ChunkSpanX = FMath::Max(1, Basin.MaxTileX - Basin.MinTileX + 1);
            const int32 ChunkSpanY = FMath::Max(1, Basin.MaxTileY - Basin.MinTileY + 1);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[LakeFix] TerrainShape Basin=%d Tiles=%d Center=(%.0f,%.0f) SurfaceZ=%.1f LoweringVoxels=%.2f FeatherTiles=%.1f"),
                Basin.BasinID,
                Basin.TileCount,
                CenterX,
                CenterY,
                WaterSurfaceHeight,
                LoweringVoxels,
                ShorelineFeatherTiles);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[LakeFix] TerrainShapeSeams Basin=%d ChunkSpan=(%d,%d) TileBounds=[%d..%d,%d..%d] CoreScale=%.2f EdgeBlendScale=%.2f"),
                Basin.BasinID,
                ChunkSpanX,
                ChunkSpanY,
                Basin.MinTileX,
                Basin.MaxTileX,
                Basin.MinTileY,
                Basin.MaxTileY,
                WF_LakeBasinCoreScale,
                WF_LakeBasinEdgeBlendScale);

            if (MaxEdgeDelta > WF_LakeSteepEdgeWarningThreshold)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[LakeFix] WARNING: Lake basin may be too steep. Basin=%d MaxEdgeDelta=%.2f"),
                    Basin.BasinID,
                    MaxEdgeDelta);
            }
        }
    }
}

TVoxelSharedRef<FVoxelGeneratorInstance> UWorldForgeTerrainGenerator::GetInstance()
{
    return MakeVoxelShared<FWorldForgeTerrainGeneratorInstance>(*this);
}

FWorldForgeTerrainGeneratorInstance::FWorldForgeTerrainGeneratorInstance(UWorldForgeTerrainGenerator& GeneratorObject)
    : Super(&GeneratorObject)
    , TerrainSnapshot(GeneratorObject.TerrainSnapshot)
    , Seed(GeneratorObject.Seed)
{
}

void FWorldForgeTerrainGeneratorInstance::Init(const FVoxelGeneratorInit& InitStruct)
{
    Noise2D.SetSeed(Seed);
    CaveNoise.SetSeed(Seed + 5000);       // worm field A
    CaveNoiseB.SetSeed(Seed + 9000);      // worm field B
    CanyonNoise.SetSeed(Seed + 13000);    // canyon domain warp
    LavaTubeNoise.SetSeed(Seed + 17000);  // lava tube field A
    LavaTubeNoiseB.SetSeed(Seed + 21000); // lava tube field B

    // v6.0: Resource vein noise — offsets from FResourceNoiseRegistry
    ResourceNoiseMetal.SetSeed(Seed + 25000);
    ResourceNoiseCrystal.SetSeed(Seed + 29000);
    ResourceNoiseFossil.SetSeed(Seed + 33000);

    if (TerrainSnapshot.IsValid())
    {
        const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
        const float HalfGridY = static_cast<float>(TerrainSnapshot.GridHeight) * 0.5f;
        const float ChunkSizeF = static_cast<float>(TerrainSnapshot.ChunkSizeVoxels);
        const float MinVoxel = -HalfGridX * ChunkSizeF;
        const float MaxVoxel = HalfGridX * ChunkSizeF;

        const int32 TileCount = TerrainSnapshot.WorldTiles.Num();
        const float WaterThreshold = FMath::Clamp(TerrainSnapshot.WaterThreshold, 0.0f, 1.0f);
        const float SeaLevel = static_cast<float>(TerrainSnapshot.SeaLevelVoxels);

        bool bHasElevationStats = false;
        float ElevationMin = 0.0f;
        float ElevationMax = 0.0f;
        int32 WaterTileCount = 0;
        int32 LandTileCount = 0;

        for (int32 TileIndex = 0; TileIndex < TileCount; ++TileIndex)
        {
            const FIntPoint Cell(
                TerrainSnapshot.GridWidth > 0 ? TileIndex % TerrainSnapshot.GridWidth : 0,
                TerrainSnapshot.GridWidth > 0 ? TileIndex / TerrainSnapshot.GridWidth : 0);
            const float Elevation = TerrainSnapshot.GetTileElevation(Cell, 0.0f);

            if (!bHasElevationStats)
            {
                ElevationMin = Elevation;
                ElevationMax = Elevation;
                bHasElevationStats = true;
            }
            else
            {
                ElevationMin = FMath::Min(ElevationMin, Elevation);
                ElevationMax = FMath::Max(ElevationMax, Elevation);
            }

            if (Elevation <= WaterThreshold)
            {
                ++WaterTileCount;
            }
            else
            {
                ++LandTileCount;
            }
        }

        bool bHasSurfaceStats = false;
        float SurfaceMin = 0.0f;
        float SurfaceMax = 0.0f;
        float RidgeMax = 0.0f;
        int32 SurfaceSamples = 0;

        const int32 SampleStepX = FMath::Max(1, TerrainSnapshot.GridWidth / 16);
        const int32 SampleStepY = FMath::Max(1, TerrainSnapshot.GridHeight / 16);

        for (int32 CellY = 0; CellY < TerrainSnapshot.GridHeight; CellY += SampleStepY)
        {
            for (int32 CellX = 0; CellX < TerrainSnapshot.GridWidth; CellX += SampleStepX)
            {
                const float VoxelX = (static_cast<float>(CellX) + 0.5f - HalfGridX) * ChunkSizeF;
                const float VoxelY = (static_cast<float>(CellY) + 0.5f - HalfGridY) * ChunkSizeF;
                const float SurfaceHeight = ComputeSurfaceHeight(VoxelX, VoxelY);

                if (!bHasSurfaceStats)
                {
                    SurfaceMin = SurfaceHeight;
                    SurfaceMax = SurfaceHeight;
                    bHasSurfaceStats = true;
                }
                else
                {
                    SurfaceMin = FMath::Min(SurfaceMin, SurfaceHeight);
                    SurfaceMax = FMath::Max(SurfaceMax, SurfaceHeight);
                }

                const float MacroHeight = FWorldForgeTerrainMath::ComputeSurfaceHeight(VoxelX, VoxelY, TerrainSnapshot);
                if (MacroHeight > SeaLevel)
                {
                    RidgeMax = FMath::Max(
                        RidgeMax,
                        FWorldForgeTerrainMath::SampleMountainRidgeHeight(VoxelX, VoxelY, TerrainSnapshot));
                }

                ++SurfaceSamples;
            }
        }

        UE_LOG(LogTemp, Log,
            TEXT("[TerrainGen] Init v5.0: Seed=%d Grid=%dx%d ChunkSizeVoxels=%d VoxelSize=%.1f "
                "Expected voxel range=[%.0f..%.0f] (total=%.0f) MountainRanges=%d Volcanoes=%d DirtyChunks=%d"),
            Seed,
            TerrainSnapshot.GridWidth, TerrainSnapshot.GridHeight,
            TerrainSnapshot.ChunkSizeVoxels,
            TerrainSnapshot.VoxelSize,
            MinVoxel, MaxVoxel, MaxVoxel - MinVoxel,
            TerrainSnapshot.PipelineMountainRanges.Num(),
            TerrainSnapshot.PipelineVolcanoes.Num(),
            TerrainSnapshot.Modifications.NumDirtyChunks());

        UE_LOG(LogTemp, Log,
            TEXT("[TerrainGen] Snapshot Grid=%dx%d Tiles=%d MountainRanges=%d Volcanoes=%d ElevMin=%.3f ElevMax=%.3f WaterTiles=%d LandTiles=%d"),
            TerrainSnapshot.GridWidth,
            TerrainSnapshot.GridHeight,
            TileCount,
            TerrainSnapshot.PipelineMountainRanges.Num(),
            TerrainSnapshot.PipelineVolcanoes.Num(),
            bHasElevationStats ? ElevationMin : 0.0f,
            bHasElevationStats ? ElevationMax : 0.0f,
            WaterTileCount,
            LandTileCount);

        UE_LOG(LogTemp, Log,
            TEXT("[TerrainGen] HeightStats SurfaceMin=%.1f SurfaceMax=%.1f RidgeMax=%.1f Samples=%d"),
            bHasSurfaceStats ? SurfaceMin : 0.0f,
            bHasSurfaceStats ? SurfaceMax : 0.0f,
            RidgeMax,
            SurfaceSamples);

        UE_LOG(LogTemp, Log,
            TEXT("[TerrainMaterial] CoastBand=%.2f SandMax=%.3f DirtMin=%.3f SlopeThresholds=(Dirt=%.1f Rock=%.1f Cliff=%.1f)"),
            TerrainSnapshot.CoastBandVoxels,
            TerrainSnapshot.SandMaxElevationNormalized,
            TerrainSnapshot.DirtMinElevationNormalized,
            TerrainSnapshot.DirtSlopeThresholdDegrees,
            TerrainSnapshot.RockSlopeThresholdDegrees,
            TerrainSnapshot.CliffSlopeThresholdDegrees);

        FWorldForgeTerrainMath::LogCoastDiagnostics(TerrainSnapshot);

        {
            const float RepairMaxLoweringVoxels = WF_ComputeLakeFloorLoweringVoxels(TerrainSnapshot);
            UE_LOG(LogTemp, Log,
                TEXT("[LakeFix] Repair shaping active. Basins=%d LakeTiles=%d MaxLoweringVoxels=%.2f"),
                TerrainSnapshot.PipelineLakeBasins.Num(),
                TerrainSnapshot.PipelineLakeTileCount,
                RepairMaxLoweringVoxels);
            UE_LOG(LogTemp, Log,
                TEXT("[LakeFix] Shallow lake terrain shaping active; Waterline surfaces are handled by WorldGenBootstrapActor."));
        }

        WF_LogLakeTerrainShapeDiagnostics(TerrainSnapshot);

        if (TerrainSnapshot.PipelineMountainRanges.Num() == 0)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[TerrainGen] MountainRanges == 0; 3D terrain will not receive Step03 mountain ridge displacement."));
        }

        if (bHasElevationStats && FMath::Abs(ElevationMax - ElevationMin) <= 0.001f)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[TerrainGen] Tile elevations are nearly identical. ElevMin=%.3f ElevMax=%.3f"),
                ElevationMin,
                ElevationMax);
        }

        const float LowReliefThreshold = SeaLevel + FMath::Max(
            TerrainSnapshot.CoastBandVoxels + 4.0f,
            static_cast<float>(FMath::Max(1, TerrainSnapshot.MaxLandHeightVoxels)) * 0.08f);
        if (bHasSurfaceStats && SurfaceMax <= LowReliefThreshold)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[TerrainGen] SurfaceMax is only slightly above sea level. SurfaceMax=%.1f SeaLevel=%.1f Threshold=%.1f"),
                SurfaceMax,
                SeaLevel,
                LowReliefThreshold);
        }

        const float RidgeNearZeroThreshold = FMath::Max(0.5f, TerrainSnapshot.MountainMinHeightVoxels * 0.05f);
        if (TerrainSnapshot.PipelineMountainRanges.Num() > 0 && RidgeMax <= RidgeNearZeroThreshold)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[TerrainGen] RidgeMax is near zero despite MountainRanges=%d. RidgeMax=%.2f Threshold=%.2f"),
                TerrainSnapshot.PipelineMountainRanges.Num(),
                RidgeMax,
                RidgeNearZeroThreshold);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// DENSITY FIELD — v5.0 Layered 3D SDF Compositor + Hybrid Modifications
// ═══════════════════════════════════════════════════════════════════

v_flt FWorldForgeTerrainGeneratorInstance::GetValueImpl(
    v_flt X,
    v_flt Y,
    v_flt Z,
    int32 LOD,
    const FVoxelItemStack& Items) const
{
    if (!IsSnapshotValid())
    {
        return 1.0f;
    }

    // ── Layer 1: Base terrain (2D heightmap → density gradient) ──
    const float SurfaceHeight = ComputeSurfaceHeight(X, Y);
    float Density = Z - SurfaceHeight;

    // Biome profile for this column
    const FVoxelBiomeProfile* Profile = SamplePrimaryBiomeProfile(X, Y);

    if (Profile)
    {
        // ── Layer 2: Worm cave networks ──
        if (Profile->bEnableCaves && Profile->CaveIntensity > 0.0f)
        {
            Density += ComputeWormCaveDensity(X, Y, Z, SurfaceHeight, *Profile);
        }

        // ── Layer 3: Canyon carving ──
        if (Profile->CanyonIntensity > 0.0f)
        {
            Density += ComputeCanyonCarve(X, Y, Z, SurfaceHeight, *Profile);
        }

        // ── Layer 4: Lava tube sub-layer (inside volcano cones) ──
        if (TerrainSnapshot.bEnableLavaTubes)
        {
            Density += ComputeLavaTubeDensity(X, Y, Z, SurfaceHeight);
        }

        // ── Layer 5: Biome density bias ──
        Density += Profile->DensityBias;
    }

    // ── Foundation: solid floor below all terrain (unchanged) ──
    if (TerrainSnapshot.bAddGlobalFoundation)
    {
        const float FoundationDepth = static_cast<float>(TerrainSnapshot.GetFoundationDepthVoxels());
        const float DistanceToFoundationTop = Z;
        const float DistanceToFoundationBottom = -(Z + FoundationDepth);
        const float FoundationDensity = FMath::Max(DistanceToFoundationTop, DistanceToFoundationBottom);
        Density = FMath::Min(Density, FoundationDensity);
    }

    // ═══════════════════════════════════════════════════════════════
    // v5.0 HYBRID MODIFICATION PATH
    // ═══════════════════════════════════════════════════════════════
    // Sample the chunk-indexed modification map.  This replaces the
    // old O(N) RemovedTerrainBlocks scan with an O(1) chunk lookup
    // + O(M) brush evaluation where M = brushes in this chunk.
    //
    // Composition formula:
    //   FinalDensity = BaseDensity + ModificationCarve
    //
    // ModificationCarve is positive where brushes carve (push
    // density toward air), so adding it to a negative base density
    // (solid) can flip it positive (air) — creating the void.
    // ═══════════════════════════════════════════════════════════════

    const float ModificationCarve = TerrainSnapshot.SampleModificationDensity(X, Y, Z);

    if (ModificationCarve != 0.0f)
    {
        Density += ModificationCarve;
    }

    return Density;
}

// ═══════════════════════════════════════════════════════════════════
// MATERIAL ASSIGNMENT — v4.0 Depth-Stratified Underground
// ═══════════════════════════════════════════════════════════════════

FVoxelMaterial FWorldForgeTerrainGeneratorInstance::GetMaterialImpl(
    v_flt X,
    v_flt Y,
    v_flt Z,
    int32 LOD,
    const FVoxelItemStack& Items) const
{
    if (!IsSnapshotValid())
    {
        return FVoxelMaterial::Default();
    }

    const float SurfaceHeight = ComputeSurfaceHeight(X, Y);
    const float DepthBelowSurface = SurfaceHeight - Z;

    // At or above surface — use the surface classification
    if (DepthBelowSurface <= 1.5f)
    {
        const int32 SurfaceLayer = ClassifyTerrainMaterialLayer(X, Y, SurfaceHeight);
        return MakeVoxelMaterialFromLayer(SurfaceLayer);
    }

    // Underground — use depth-stratified material
    return MakeVoxelMaterialFromLayer(ClassifyUndergroundMaterial(X, Y, Z, SurfaceHeight));
}

// ─── Value range (LOD culling) — v5.0 chunk-indexed ─────────────

TVoxelRange<v_flt> FWorldForgeTerrainGeneratorInstance::GetValueRangeImpl(
    const FVoxelIntBox& Bounds,
    int32 LOD,
    const FVoxelItemStack& Items) const
{
    if (!IsSnapshotValid())
    {
        return TVoxelRange<v_flt>(1.0f, 1.0f);
    }

    const float S00 = ComputeSurfaceHeight(Bounds.Min.X, Bounds.Min.Y);
    const float S01 = ComputeSurfaceHeight(Bounds.Min.X, Bounds.Max.Y);
    const float S10 = ComputeSurfaceHeight(Bounds.Max.X, Bounds.Min.Y);
    const float S11 = ComputeSurfaceHeight(Bounds.Max.X, Bounds.Max.Y);

    const float MinSurface = FMath::Min(FMath::Min(S00, S01), FMath::Min(S10, S11));
    const float MaxSurface = FMath::Max(FMath::Max(S00, S01), FMath::Max(S10, S11));

    float MinValue = static_cast<float>(Bounds.Min.Z) - MaxSurface;
    float MaxValue = static_cast<float>(Bounds.Max.Z) - MinSurface;

    // v4.0: Cave/Canyon overlap check
    if (TerrainSnapshot.bEnableCaves)
    {
        const float MaxCaveDepth = FMath::Max(
            TerrainSnapshot.CaveDepthFadeEnd,
            TerrainSnapshot.CanyonMaxDepthVoxels);

        if (static_cast<float>(Bounds.Max.Z) > (MinSurface - MaxCaveDepth) &&
            static_cast<float>(Bounds.Min.Z) < MaxSurface)
        {
            return TVoxelRange<v_flt>(
                FMath::Min(MinValue, -100.0f),
                FMath::Max(MaxValue, 100.0f));
        }
    }

    // Foundation bounds
    if (TerrainSnapshot.bAddGlobalFoundation)
    {
        const float FoundationDepth = static_cast<float>(TerrainSnapshot.GetFoundationDepthVoxels());
        const float FoundationAtMinZ = FMath::Max(
            static_cast<float>(Bounds.Min.Z),
            -(static_cast<float>(Bounds.Min.Z) + FoundationDepth));
        const float FoundationAtMaxZ = FMath::Max(
            static_cast<float>(Bounds.Max.Z),
            -(static_cast<float>(Bounds.Max.Z) + FoundationDepth));
        const float FoundationMin = FMath::Min(FoundationAtMinZ, FoundationAtMaxZ);
        MinValue = FMath::Min(MinValue, FoundationMin);
    }

    // ── v5.0: Chunk-indexed modification bounds check ──
    // Instead of iterating every removed block O(N), check if any
    // modified chunks overlap this query region.  O(C) where C is
    // the number of chunks the bounds span (typically 1–4).
    if (TerrainSnapshot.HasModificationsInBounds(
        static_cast<float>(Bounds.Min.X),
        static_cast<float>(Bounds.Min.Y),
        static_cast<float>(Bounds.Max.X),
        static_cast<float>(Bounds.Max.Y)))
    {
        return TVoxelRange<v_flt>(
            FMath::Min(MinValue, -10.0f),
            FMath::Max(MaxValue, 10.0f)
        );
    }

    return TVoxelRange<v_flt>(MinValue, MaxValue);
}

FVector FWorldForgeTerrainGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
    return FVector::UpVector;
}

// ═══════════════════════════════════════════════════════════════════
// SURFACE HEIGHT (macro height + coast + lake shaping)
// ═══════════════════════════════════════════════════════════════════

float FWorldForgeTerrainGeneratorInstance::ComputeSurfaceHeight(float VoxelX, float VoxelY) const
{
    // Step 1: Get bilinearly interpolated macro height from tile elevation.
    const float MacroHeight = FWorldForgeTerrainMath::ComputeSurfaceHeight(
        VoxelX, VoxelY, TerrainSnapshot);

    // Step 2: Feather ordinary coast and inlet land down toward sea level.
    const float CoastalHeight = FWorldForgeTerrainMath::ApplyCoastalShorelineShaping(
        VoxelX, VoxelY, MacroHeight, TerrainSnapshot);

    // Step 3: Shape accepted lake basins with a shallow elliptical profile.
    float BasinHeight = CoastalHeight;
    const bool bHasLakes = TerrainSnapshot.PipelineLakeTileCount > 0;
    if (bHasLakes)
    {
        BasinHeight = FWorldForgeTerrainMath::ApplyLakeBasinShaping(
            VoxelX, VoxelY, CoastalHeight, TerrainSnapshot);
    }

    // Step 4: Apply shallow river channel shaping from lightweight descriptors.
    float RiverHeight = BasinHeight;
    const bool bHasRivers = TerrainSnapshot.RiverSurfaceDescCount > 0;
    if (bHasRivers)
    {
        RiverHeight = FWorldForgeTerrainMath::ApplyRiverChannelShaping(
            VoxelX, VoxelY, BasinHeight, TerrainSnapshot);
    }

    // Step 5: Add mountain ridge displacement + volcano height from Step03.
    // Suppress these layers anywhere the lake or river shaping path changed the column.
    float RidgeHeight = 0.0f;
    float VolcanoHeight = 0.0f;
    const float SeaLevel = static_cast<float>(TerrainSnapshot.SeaLevelVoxels);
    const bool bInsideLakeBasin = bHasLakes && !FMath::IsNearlyEqual(BasinHeight, CoastalHeight, 0.01f);
    const bool bInsideRiverChannel = bHasRivers && !FMath::IsNearlyEqual(RiverHeight, BasinHeight, 0.01f);
    if (!bInsideLakeBasin && !bInsideRiverChannel && RiverHeight > SeaLevel)
    {
        RidgeHeight = FWorldForgeTerrainMath::SampleMountainRidgeHeight(
            VoxelX, VoxelY, TerrainSnapshot);

        VolcanoHeight = FWorldForgeTerrainMath::SampleVolcanoHeight(
            VoxelX, VoxelY, TerrainSnapshot);
    }

    // Step 6: Add small biome-dependent detail noise.
    float DetailHeight = 0.0f;
    if (const FVoxelBiomeProfile* Profile = SamplePrimaryBiomeProfile(VoxelX, VoxelY))
    {
        DetailHeight = ComputeDetailHeight(VoxelX, VoxelY, *Profile);
    }
    DetailHeight = FMath::Clamp(DetailHeight, -24.0f, 24.0f);

    return RiverHeight + RidgeHeight + VolcanoHeight + DetailHeight;
}

// ─── Tile elevation sampler (delegates to TerrainMath) ──────────

float FWorldForgeTerrainGeneratorInstance::SampleTileElevationBilinear(float VoxelX, float VoxelY) const
{
    return FWorldForgeTerrainMath::SampleTileElevationBilinear(VoxelX, VoxelY, TerrainSnapshot);
}

// ─── Biome profile lookup ───────────────────────────────────────

const FVoxelBiomeProfile* FWorldForgeTerrainGeneratorInstance::SamplePrimaryBiomeProfile(float VoxelX, float VoxelY) const
{
    if (!IsSnapshotValid())
    {
        return nullptr;
    }

    const float ChunkSizeF = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(TerrainSnapshot.GridHeight) * 0.5f;

    const int32 ChunkX = FMath::FloorToInt((VoxelX / ChunkSizeF) + HalfGridX);
    const int32 ChunkY = FMath::FloorToInt((VoxelY / ChunkSizeF) + HalfGridY);

    const FIntPoint ChunkCell = TerrainSnapshot.ClampCell(FIntPoint(ChunkX, ChunkY));
    return TerrainSnapshot.GetVoxelProfile(ChunkCell);
}

// ─── Detail noise (unchanged) ───────────────────────────────────

float FWorldForgeTerrainGeneratorInstance::ComputeDetailHeight(float VoxelX, float VoxelY, const FVoxelBiomeProfile& Profile) const
{
    const float SafeVoxelSize = FMath::Max(KINDA_SMALL_NUMBER, TerrainSnapshot.VoxelSize);

    const float RawAmplitudeVoxels = Profile.NoiseAmplitude / SafeVoxelSize;
    const float RoughnessScale = FMath::Max(0.0f, Profile.SurfaceRoughnessScale);
    const float DetailAmplitudeVoxels = FMath::Clamp(
        RawAmplitudeVoxels * TerrainSnapshot.DetailAmplitude * 0.01f * RoughnessScale,
        0.0f,
        48.0f
    );

    const float DetailFrequency = FMath::Max(0.0001f, TerrainSnapshot.DetailScale);
    const int32 DetailOctaves = FMath::Clamp(TerrainSnapshot.DetailOctaves, 1, 8);

    const float NoiseValue = WF_FractalPerlin2D(
        VoxelX + static_cast<float>(Seed),
        VoxelY + static_cast<float>(Seed * 2),
        DetailFrequency,
        DetailOctaves
    );

    return NoiseValue * DetailAmplitudeVoxels;
}

// ═══════════════════════════════════════════════════════════════════
// LAYER 2: WORM CAVE NETWORKS (v4.0)
// ═══════════════════════════════════════════════════════════════════

float FWorldForgeTerrainGeneratorInstance::ComputeWormCaveDensity(
    float X, float Y, float Z,
    float SurfaceHeight, const FVoxelBiomeProfile& Profile) const
{
    const float DepthBelowSurface = SurfaceHeight - Z;

    const float MinDepth = FMath::Max(1.0f, Profile.CaveMinDepth);
    const float MaxDepth = FMath::Max(MinDepth + 1.0f, Profile.CaveMaxDepth);

    if (DepthBelowSurface < MinDepth || DepthBelowSurface > MaxDepth)
    {
        return 0.0f;
    }

    const float DepthRange = MaxDepth - MinDepth;
    const float FadeInEnd = MinDepth + DepthRange * 0.2f;
    const float FadeOutStart = MaxDepth - DepthRange * 0.1f;

    float DepthAlpha = 1.0f;
    if (DepthBelowSurface < FadeInEnd)
    {
        DepthAlpha = FMath::Clamp((DepthBelowSurface - MinDepth) / (FadeInEnd - MinDepth), 0.0f, 1.0f);
    }
    else if (DepthBelowSurface > FadeOutStart)
    {
        DepthAlpha = FMath::Clamp((MaxDepth - DepthBelowSurface) / (MaxDepth - FadeOutStart), 0.0f, 1.0f);
    }

    const float Freq = FMath::Max(0.00001f, Profile.CaveWormFrequency);
    const float ZScale = FMath::Max(0.05f, Profile.CaveZScale);
    const float SeedF = static_cast<float>(Seed);

    const float N1 = WF_Perlin3D(
        (X + SeedF * 1.17f) * Freq,
        (Y + SeedF * 1.73f) * Freq,
        (Z + SeedF * 2.11f) * Freq * ZScale
    );

    const float N2 = WF_Perlin3D(
        (X + 1000.0f + SeedF * 0.37f) * Freq,
        (Y + 1000.0f + SeedF * 0.53f) * Freq,
        (Z + 1000.0f + SeedF * 0.89f) * Freq * ZScale
    );

    const float WormMask = 1.0f - (N1 * N1 + N2 * N2);

    const float Threshold = FMath::Clamp(TerrainSnapshot.CaveWormThreshold, 0.0f, 1.0f);

    if (WormMask <= Threshold)
    {
        return 0.0f;
    }

    const float CarveStrength = FMath::Max(0.0f, TerrainSnapshot.CaveCarveStrength);
    const float Intensity = FMath::Max(0.0f, Profile.CaveIntensity);

    const float RawCarve = (WormMask - Threshold) * CarveStrength * Intensity * DepthAlpha;

    return RawCarve;
}

// ═══════════════════════════════════════════════════════════════════
// LAYER 3: CANYON CARVING (v4.0)
// ═══════════════════════════════════════════════════════════════════

float FWorldForgeTerrainGeneratorInstance::ComputeCanyonCarve(
    float X, float Y, float Z,
    float SurfaceHeight, const FVoxelBiomeProfile& Profile) const
{
    const float MaxDepth = FMath::Max(1.0f, Profile.CanyonMaxDepth);
    const float DepthBelowSurface = SurfaceHeight - Z;

    if (DepthBelowSurface < -2.0f || DepthBelowSurface > MaxDepth)
    {
        return 0.0f;
    }

    const float RidgeRaw = FWorldForgeTerrainMath::SampleCanyonRidgeNoise(
        X, Y, TerrainSnapshot);

    const float Sharpness = FMath::Max(0.5f, Profile.CanyonSharpness);
    const float Ridge = FMath::Pow(FMath::Clamp(RidgeRaw, 0.0f, 1.0f), Sharpness);

    const float CanyonFloor = SurfaceHeight - Ridge * MaxDepth;

    const float OverhangNoise = WF_Perlin3D(
        X * 0.015f + 3100.0f,
        Y * 0.015f + 7300.0f,
        Z * 0.02f + 1900.0f
    ) * 3.0f;

    if (Z > (CanyonFloor + OverhangNoise) && Z < (SurfaceHeight + 1.0f))
    {
        const float DistFromFloor = Z - (CanyonFloor + OverhangNoise);
        const float DistFromSurface = SurfaceHeight - Z;
        const float WallFade = FMath::Min(
            FMath::Clamp(DistFromFloor / 3.0f, 0.0f, 1.0f),
            FMath::Clamp(DistFromSurface / 2.0f, 0.0f, 1.0f)
        );

        const float Intensity = FMath::Max(0.0f, Profile.CanyonIntensity);
        return WallFade * Intensity * 8.0f;
    }

    return 0.0f;
}

// ═══════════════════════════════════════════════════════════════════
// LAYER 4: LAVA TUBE SUB-LAYER (v4.0)
// ═══════════════════════════════════════════════════════════════════

float FWorldForgeTerrainGeneratorInstance::ComputeLavaTubeDensity(
    float X, float Y, float Z,
    float SurfaceHeight) const
{
    if (TerrainSnapshot.PipelineVolcanoes.Num() == 0)
    {
        return 0.0f;
    }

    const float DepthBelowSurface = SurfaceHeight - Z;

    if (DepthBelowSurface < 15.0f || DepthBelowSurface > 80.0f)
    {
        return 0.0f;
    }

    const float ChunkSize = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));
    const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
    const float HalfGridY = static_cast<float>(TerrainSnapshot.GridHeight) * 0.5f;
    const float TileX = (X / ChunkSize) + HalfGridX;
    const float TileY = (Y / ChunkSize) + HalfGridY;

    bool bInsideVolcano = false;
    for (const FVolcanoData& V : TerrainSnapshot.PipelineVolcanoes)
    {
        const float DX = TileX - V.Center.X;
        const float DY = TileY - V.Center.Y;
        if ((DX * DX + DY * DY) < (V.ConeRadius * V.ConeRadius))
        {
            bInsideVolcano = true;
            break;
        }
    }

    if (!bInsideVolcano)
    {
        return 0.0f;
    }

    const float Freq = 0.015f;
    const float ZCompress = FMath::Max(0.05f, TerrainSnapshot.LavaTubeZCompress);
    const float SeedF = static_cast<float>(Seed);

    const float N1 = WF_Perlin3D(
        (X + SeedF * 3.17f + 5000.0f) * Freq,
        (Y + SeedF * 3.73f + 5000.0f) * Freq,
        (Z + SeedF * 3.11f + 5000.0f) * Freq * ZCompress
    );

    const float N2 = WF_Perlin3D(
        (X + 2000.0f + SeedF * 1.37f) * Freq,
        (Y + 2000.0f + SeedF * 1.53f) * Freq,
        (Z + 2000.0f + SeedF * 1.89f) * Freq * ZCompress
    );

    const float WormMask = 1.0f - (N1 * N1 + N2 * N2);

    if (WormMask <= 0.60f)
    {
        return 0.0f;
    }

    const float DepthAlpha = FMath::Clamp((DepthBelowSurface - 15.0f) / 20.0f, 0.0f, 1.0f) *
        FMath::Clamp((80.0f - DepthBelowSurface) / 15.0f, 0.0f, 1.0f);

    return (WormMask - 0.60f) * 10.0f * DepthAlpha;
}

// ─── Legacy cave density (v3.0 — replaced by worm caves) ────────

float FWorldForgeTerrainGeneratorInstance::ComputeCaveDensity(float X, float Y, float Z, const FVoxelBiomeProfile& Profile) const
{
    return 0.0f;
}

// ═══════════════════════════════════════════════════════════════════
// SLOPE + MATERIAL CLASSIFICATION
// ═══════════════════════════════════════════════════════════════════

float FWorldForgeTerrainGeneratorInstance::ComputeApproximateSlopeDegrees(float VoxelX, float VoxelY) const
{
    // Local 4-voxel sampling for material slope classification; avoids tile-wide slope quantization that causes blocky material plates.
    const float SampleOffset = 4.0f;

    const float Hx0 = ComputeSurfaceHeight(VoxelX - SampleOffset, VoxelY);
    const float Hx1 = ComputeSurfaceHeight(VoxelX + SampleOffset, VoxelY);
    const float Hy0 = ComputeSurfaceHeight(VoxelX, VoxelY - SampleOffset);
    const float Hy1 = ComputeSurfaceHeight(VoxelX, VoxelY + SampleOffset);

    const float Dx = (Hx1 - Hx0) / (2.0f * SampleOffset);
    const float Dy = (Hy1 - Hy0) / (2.0f * SampleOffset);
    const float GradientMagnitude = FMath::Sqrt((Dx * Dx) + (Dy * Dy));

    return FMath::RadiansToDegrees(FMath::Atan(GradientMagnitude));
}

float FWorldForgeTerrainGeneratorInstance::ComputeNormalizedSurfaceElevation(float SurfaceHeight) const
{
    return FWorldForgeTerrainMath::ComputeNormalizedLandElevation(SurfaceHeight, TerrainSnapshot);
}

// ─── Surface material classification (coast-aware) ──────────────

int32 FWorldForgeTerrainGeneratorInstance::ClassifyTerrainMaterialLayer(float VoxelX, float VoxelY, float SurfaceHeight) const
{
    const float NormElev = ComputeNormalizedSurfaceElevation(SurfaceHeight);
    const float SlopeDeg = ComputeApproximateSlopeDegrees(VoxelX, VoxelY);
    const float SeaLevel = static_cast<float>(TerrainSnapshot.SeaLevelVoxels);

    // ── Biome tag lookup for special terrain types (Lava, Volcanic)
    {
        const float ChunkSizeF = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));
        const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
        const float HalfGridY = static_cast<float>(TerrainSnapshot.GridHeight) * 0.5f;

        const int32 ChunkX = FMath::FloorToInt((VoxelX / ChunkSizeF) + HalfGridX);
        const int32 ChunkY = FMath::FloorToInt((VoxelY / ChunkSizeF) + HalfGridY);
        const FIntPoint Cell = TerrainSnapshot.ClampCell(FIntPoint(ChunkX, ChunkY));

        const FGameplayTag BiomeTag = TerrainSnapshot.GetBiomeTag(Cell);

        static const FGameplayTag LavaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);
        if (LavaTag.IsValid() && BiomeTag.MatchesTag(LavaTag))
        {
            return TerrainLayer_Lava;
        }

        static const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);
        if (VolcanicTag.IsValid() && BiomeTag.MatchesTag(VolcanicTag))
        {
            return TerrainLayer_Basalt;
        }
    }

    if (SurfaceHeight <= SeaLevel)
    {
        return TerrainLayer_Water;
    }

    const float CoastBand = FMath::Max(0.0f, TerrainSnapshot.CoastBandVoxels);
    const float SandMaxElevation = FMath::Clamp(TerrainSnapshot.SandMaxElevationNormalized, 0.0f, 1.0f);
    if (SurfaceHeight <= (SeaLevel + CoastBand) || NormElev <= SandMaxElevation)
    {
        const float CoastDistanceTiles = FWorldForgeTerrainMath::SampleCoastalWaterDistanceTiles(
            VoxelX, VoxelY, TerrainSnapshot);
        const bool bNearCoastalWater = CoastDistanceTiles >= 0.0f &&
            CoastDistanceTiles <= WF_CoastMaterialReachTiles;
        const float SandSlopeLimit = FMath::Min(
            WF_CoastSandSlopeMaxDegrees,
            FMath::Max(0.0f, TerrainSnapshot.DirtSlopeThresholdDegrees));
        const bool bImmediateShore = bNearCoastalWater &&
            SurfaceHeight <= (SeaLevel + CoastBand) &&
            SlopeDeg <= SandSlopeLimit;
        const bool bLowGentleCoastShelf = bNearCoastalWater &&
            NormElev <= SandMaxElevation &&
            SlopeDeg <= SandSlopeLimit;

        if (bImmediateShore || bLowGentleCoastShelf)
        {
            return TerrainLayer_Sand;
        }
    }

    if (NormElev >= FMath::Clamp(TerrainSnapshot.SnowMinElevationNormalized, 0.0f, 1.0f))
    {
        return TerrainLayer_Snow;
    }

    if (SlopeDeg >= FMath::Max(0.0f, TerrainSnapshot.CliffSlopeThresholdDegrees))
    {
        return TerrainLayer_Rock;
    }

    if (NormElev >= FMath::Clamp(TerrainSnapshot.HighRockMinElevationNormalized, 0.0f, 1.0f))
    {
        return TerrainLayer_Rock;
    }

    if (NormElev >= FMath::Clamp(TerrainSnapshot.RockMinElevationNormalized, 0.0f, 1.0f) &&
        SlopeDeg >= FMath::Max(0.0f, TerrainSnapshot.RockSlopeThresholdDegrees))
    {
        return TerrainLayer_Rock;
    }

    if (NormElev >= FMath::Clamp(TerrainSnapshot.DirtMinElevationNormalized, 0.0f, 1.0f) ||
        SlopeDeg >= FMath::Max(0.0f, TerrainSnapshot.DirtSlopeThresholdDegrees))
    {
        return TerrainLayer_Dirt;
    }

    return TerrainLayer_Grass;
}

// ─── v4.0: Underground material classification ──────────────────

int32 FWorldForgeTerrainGeneratorInstance::ClassifyUndergroundMaterial(
    float VoxelX, float VoxelY, float Z, float SurfaceHeight) const
{
    const float DepthBelowSurface = SurfaceHeight - Z;
    const int32 SurfaceLayer = ClassifyTerrainMaterialLayer(VoxelX, VoxelY, SurfaceHeight);

    {
        const float ChunkSizeF = static_cast<float>(FMath::Max(1, TerrainSnapshot.ChunkSizeVoxels));
        const float HalfGridX = static_cast<float>(TerrainSnapshot.GridWidth) * 0.5f;
        const float HalfGridY = static_cast<float>(TerrainSnapshot.GridHeight) * 0.5f;
        const int32 ChunkX = FMath::FloorToInt((VoxelX / ChunkSizeF) + HalfGridX);
        const int32 ChunkY = FMath::FloorToInt((VoxelY / ChunkSizeF) + HalfGridY);
        const FIntPoint Cell = TerrainSnapshot.ClampCell(FIntPoint(ChunkX, ChunkY));
        const FGameplayTag BiomeTag = TerrainSnapshot.GetBiomeTag(Cell);

        static const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);
        static const FGameplayTag LavaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);

        if (LavaTag.IsValid() && BiomeTag.MatchesTag(LavaTag))
        {
            return TerrainLayer_Lava;
        }
        if (VolcanicTag.IsValid() && BiomeTag.MatchesTag(VolcanicTag))
        {
            return TerrainLayer_Basalt;
        }
    }

    if (DepthBelowSurface <= 4.5f)
    {
        switch (SurfaceLayer)
        {
        case TerrainLayer_Water:  return TerrainLayer_Water;
        case TerrainLayer_Sand:   return TerrainLayer_Sand;
        case TerrainLayer_Snow:   return TerrainLayer_Dirt;
        case TerrainLayer_Rock:   return TerrainLayer_Rock;
        case TerrainLayer_Lava:   return TerrainLayer_Lava;
        case TerrainLayer_Basalt: return TerrainLayer_Basalt;
        default:                  return TerrainLayer_Dirt;
        }
    }

    if (DepthBelowSurface <= 20.0f)
    {
        return TerrainLayer_Rock;
    }

    return TerrainLayer_DeepRock;
}

// ─── Material builder ───────────────────────────────────────────

FVoxelMaterial FWorldForgeTerrainGeneratorInstance::MakeVoxelMaterialFromLayer(int32 MaterialLayer) const
{
    FVoxelMaterialBuilder Builder;
    Builder.SetMaterialConfig(EVoxelMaterialConfig::SingleIndex);
    Builder.SetSingleIndex(static_cast<uint8>(FMath::Clamp(MaterialLayer, 0, 255)));
    return Builder.Build();
}

// ─── Helpers ────────────────────────────────────────────────────

bool FWorldForgeTerrainGeneratorInstance::IsSnapshotValid() const
{
    return TerrainSnapshot.IsValid();
}

FIntPoint FWorldForgeTerrainGeneratorInstance::ClampChunkCell(const FIntPoint& Cell) const
{
    return TerrainSnapshot.ClampCell(Cell);
}

int32 FWorldForgeTerrainGeneratorInstance::ComputeChunkBaseDepthVoxels(const FVoxelBiomeProfile& Profile) const
{
    const float SafeVoxelSize = (TerrainSnapshot.VoxelSize > KINDA_SMALL_NUMBER)
        ? TerrainSnapshot.VoxelSize
        : 100.0f;
    const int32 DerivedDepth = FMath::RoundToInt(Profile.NoiseAmplitude / SafeVoxelSize);
    return FMath::Max(12, DerivedDepth);
}

// ═══════════════════════════════════════════════════════════════════
// RESOURCE VEIN NOISE SAMPLING (v6.0)
//
// Each EResourceCategory has its own dual-noise intersection field.
// These functions are used by Step08_SeedResources (via pipeline scan)
// and ResourceSpawnSubsystem for deterministic node placement.
// They do NOT modify terrain visuals — ore rendering is handled
// entirely by Shatter Stone BP actors spawned at runtime.
//
// Tuning comes from FResourceNoiseRegistry so the terrain generator,
// Step08 pipeline scan, and ResourceSpawnSubsystem all share the
// same parameters.
// ═══════════════════════════════════════════════════════════════════

float FWorldForgeTerrainGeneratorInstance::SampleResourceDensity(
    EResourceCategory Category,
    float X, float Y, float Z) const
{
    const FResourceNoiseProfile& Prof = FResourceNoiseRegistry::GetProfile(Category);

    const float Freq = Prof.Frequency;
    const float ZScale = Prof.ZScale;
    const float SeedOffset = Prof.SeedOffset;
    const float SeedF = static_cast<float>(Seed);

    const float N1 = WF_Perlin3D(
        (X + SeedF * 0.71f + SeedOffset) * Freq,
        (Y + SeedF * 0.83f + SeedOffset) * Freq,
        (Z + SeedF * 0.97f + SeedOffset) * Freq * ZScale
    );

    const float N2 = WF_Perlin3D(
        (X + 3000.0f + SeedF * 0.41f + SeedOffset) * Freq,
        (Y + 3000.0f + SeedF * 0.59f + SeedOffset) * Freq,
        (Z + 3000.0f + SeedF * 0.67f + SeedOffset) * Freq * ZScale
    );

    // Intersection mask — high when both noise fields are near zero
    const float Mask = 1.0f - (N1 * N1 + N2 * N2);
    return FMath::Clamp(Mask, 0.0f, 1.0f);
}

bool FWorldForgeTerrainGeneratorInstance::IsNearSurface(
    float Z, float SurfaceHeight, float Tolerance) const
{
    return FMath::Abs(Z - SurfaceHeight) <= Tolerance;
}

// ─── Legacy stubs (kept for header compatibility) ───────────────

float FWorldForgeTerrainGeneratorInstance::SampleContinentHeight(float X, float Y) const { return 0.0f; }
float FWorldForgeTerrainGeneratorInstance::SampleBasinDepth(float X, float Y) const { return 0.0f; }
float FWorldForgeTerrainGeneratorInstance::SampleRiverCarve(float X, float Y) const { return 0.0f; }
float FWorldForgeTerrainGeneratorInstance::SampleMountainHeight(float X, float Y) const { return 0.0f; }

float FWorldForgeTerrainGeneratorInstance::ComputeMacroBaseHeight(float VoxelX, float VoxelY) const
{
    return FWorldForgeTerrainMath::ComputeSurfaceHeight(VoxelX, VoxelY, TerrainSnapshot);
}
