#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DA_VoxelVisualRuleSet.generated.h"

class UStaticMesh;

USTRUCT(BlueprintType)
struct FBuildingVisualRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    FGameplayTag BuildingTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    TObjectPtr<UStaticMesh> Mesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    FVector RelativeOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    FRotator RelativeRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    FVector RelativeScale = FVector(1.f, 1.f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    bool bUseHISM = true;
};

USTRUCT(BlueprintType)
struct FBiomePropRule
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    FGameplayTag BiomeTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    TObjectPtr<UStaticMesh> Mesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    int32 Density = 16;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    FVector MinScale = FVector(0.9f, 0.9f, 0.9f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    FVector MaxScale = FVector(1.1f, 1.1f, 1.1f);
};

UCLASS(BlueprintType)
class WORLDFORGE_API UDA_VoxelVisualRuleSet : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    TArray<FBuildingVisualRule> BuildingRules;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|VSS")
    TArray<FBiomePropRule> BiomePropRules;

    UFUNCTION(BlueprintPure, Category = "WorldForge|VSS")
    bool GetBuildingRule(FGameplayTag BuildingTag, FBuildingVisualRule& OutRule) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|VSS")
    bool GetBiomePropRule(FGameplayTag BiomeTag, FBiomePropRule& OutRule) const;
};