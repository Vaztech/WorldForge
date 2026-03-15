#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataRegistry.generated.h"

UCLASS(BlueprintType)
class WORLDFORGE_API UItemDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FGameplayTag ItemTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FText Description;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") float BaseWeight = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") bool bIsStackable = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") int32 MaxStackSize = 50;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") float BaseAnchorValue = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") float PriceElasticity = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") bool bIsVeblenGood = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item") FGameplayTagContainer ItemCategories;
};

UCLASS(BlueprintType)
class WORLDFORGE_API UBuildingDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building") FGameplayTag BuildingTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building") FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building") TMap<FGameplayTag, int32> ConstructionCost;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building") TArray<FGameplayTag> UnlocksTaskTypes;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building") int32 MaxWorkers = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building") float MoodModifier = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building") FGameplayTag BuildingCategory;
};

UCLASS(BlueprintType)
class WORLDFORGE_API UJobDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Job") FGameplayTag JobTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Job") FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Job") FGameplayTag RequiredSkill;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Job") FGameplayTag RequiredBuildingTag;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Job") float WorkDuration = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Job") TMap<FGameplayTag, int32> InputItems;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Job") TMap<FGameplayTag, int32> OutputItems;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Job") int32 Priority = 5;
};

UCLASS()
class WORLDFORGE_API UWorldForgeDataRegistry : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    void RegisterItem(UItemDefinition* Def);
    void RegisterBuilding(UBuildingDefinition* Def);
    void RegisterJob(UJobDefinition* Def);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Registry")
    const UItemDefinition* FindItem(FGameplayTag Tag) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Registry")
    const UBuildingDefinition* FindBuilding(FGameplayTag Tag) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Registry")
    const UJobDefinition* FindJob(FGameplayTag Tag) const;

    const TMap<FGameplayTag, UItemDefinition*>& GetAllItems() const { return Items; }
    const TMap<FGameplayTag, UBuildingDefinition*>& GetAllBuildings() const { return Buildings; }
    const TMap<FGameplayTag, UJobDefinition*>& GetAllJobs() const { return Jobs; }

    void RegisterModContent(const FString& ModID, UPrimaryDataAsset* Asset);

private:
    TMap<FGameplayTag, UItemDefinition*> Items;
    TMap<FGameplayTag, UBuildingDefinition*> Buildings;
    TMap<FGameplayTag, UJobDefinition*> Jobs;
};