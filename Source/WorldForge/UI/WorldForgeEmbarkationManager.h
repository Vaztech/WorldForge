#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeEmbarkationManager.generated.h"

USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeEmbarkColonist
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	FString Name = TEXT("Colonist");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	FGameplayTag RaceTag;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	FString SeedString = TEXT("12345");

	// Stores actual grid size selected by frontend:
	// 128 = Small, 256 = Medium, 512 = Large, 1024 = OMG.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	int32 WorldSize = 256;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Embark")
	TArray<FWorldForgeEmbarkColonist> StartingColonists;
};

UCLASS(BlueprintType)
class WORLDFORGE_API UWorldForgeEmbarkationManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
	void SetSeedString(const FString& InSeedString);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
	void SetWorldSize(int32 InWorldSize);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
	void AddColonist(const FWorldForgeEmbarkColonist& InColonist);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
	void SetColonists(const TArray<FWorldForgeEmbarkColonist>& InColonists);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
	void ClearColonists();

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Embark")
	void ClearEmbarkConfig();

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
	FWorldForgeEmbarkConfig GetEmbarkConfig() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
	FString GetSeedString() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
	int32 GetResolvedSeed() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
	int32 GetWorldSize() const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Embark")
	bool HasUserEmbarkConfig() const;

private:
	UPROPERTY()
	FWorldForgeEmbarkConfig EmbarkConfig;

	// Prevents direct WorldGenTest launches from accidentally using default frontend config.
	UPROPERTY()
	bool bHasUserEmbarkConfig = false;
};