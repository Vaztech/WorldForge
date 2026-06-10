#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Embark/EmbarkLoadoutTypes.h"
#include "WorldForgeEmbarkationManager.generated.h"

UENUM(BlueprintType)
enum class EWorldForgeColonyMode : uint8
{
	Mixed UMETA(DisplayName = "Mixed Colony"),
	SingleRace UMETA(DisplayName = "Single Race Colony")
};

USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeEmbarkColonist
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	FString Name = TEXT("Colonist");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	FGameplayTag RaceTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	int32 Age = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark", meta = (MultiLine = true))
	FString Backstory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	FName RoleId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	TMap<FName, int32> Skills;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	TArray<FString> Traits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	TArray<FGameplayTag> TraitTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	int32 Strength = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	int32 Dexterity = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	int32 Intelligence = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	int32 Endurance = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	int32 Charisma = 10;
};

USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeEmbarkConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|World")
	FString WorldName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|World")
	FString SaveSlotName = TEXT("WorldForge_Save");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|World")
	FString SeedString = TEXT("12345");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|World")
	int32 ResolvedSeed = 0;

	// Stores actual grid size selected by frontend:
	// 128 = Small, 256 = Medium, 512 = Large, 1024 = OMG.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|World")
	int32 WorldSizeGrid = 256;

	// Legacy mirror kept so existing Blueprint struct pins do not lose data before the UI is rebuilt.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|World")
	int32 WorldSize = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Colony")
	EWorldForgeColonyMode ColonyMode = EWorldForgeColonyMode::Mixed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Colony")
	FGameplayTag SelectedRaceTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Colony")
	TArray<FGameplayTag> AllowedRaceTags;

	// Fallback race used when a colonist entry has no RaceTag selected.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Colony")
	FGameplayTag DefaultRaceTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Colonists", meta = (ClampMin = "0"))
	int32 StartingColonistCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Colonists")
	TArray<FWorldForgeEmbarkColonist> StartingColonists;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Loadout", meta = (ClampMin = "0"))
	int32 MaxEmbarkPoints = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Loadout", meta = (ClampMin = "0"))
	int32 UsedEmbarkPoints = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Loadout")
	TArray<FEmbarkSelectedItem> SelectedLoadoutItems;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|World")
	int32 SelectedEmbarkTileX = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|World")
	int32 SelectedEmbarkTileY = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|World")
	int32 SelectedEmbarkTileIndex = -1;
};

USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeSimpleEmbarkLoadoutOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Loadout")
	FName ItemId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Loadout")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Loadout")
	FGameplayTag ItemTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark|Loadout", meta = (ClampMin = "0"))
	int32 PointCost = 0;
};

