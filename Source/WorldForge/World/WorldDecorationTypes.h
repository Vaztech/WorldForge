#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPtr.h"
#include "WorldDecorationTypes.generated.h"

class AActor;
class UStaticMesh;

UENUM(BlueprintType)
enum class EWorldDecorationFootprint : uint8
{
    OneByOne     UMETA(DisplayName = "1x1"),
    TwoByTwo     UMETA(DisplayName = "2x2"),
    ThreeByThree UMETA(DisplayName = "3x3")
};

USTRUCT(BlueprintType)
struct FWorldDecorationEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FString EntryName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    TSoftClassPtr<AActor> ActorClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    TSoftObjectPtr<UStaticMesh> MeshAsset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FGameplayTag AllowedBiomeTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FGameplayTag ResourceTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    EWorldDecorationFootprint Footprint = EWorldDecorationFootprint::OneByOne;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration", meta = (ClampMin = "0"))
    int32 MaxHeightDeltaVoxels = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration", meta = (ClampMin = "0"))
    int32 BiomeTransitionAllowanceCells = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    bool bRejectWaterOrCoastAtAnchor = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    bool bBlocksNavigation = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    bool bChoppable = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration", meta = (ClampMin = "1"))
    int32 ClusterCellSize = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ClusterChance = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Density = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    int32 SeedOffset = 0;
};

USTRUCT(BlueprintType)
struct FWorldDecorationSpawnRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FString EntryName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FString ActorClassPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FString MeshAssetPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FGameplayTag ResourceTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FIntPoint AnchorCell = FIntPoint::ZeroValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    int32 FootprintSize = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    bool bBlocksNavigation = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    bool bChoppable = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FVector WorldLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FRotator Rotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Decoration")
    FVector Scale = FVector(1.0f, 1.0f, 1.0f);
};