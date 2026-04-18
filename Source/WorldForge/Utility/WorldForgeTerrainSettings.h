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

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel")
    float ChunkWorldSize = 3200.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel")
    float VoxelSize = 100.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel")
    float TerrainSurfaceZOffset = 50.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel")
    bool bAddGlobalFoundation = true;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Voxel")
    float GlobalFoundationDepthWorld = 5000.0f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Perlin")
    FPerlinOctaveSettings DefaultPerlinSettings;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SandMaxElevationNormalized = 0.35f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RockMinElevationNormalized = 0.75f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SnowMinElevationNormalized = 0.90f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Water")
    int32 SeaLevelVoxels = 2;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    float ContinentFrequency = 0.00018f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    int32 ContinentHeightRangeVoxels = 90;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    float ContinentExponent = 2.2f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    float BasinFrequency = 0.00055f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    float BasinThreshold = 0.82f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Macro")
    int32 LakeBasinDepthVoxels = 14;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Rivers")
    float RiverFrequency = 0.0015f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Rivers")
    float RiverThreshold = 0.92f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Rivers")
    int32 RiverCarveDepthVoxels = 5;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Rivers")
    float RiverSharpness = 2.2f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Performance")
    bool bUseFastTerrainSampling = true;

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

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Runtime")
    float TerrainRetryIntervalSeconds = 0.25f;

    UPROPERTY(EditAnywhere, Config, BlueprintReadWrite, Category = "Runtime")
    int32 MaxTerrainRetryCount = 40;
};