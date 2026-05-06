#include "UI/WorldForgeEmbarkationManager.h"

void UWorldForgeEmbarkationManager::SetSeedString(const FString& InSeedString)
{
	EmbarkConfig.SeedString = InSeedString.TrimStartAndEnd().IsEmpty()
		? TEXT("12345")
		: InSeedString.TrimStartAndEnd();

	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Seed set to '%s' ResolvedSeed=%d"),
		*EmbarkConfig.SeedString,
		GetResolvedSeed());
}

void UWorldForgeEmbarkationManager::SetWorldSize(int32 InWorldSize)
{
	// Clamp to supported GDD world sizes.
	// Blueprint should send 128, 256, 512, or 1024.
	EmbarkConfig.WorldSize = FMath::Clamp(InWorldSize, 128, 1024);

	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] WorldSize set to %d"),
		EmbarkConfig.WorldSize);
}

void UWorldForgeEmbarkationManager::AddColonist(const FWorldForgeEmbarkColonist& InColonist)
{
	EmbarkConfig.StartingColonists.Add(InColonist);

	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Added colonist '%s'. Total=%d"),
		*InColonist.Name,
		EmbarkConfig.StartingColonists.Num());
}

void UWorldForgeEmbarkationManager::SetColonists(const TArray<FWorldForgeEmbarkColonist>& InColonists)
{
	EmbarkConfig.StartingColonists = InColonists;

	bHasUserEmbarkConfig = true;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Colonists set. Total=%d"),
		EmbarkConfig.StartingColonists.Num());
}

void UWorldForgeEmbarkationManager::ClearColonists()
{
	EmbarkConfig.StartingColonists.Reset();

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Colonists cleared."));
}

void UWorldForgeEmbarkationManager::ClearEmbarkConfig()
{
	EmbarkConfig = FWorldForgeEmbarkConfig();
	bHasUserEmbarkConfig = false;

	UE_LOG(LogTemp, Log,
		TEXT("[EmbarkationManager] Embark config cleared."));
}

FWorldForgeEmbarkConfig UWorldForgeEmbarkationManager::GetEmbarkConfig() const
{
	return EmbarkConfig;
}

FString UWorldForgeEmbarkationManager::GetSeedString() const
{
	return EmbarkConfig.SeedString;
}

int32 UWorldForgeEmbarkationManager::GetResolvedSeed() const
{
	return static_cast<int32>(GetTypeHash(EmbarkConfig.SeedString));
}

int32 UWorldForgeEmbarkationManager::GetWorldSize() const
{
	return EmbarkConfig.WorldSize;
}

bool UWorldForgeEmbarkationManager::HasUserEmbarkConfig() const
{
	return bHasUserEmbarkConfig;
}