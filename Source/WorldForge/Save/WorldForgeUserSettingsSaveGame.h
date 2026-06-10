#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "WorldForgeUserSettingsSaveGame.generated.h"

UCLASS()
class WORLDFORGE_API UWorldForgeUserSettingsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	int32 SaveVersion = 2;

	UPROPERTY(SaveGame)
	bool bAutosaveEnabled = true;

	UPROPERTY(SaveGame)
	bool bTutorialHintsEnabled = true;

	UPROPERTY(SaveGame)
	bool bPauseOnMenuEnabled = true;

	UPROPERTY(SaveGame)
	float MasterVolume = 1.0f;

	UPROPERTY(SaveGame)
	float MusicVolume = 1.0f;

	UPROPERTY(SaveGame)
	float SfxVolume = 1.0f;

	UPROPERTY(SaveGame)
	float AmbienceVolume = 1.0f;

	UPROPERTY(SaveGame)
	bool bVSyncEnabled = true;

	UPROPERTY(SaveGame)
	FString ResolutionLabel = TEXT("1920x1080");

	UPROPERTY(SaveGame)
	FString WindowModeLabel = TEXT("Borderless");

	UPROPERTY(SaveGame)
	float TextScale = 1.0f;

	UPROPERTY(SaveGame)
	bool bHighContrastMode = false;

	UPROPERTY(SaveGame)
	bool bColorblindAssist = false;

	UPROPERTY(SaveGame)
	bool bReduceMotion = false;

	UPROPERTY(SaveGame)
	bool bLargeTextMode = false;

	UPROPERTY(SaveGame)
	float AccessibilityTextScale = 1.0f;
};
