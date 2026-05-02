#pragma once

#include "CoreMinimal.h"
#include "FastNoise/VoxelFastNoise.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "WorldForgeTerrainSnapshot.h"
#include "WorldForgeTerrainGenerator.generated.h"

enum : int32
{
    TerrainLayer_Sand = 0,
    TerrainLayer_Grass = 1,
    TerrainLayer_Dirt = 2,
    TerrainLayer_Rock = 3,
    TerrainLayer_Snow = 4,
    TerrainLayer_Water = 5,
    TerrainLayer_Lava = 6,
    TerrainLayer_Basalt = 7,
    TerrainLayer_DeepRock = 8    // v4.0: underground strata
};

UCLASS(Blueprintable)
class WORLDFORGE_API UWorldForgeTerrainGenerator : public UVoxelGenerator
{
    GENERATED_BODY()

public:
    FWorldForgeTerrainSnapshot TerrainSnapshot;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Terrain")
    int32 Seed = 1337;

public:
    void SetTerrainSnapshot(const FWorldForgeTerrainSnapshot& InSnapshot)
    {
        TerrainSnapshot = InSnapshot;
    }

    void SetSeed(int32 InSeed)
    {
        Seed = InSeed;
    }

    virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
};

class FWorldForgeTerrainGeneratorInstance
    : public TVoxelGeneratorInstanceHelper<FWorldForgeTerrainGeneratorInstance, UWorldForgeTerrainGenerator>
{
public:
    using Super = TVoxelGeneratorInstanceHelper<FWorldForgeTerrainGeneratorInstance, UWorldForgeTerrainGenerator>;

public:
    explicit FWorldForgeTerrainGeneratorInstance(UWorldForgeTerrainGenerator& GeneratorObject);

    virtual void Init(const FVoxelGeneratorInit& InitStruct) override;

    v_flt GetValueImpl(
        v_flt X,
        v_flt Y,
        v_flt Z,
        int32 LOD,
        const FVoxelItemStack& Items) const;

    FVoxelMaterial GetMaterialImpl(
        v_flt X,
        v_flt Y,
        v_flt Z,
        int32 LOD,
        const FVoxelItemStack& Items) const;

    TVoxelRange<v_flt> GetValueRangeImpl(
        const FVoxelIntBox& Bounds,
        int32 LOD,
        const FVoxelItemStack& Items) const;

    virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;

private:
    // ── Legacy stubs (kept for header compat) ──
    float SampleContinentHeight(float X, float Y) const;
    float SampleBasinDepth(float X, float Y) const;
    float SampleRiverCarve(float X, float Y) const;
    float SampleMountainHeight(float X, float Y) const;

    // ── Tile / biome lookups ──
    float SampleTileElevationBilinear(float VoxelX, float VoxelY) const;
    const FVoxelBiomeProfile* SamplePrimaryBiomeProfile(float VoxelX, float VoxelY) const;

    // ── Surface height (unchanged — 2D macro shape) ──
    float ComputeMacroBaseHeight(float VoxelX, float VoxelY) const;
    float ComputeDetailHeight(float VoxelX, float VoxelY, const FVoxelBiomeProfile& Profile) const;
    float ComputeSurfaceHeight(float VoxelX, float VoxelY) const;

    // ── v4.0: 3D SDF density layers ──
    // Worm cave: two independent 3D noise fields intersected to form tunnels.
    // Returns positive values where caves exist (pushes density toward air).
    float ComputeWormCaveDensity(float X, float Y, float Z,
        float SurfaceHeight, const FVoxelBiomeProfile& Profile) const;

    // Canyon carving: domain-warped ridge noise cutting into the surface.
    // Returns positive values where canyons are carved.
    float ComputeCanyonCarve(float X, float Y, float Z,
        float SurfaceHeight, const FVoxelBiomeProfile& Profile) const;

    // Lava tube sub-layer: horizontal-biased worm caves inside volcano cones.
    // Returns positive values where tubes are carved.
    float ComputeLavaTubeDensity(float X, float Y, float Z,
        float SurfaceHeight) const;

    // ── v3.0 legacy cave (replaced by worm caves, kept for reference) ──
    float ComputeCaveDensity(float X, float Y, float Z, const FVoxelBiomeProfile& Profile) const;

    // ── Material helpers ──
    float ComputeApproximateSlopeDegrees(float VoxelX, float VoxelY) const;
    float ComputeNormalizedSurfaceElevation(float SurfaceHeight) const;
    int32 ClassifyTerrainMaterialLayer(float VoxelX, float VoxelY, float SurfaceHeight) const;
    int32 ClassifyUndergroundMaterial(float VoxelX, float VoxelY, float Z, float SurfaceHeight) const;
    FVoxelMaterial MakeVoxelMaterialFromLayer(int32 MaterialLayer) const;

    // ── Snapshot helpers ──
    bool IsSnapshotValid() const;
    FIntPoint ClampChunkCell(const FIntPoint& Cell) const;
    int32 ComputeChunkBaseDepthVoxels(const FVoxelBiomeProfile& Profile) const;

private:
    const FWorldForgeTerrainSnapshot TerrainSnapshot;
    const int32 Seed;

    mutable FVoxelFastNoise Noise2D;
    mutable FVoxelFastNoise CaveNoise;      // worm field A
    mutable FVoxelFastNoise CaveNoiseB;     // v4.0: worm field B
    mutable FVoxelFastNoise CanyonNoise;    // v4.0: canyon domain warp
    mutable FVoxelFastNoise LavaTubeNoise;  // v4.0: lava tube field
    mutable FVoxelFastNoise LavaTubeNoiseB; // v4.0: lava tube field B
};
