#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "RaceDataAsset.generated.h"

UCLASS(BlueprintType)
class WORLDFORGE_API URaceDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race")    FGameplayTag RaceTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Race")    FText        DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float MiningMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float CraftingMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float CookingMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float FarmingMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float BuildingMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float CombatMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float MagicLearningMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float HaulingMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float MedicineMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")   float ResearchMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Needs")   float FoodNeedDecayMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Needs")   float SleepNeedDecayMod = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")   TMap<FGameplayTag, float> BiomeAffinities;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Magic")   TMap<FGameplayTag, float> MagicAffinities;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cosmic")  TMap<FGameplayTag, float> CosmicResonance;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy") float  CurrencyBaseValue = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy") FText  CurrencyName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mechanics") FGameplayTag UniqueMechanic;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physical") bool  bRequiresFood = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physical") bool  bRequiresSleep = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physical") float BaseHealthPoints = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Physical") float BaseMoveSpeed = 1.f;
};