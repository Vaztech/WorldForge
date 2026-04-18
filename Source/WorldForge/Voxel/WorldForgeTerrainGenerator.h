#pragma once

#include "CoreMinimal.h"
#include "FastNoise/VoxelFastNoise.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "WorldForgeTerrainSnapshot.h"
#include "WorldForgeTerrainGenerator.generated.h"

UCLASS(Blueprintable)
class WORLDFORGE_API UWorldForgeTerrainGenerator : public UVoxelGenerator
{
    GENERATED_BODY()

public:
    // Plain C++ snapshot data. Intentionally not reflected.
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
    float SampleContinentHeight(float X, float Y) const;
    float SampleBasinDepth(float X, float Y) const;
    float SampleRiverCarve(float X, float Y) const;

    float SampleTileElevationBilinear(float VoxelX, float VoxelY) const;
    const FVoxelBiomeProfile* SamplePrimaryBiomeProfile(float VoxelX, float VoxelY) const;

    float ComputeMacroBaseHeight(float VoxelX, float VoxelY) const;
    float ComputeDetailHeight(float VoxelX, float VoxelY, const FVoxelBiomeProfile& Profile) const;
    float ComputeSurfaceHeight(float VoxelX, float VoxelY) const;
    float ComputeCaveDensity(float X, float Y, float Z, const FVoxelBiomeProfile& Profile) const;

    bool IsSnapshotValid() const;
    FIntPoint ClampChunkCell(const FIntPoint& Cell) const;
    int32 ComputeChunkBaseDepthVoxels(const FVoxelBiomeProfile& Profile) const;

private:
    const FWorldForgeTerrainSnapshot TerrainSnapshot;
    const int32 Seed;

    mutable FVoxelFastNoise Noise2D;
    mutable FVoxelFastNoise CaveNoise;
};