UCLASS(BlueprintType)
class WORLDFORGE_API UWorldForgeEmbarkationManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|World")
	void SetWorldName(const FString& InWorldName);

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|World")
	FString GetWorldName() const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|World")
	void SetSeedString(const FString& InSeedString);

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|World")
	FString GetSeedString() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|World")
	int32 GetResolvedSeed() const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|World")
	void SetWorldSize(int32 InWorldSizeGrid);

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|World")
	int32 GetWorldSizeGrid() const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|World")
	void GenerateOrUpdateSaveSlotNameFromWorldName();

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|World")
	FString GetSaveSlotName() const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colony")
	void SetColonyMode(EWorldForgeColonyMode InColonyMode);

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Colony")
	EWorldForgeColonyMode GetColonyMode() const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colony")
	void SetSelectedRaceTag(FGameplayTag InRaceTag);

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Colony")
	FGameplayTag GetSelectedRaceTag() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Validation")
	bool IsWorldNameValid(FString& OutReason) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Validation")
	bool IsEmbarkWorldConfigValid(FString& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
	void ResetEmbarkConfigToDefaults();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colonists")
	void GenerateStartingColonists();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colonists")
	bool EnsureStartingColonistsGenerated();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colonists")
	bool RerollStartingColonist(int32 ColonistIndex);

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Colonists")
	TArray<FWorldForgeEmbarkColonist> GetStartingColonists() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Colonists")
	bool GetStartingColonist(int32 ColonistIndex, FWorldForgeEmbarkColonist& OutColonist) const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colonists")
	void AddColonist(const FWorldForgeEmbarkColonist& InColonist);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colonists")
	void SetColonists(const TArray<FWorldForgeEmbarkColonist>& InColonists);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colonists")
	void SetColonistRaceByIndex(int32 ColonistIndex, FGameplayTag RaceTag);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colony")
	void SetDefaultRace(FGameplayTag RaceTag);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colony")
	TArray<FGameplayTag> GetDefaultPlayableRaceTags() const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colonists")
	void EnsureDefaultColonists(int32 DesiredCount);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Colonists")
	void ClearColonists();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
	void ClearEmbarkConfig();

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
	FWorldForgeEmbarkConfig GetEmbarkConfig() const;

	// Compatibility accessor for existing Blueprints. Prefer GetWorldSizeGrid for new UI.
	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|World")
	int32 GetWorldSize() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
	bool HasUserEmbarkConfig() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Loadout")
	TArray<FWorldForgeSimpleEmbarkLoadoutOption> GetStarterEmbarkLoadoutOptions() const;

	void RegisterDynamicSeedLoadoutOption(FName ItemId, const FText& DisplayName, int32 PointCost = 2);

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Loadout")
	int32 GetMaxEmbarkPoints() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Loadout")
	int32 GetUsedEmbarkPoints() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Loadout")
	int32 GetRemainingEmbarkPoints() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Loadout")
	int32 GetEmbarkLoadoutItemQuantity(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Loadout")
	int32 GetEmbarkLoadoutItemCost(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Loadout")
	int32 GetEmbarkLoadoutItemTotalCost(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Loadout")
	FText GetEmbarkLoadoutItemDisplayName(FName ItemId) const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Loadout")
	void ClearEmbarkLoadout();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Loadout")
	bool SetEmbarkLoadoutItemQuantity(FName ItemId, int32 Quantity);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Loadout")
	bool AddEmbarkLoadoutItem(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Loadout")
	bool RemoveEmbarkLoadoutItem(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Loadout")
	bool AddLoadoutItemById(FName ItemId, int32 DeltaQty = 1);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Loadout")
	bool RemoveLoadoutItemById(FName ItemId, int32 DeltaQty = 1);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Loadout")
	bool SetLoadoutItemQuantity(FName ItemId, int32 Quantity);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Loadout")
	bool ClearLoadoutItem(FName ItemId);

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Loadout")
	TArray<FEmbarkSelectedItem> GetSelectedLoadoutItems() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|Validation")
	bool IsEmbarkLoadoutValid(FString& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|Loadout")
	bool LogEmbarkLoadoutContinue() const;

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark|World")
	void SetSelectedEmbarkTile(int32 InTileX, int32 InTileY, int32 InTileIndex);

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|World")
	bool GetSelectedEmbarkTile(int32& OutTileX, int32& OutTileY, int32& OutTileIndex) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark|World")
	bool HasSelectedEmbarkTile() const;

private:
	UPROPERTY()
	FWorldForgeEmbarkConfig EmbarkConfig;

	UPROPERTY()
	TMap<FName, FWorldForgeSimpleEmbarkLoadoutOption> DynamicSeedLoadoutOptions;

	UPROPERTY()
	TMap<int32, int32> StartingColonistRerollCounts;

	// Prevents direct WorldGenTest launches from accidentally using default frontend config.
	UPROPERTY()
	bool bHasUserEmbarkConfig = false;

	static FString MakeSafeSaveSlotName(const FString& InWorldName);
	static int32 ResolveSeedString(const FString& InSeedString);

	FWorldForgeEmbarkColonist GenerateStartingColonistProfile(int32 ColonistIndex, int32 RerollSalt) const;
	TArray<FGameplayTag> GetAllowedPlayableRaceTagsForGeneration() const;
	FGameplayTag ResolveGeneratedColonistRace(FRandomStream& RandomStream) const;
	FGameplayTag ResolveFallbackPlayableRaceTag() const;
	FGameplayTag ResolveStartingRaceForIndex(int32 Index) const;
	bool IsPlayableRaceTag(FGameplayTag RaceTag) const;

	const FWorldForgeSimpleEmbarkLoadoutOption* FindStarterEmbarkLoadoutOption(FName ItemId) const;
	FWorldForgeSimpleEmbarkLoadoutOption MakeDynamicSeedOption(FName ItemId) const;
	bool TryResolveStarterEmbarkLoadoutOption(FName ItemId, FWorldForgeSimpleEmbarkLoadoutOption& OutOption) const;
	int32 FindSelectedLoadoutItemIndex(const FWorldForgeSimpleEmbarkLoadoutOption& Option) const;
	int32 FindSelectedLoadoutItemIndexByItemId(FName ItemId) const;
	int32 CalculateUsedEmbarkPoints() const;
	void RefreshUsedEmbarkPoints();
};
